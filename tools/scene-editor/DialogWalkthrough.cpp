/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Dialog walkthrough: linear navigation of conversation lines + TTS.
 ******************************************************************************/

#include "DialogWalkthrough.h"
#include "EditorInput.h"

#include "ConversationHelpers.h"
#include "EditorButton.h"
#include "EditorPrefs.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "TtsVoiceMarkup.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <set>
#include <sstream>

using timberline_engine::builtinVoiceIds;
using timberline_engine::isKnownBuiltinVoiceId;
using timberline_engine::normalizeVoiceId;

namespace timberline_editor
{

namespace
{

const float kToolbarH = 36.0f;
const float kListW = 240.0f;
const float kRowH = 22.0f;
const float kFontEdit = 16.0f;
const float kLineH = 20.0f;

std::string truncateOneLine(const std::string& text, size_t maxLen)
{
    std::string compact;
    compact.reserve(std::min(text.size(), maxLen + 8));
    bool lastSpace = false;
    for (char ch : text)
    {
        if (ch == '\n' || ch == '\r' || ch == '\t')
        {
            if (!lastSpace && !compact.empty())
            {
                compact.push_back(' ');
                lastSpace = true;
            }
            continue;
        }
        compact.push_back(ch);
        lastSpace = (ch == ' ');
        if (compact.size() >= maxLen)
            break;
    }
    if (text.size() > maxLen)
    {
        if (compact.size() > maxLen - 1)
            compact.resize(maxLen - 1);
        compact += "...";
    }
    return compact;
}

void insertUtf8(std::string& buffer, int& cursor, int codepoint)
{
    if (codepoint <= 0)
        return;
    char bytes[5] = {};
    int size = 0;
    if (codepoint < 0x80)
    {
        bytes[0] = static_cast<char>(codepoint);
        size = 1;
    }
    else if (codepoint <= 0x7FF)
    {
        bytes[0] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
        bytes[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        bytes[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 3;
    }
    else if (codepoint <= 0x10FFFF)
    {
        bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    }
    if (size <= 0)
        return;
    if (cursor < 0)
        cursor = 0;
    if (cursor > static_cast<int>(buffer.size()))
        cursor = static_cast<int>(buffer.size());
    buffer.insert(static_cast<size_t>(cursor), bytes, static_cast<size_t>(size));
    cursor += size;
}

void backspaceUtf8(std::string& buffer, int& cursor)
{
    if (cursor <= 0 || buffer.empty())
        return;
    int i = cursor - 1;
    while (i > 0 && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        --i;
    buffer.erase(static_cast<size_t>(i), static_cast<size_t>(cursor - i));
    cursor = i;
}

} // namespace

const char* DialogWalkthrough::fieldKey(DialogWalkStep::Field field)
{
    switch (field)
    {
    case DialogWalkStep::Field::Intro:
        return "intro";
    case DialogWalkStep::Field::ResumeIntro:
        return "resumeIntro";
    case DialogWalkStep::Field::LineText:
        return "text";
    case DialogWalkStep::Field::Response:
    default:
        return "response";
    }
}

const char* DialogWalkthrough::ttsFlagKey(DialogWalkStep::Field field)
{
    return field == DialogWalkStep::Field::ResumeIntro ? "resumeTts" : "tts";
}

const char* DialogWalkthrough::ttsVoiceKey(DialogWalkStep::Field field)
{
    return field == DialogWalkStep::Field::ResumeIntro ? "resumeTtsVoice" : "ttsVoice";
}

const char* DialogWalkthrough::ttsAudioKey(DialogWalkStep::Field field)
{
    return field == DialogWalkStep::Field::ResumeIntro ? "resumeTtsAudio" : "ttsAudio";
}

const char* DialogWalkthrough::ttsTextKey(DialogWalkStep::Field field)
{
    return field == DialogWalkStep::Field::ResumeIntro ? "resumeTtsText" : "ttsText";
}

const char* DialogWalkthrough::ttsShaKey(DialogWalkStep::Field field)
{
    return field == DialogWalkStep::Field::ResumeIntro ? "resumeTtsTextSha256"
                                                      : "ttsTextSha256";
}

void DialogWalkthrough::appendChoiceSteps(
    const nlohmann::json& choice,
    const std::string& objectPointer,
    const std::string& breadcrumb,
    const std::string& sceneId,
    int depth)
{
    if (!choice.is_object())
        return;

    const std::string label = choiceTreeLabel(choice);
    const std::string id =
        choice.contains("id") && choice["id"].is_string() ? choice["id"].get<std::string>()
                                                          : "";
    const std::string crumb =
        breadcrumb.empty() ? label : (breadcrumb + " > " + label);

    if (choice.contains("response") && choice["response"].is_string())
    {
        DialogWalkStep step;
        step.sceneId = sceneId;
        step.objectPointer = objectPointer;
        step.field = DialogWalkStep::Field::Response;
        step.treeKey = "choice:" + objectPointer;
        step.breadcrumb = crumb;
        step.stepLabel = id.empty() ? "Response" : id;
        step.playerLabel = label;
        step.objectId = id;
        steps.push_back(std::move(step));
    }
    else if (choice.contains("text") && choice["text"].is_string())
    {
        DialogWalkStep step;
        step.sceneId = sceneId;
        step.objectPointer = objectPointer;
        step.field = DialogWalkStep::Field::LineText;
        step.treeKey = "choice:" + objectPointer;
        step.breadcrumb = crumb;
        step.stepLabel = id.empty() ? "Line" : id;
        step.playerLabel = label;
        step.objectId = id;
        steps.push_back(std::move(step));
    }

    if (choice.contains("choices") && choice["choices"].is_array())
    {
        const nlohmann::json& nested = choice["choices"];
        for (size_t i = 0; i < nested.size(); ++i)
        {
            if (!nested[i].is_object())
                continue;
            appendChoiceSteps(
                nested[i],
                conversationPointerIndex(
                    conversationPointerJoin(objectPointer, "choices"), i),
                crumb,
                sceneId,
                depth + 1);
        }
    }
}

void DialogWalkthrough::rebuildSteps()
{
    const int previousIndex = index;
    std::string previousTreeKey;
    if (index >= 0 && index < static_cast<int>(steps.size()))
        previousTreeKey = steps[static_cast<size_t>(index)].treeKey;

    steps.clear();
    dirtyStep = false;
    error.clear();

    if (docs == nullptr || selectionSceneId == nullptr || selectionSceneId->empty())
    {
        index = 0;
        textBuffer.clear();
        return;
    }
    if (!docs->conversationsLoaded || !docs->conversationsRoot.is_object())
    {
        index = 0;
        return;
    }
    if (!docs->conversationsRoot.contains(*selectionSceneId)
        || !docs->conversationsRoot[*selectionSceneId].is_object())
    {
        index = 0;
        return;
    }

    const nlohmann::json& sceneNode = docs->conversationsRoot[*selectionSceneId];
    const std::string scenePointer = conversationPointerJoin("", *selectionSceneId);
    if (!sceneNode.contains("speakPhases") || !sceneNode["speakPhases"].is_array())
    {
        index = 0;
        return;
    }

    const nlohmann::json& phases = sceneNode["speakPhases"];
    for (size_t phaseIndex = 0; phaseIndex < phases.size(); ++phaseIndex)
    {
        const nlohmann::json& phase = phases[phaseIndex];
        if (!phase.is_object())
            continue;

        const std::string phasePointer = conversationPointerIndex(
            conversationPointerJoin(scenePointer, "speakPhases"), phaseIndex);
        const std::string phaseId =
            phase.contains("id") && phase["id"].is_string() ? phase["id"].get<std::string>()
                                                            : ("phase " + std::to_string(phaseIndex));
        const std::string actor = phaseActorName(phase, phaseActorId(phase));
        const std::string phaseCrumb = actor + " > " + phaseId;

        if (phase.contains("intro") && phase["intro"].is_string())
        {
            DialogWalkStep step;
            step.sceneId = *selectionSceneId;
            step.objectPointer = phasePointer;
            step.field = DialogWalkStep::Field::Intro;
            step.treeKey = "narrative-conv:" + phasePointer + "/intro";
            step.breadcrumb = phaseCrumb + " > Intro";
            step.stepLabel = phaseId + "  |  intro";
            step.objectId = phaseId;
            steps.push_back(std::move(step));
        }
        if (phase.contains("resumeIntro") && phase["resumeIntro"].is_string())
        {
            DialogWalkStep step;
            step.sceneId = *selectionSceneId;
            step.objectPointer = phasePointer;
            step.field = DialogWalkStep::Field::ResumeIntro;
            step.treeKey = "narrative-conv:" + phasePointer + "/resumeIntro";
            step.breadcrumb = phaseCrumb + " > Resume intro";
            step.stepLabel = phaseId + "  |  resume";
            step.objectId = phaseId;
            steps.push_back(std::move(step));
        }
        if (phase.contains("text") && phase["text"].is_string())
        {
            DialogWalkStep step;
            step.sceneId = *selectionSceneId;
            step.objectPointer = phasePointer;
            step.field = DialogWalkStep::Field::LineText;
            step.treeKey = "narrative-conv:" + phasePointer + "/text";
            step.breadcrumb = phaseCrumb + " > Text";
            step.stepLabel = phaseId + "  |  text";
            step.objectId = phaseId;
            steps.push_back(std::move(step));
        }

        if (phase.contains("choices") && phase["choices"].is_array())
        {
            const nlohmann::json& choices = phase["choices"];
            for (size_t i = 0; i < choices.size(); ++i)
            {
                if (!choices[i].is_object())
                    continue;
                appendChoiceSteps(
                    choices[i],
                    conversationPointerIndex(
                        conversationPointerJoin(phasePointer, "choices"), i),
                    phaseCrumb,
                    *selectionSceneId,
                    0);
            }
        }

        if (phase.contains("lines") && phase["lines"].is_array())
        {
            const nlohmann::json& lines = phase["lines"];
            for (size_t i = 0; i < lines.size(); ++i)
            {
                if (!lines[i].is_object())
                    continue;
                appendChoiceSteps(
                    lines[i],
                    conversationPointerIndex(
                        conversationPointerJoin(phasePointer, "lines"), i),
                    phaseCrumb + " > lines",
                    *selectionSceneId,
                    0);
            }
        }
    }

    if (steps.empty())
    {
        index = 0;
        textBuffer.clear();
        return;
    }

    // Restore position when possible.
    if (!previousTreeKey.empty())
    {
        for (size_t i = 0; i < steps.size(); ++i)
        {
            if (steps[i].treeKey == previousTreeKey)
            {
                index = static_cast<int>(i);
                loadCurrentStep();
                return;
            }
        }
    }
    index = std::clamp(previousIndex, 0, static_cast<int>(steps.size()) - 1);
    loadCurrentStep();
}

nlohmann::json* DialogWalkthrough::currentObject()
{
    if (docs == nullptr || index < 0 || index >= static_cast<int>(steps.size()))
        return nullptr;
    return docs->conversationJsonAt(steps[static_cast<size_t>(index)].objectPointer);
}

const nlohmann::json* DialogWalkthrough::currentObject() const
{
    if (docs == nullptr || index < 0 || index >= static_cast<int>(steps.size()))
        return nullptr;
    return docs->conversationJsonAt(steps[static_cast<size_t>(index)].objectPointer);
}

void DialogWalkthrough::loadCurrentStep()
{
    dirtyStep = false;
    error.clear();
    status.clear();
    editTtsText = false;
    voiceMenuOpen = false;
    startPhaseMenuOpen = false;
    flowFocus = FlowFocus::None;
    cursor = 0;
    selectAnchor = -1;
    mouseSelecting = false;
    lastClickTime = -1.0;
    lastClickPos = -1;
    textScroll = 0.0f;

    textBuffer.clear();
    ttsTextBuffer.clear();
    ttsVoice.clear();
    ttsAudio.clear();
    ttsEnabled = false;
    editingChoice = false;
    confirmDeleteChoice = false;
    choiceLabel.clear();
    choiceClosePhase = true;
    choiceStartPhase.clear();
    choiceSkipIntro = false;
    choiceResumeId.clear();
    choiceGrantFlag.clear();
    choiceExitSceneId.clear();
    phaseIdOptions.clear();

    if (index < 0 || index >= static_cast<int>(steps.size()))
        return;

    const DialogWalkStep& step = steps[static_cast<size_t>(index)];
    const nlohmann::json* obj = currentObject();
    if (obj == nullptr || !obj->is_object())
    {
        error = "Missing conversation object for this step";
        return;
    }

    const char* fk = fieldKey(step.field);
    if (obj->contains(fk) && (*obj)[fk].is_string())
        textBuffer = (*obj)[fk].get<std::string>();

    ttsEnabled = obj->value(ttsFlagKey(step.field), false);
    ttsVoice = obj->value(ttsVoiceKey(step.field), "");
    ttsAudio = obj->value(ttsAudioKey(step.field), "");
    if (obj->contains(ttsTextKey(step.field)) && (*obj)[ttsTextKey(step.field)].is_string())
        ttsTextBuffer = (*obj)[ttsTextKey(step.field)].get<std::string>();

    // Scene default voice when unset.
    if (ttsVoice.empty() && docs != nullptr && docs->scenes.isLoaded()
        && docs->scenes.hasScene(step.sceneId))
    {
        const nlohmann::json* scene = docs->scenes.sceneJson(step.sceneId);
        if (scene != nullptr && scene->is_object())
            ttsVoice = scene->value("ttsDefaultVoice", "");
    }
    if (ttsVoice.empty())
        ttsVoice = "leo";
    ttsVoice = normalizeVoiceId(ttsVoice);

    loadChoiceFlowFromObject(*obj);
    if (editingChoice)
        refreshPhaseIdOptions();

    cursor = static_cast<int>(textBuffer.size());
    textFieldFocused = true;

    if (conversationSelectedKey != nullptr)
        *conversationSelectedKey = step.treeKey;

    // Keep list scrolled so current row is visible.
    const float needY = static_cast<float>(index) * kRowH;
    if (needY < listScroll)
        listScroll = needY;
    const float viewH = std::max(40.0f, listPanel.height - 8.0f);
    if (needY + kRowH > listScroll + viewH)
        listScroll = needY + kRowH - viewH;
}

void DialogWalkthrough::ensureDefaultAudioPath()
{
    if (!ttsAudio.empty() || index < 0 || index >= static_cast<int>(steps.size()))
        return;
    const DialogWalkStep& step = steps[static_cast<size_t>(index)];
    std::string leaf = step.objectId.empty() ? "line" : step.objectId;
    if (step.field == DialogWalkStep::Field::Intro)
        leaf = (step.objectId.empty() ? "phase" : step.objectId) + "_intro";
    else if (step.field == DialogWalkStep::Field::ResumeIntro)
        leaf = (step.objectId.empty() ? "phase" : step.objectId) + "_resume";
    // sanitize
    for (char& ch : leaf)
    {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-'))
            ch = '_';
    }
    ttsAudio = "resources/audio/tts/" + step.sceneId + "/" + leaf + ".mp3";
}

bool DialogWalkthrough::applyCurrentStep()
{
    error.clear();
    if (index < 0 || index >= static_cast<int>(steps.size()))
        return false;
    nlohmann::json* obj = currentObject();
    if (obj == nullptr || !obj->is_object())
    {
        error = "Cannot save  -  object missing";
        return false;
    }

    const DialogWalkStep& step = steps[static_cast<size_t>(index)];
    const char* fk = fieldKey(step.field);
    const std::string oldText =
        obj->contains(fk) && (*obj)[fk].is_string() ? (*obj)[fk].get<std::string>() : "";
    (*obj)[fk] = textBuffer;

    if (ttsEnabled)
        ensureDefaultAudioPath();

    (*obj)[ttsFlagKey(step.field)] = ttsEnabled;
    (*obj)[ttsVoiceKey(step.field)] = normalizeVoiceId(ttsVoice);
    if (ttsEnabled)
        (*obj)[ttsAudioKey(step.field)] = ttsAudio;
    // Spoken TTS line: explicit buffer, or fall back to on-screen text.
    const std::string spoken =
        !ttsTextBuffer.empty() ? ttsTextBuffer : textBuffer;
    if (ttsEnabled && !spoken.empty())
        (*obj)[ttsTextKey(step.field)] = spoken;
    else if (!ttsEnabled)
    {
        // Leave ttsText if present; flag off is enough for collectors.
    }

    if (editingChoice)
        applyChoiceFlowToObject(*obj);

    if (oldText != textBuffer || dirtyStep)
        obj->erase(ttsShaKey(step.field));

    dirtyStep = false;
    status = "Saved step " + std::to_string(index + 1) + " / "
        + std::to_string(static_cast<int>(steps.size()));
    if (docs != nullptr)
        docs->markDirty();
    if (onDirty)
        onDirty();
    if (onTreeRebuild)
        onTreeRebuild();
    return true;
}

void DialogWalkthrough::selectIndex(int i)
{
    if (steps.empty())
    {
        index = 0;
        return;
    }
    if (dirtyStep)
        applyCurrentStep();
    index = std::clamp(i, 0, static_cast<int>(steps.size()) - 1);
    loadCurrentStep();
}

void DialogWalkthrough::goPrev()
{
    if (index > 0)
        selectIndex(index - 1);
}

void DialogWalkthrough::goNext()
{
    if (index + 1 < static_cast<int>(steps.size()))
        selectIndex(index + 1);
}

std::vector<std::string> DialogWalkthrough::conversationSceneIds() const
{
    std::vector<std::string> ids;
    if (docs == nullptr || !docs->conversationsLoaded || !docs->conversationsRoot.is_object())
        return ids;
    for (auto it = docs->conversationsRoot.begin(); it != docs->conversationsRoot.end(); ++it)
    {
        if (!it.value().is_object())
            continue;
        const nlohmann::json& node = it.value();
        if (!node.contains("speakPhases") || !node["speakPhases"].is_array()
            || node["speakPhases"].empty())
            continue;
        ids.push_back(it.key());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void DialogWalkthrough::ensureConversationSceneSelected()
{
    if (selectionSceneId == nullptr || docs == nullptr)
        return;
    const std::vector<std::string> ids = conversationSceneIds();
    if (ids.empty())
        return;

    // Keep current scene if it has dialogs.
    if (!selectionSceneId->empty())
    {
        for (const std::string& id : ids)
        {
            if (id == *selectionSceneId)
                return;
        }
    }

    // Prefer a known shop / saloon scene if present; else first with speakPhases.
    const char* preferred[] = {
        "alpine_hardware",
        "ridge_haberdashery",
        "saloon_interior",
        "saloon_balcony",
        "saloon_front",
        "white_baptist_church_vestry",
    };
    for (const char* pref : preferred)
    {
        for (const std::string& id : ids)
        {
            if (id == pref)
            {
                selectConversationScene(id);
                return;
            }
        }
    }
    selectConversationScene(ids.front());
}

bool DialogWalkthrough::selectConversationScene(const std::string& sceneId)
{
    if (selectionSceneId == nullptr || sceneId.empty())
        return false;
    if (*selectionSceneId == sceneId && !steps.empty() && lastBuiltScene == sceneId)
        return true;
    if (dirtyStep)
        applyCurrentStep();
    *selectionSceneId = sceneId;
    lastBuiltScene.clear();
    rebuildSteps();
    lastBuiltScene = sceneId;
    if (onSceneChanged)
        onSceneChanged();
    return true;
}

bool DialogWalkthrough::selectTreeKey(const std::string& treeKey)
{
    if (treeKey.empty())
        return false;

    // Rebuild if the left tree selected a scene before steps were refreshed.
    if (selectionSceneId != nullptr && !selectionSceneId->empty()
        && lastBuiltScene != *selectionSceneId)
    {
        rebuildSteps();
        lastBuiltScene = *selectionSceneId;
    }

    // Exact match only — loose prefix matching was jumping to the wrong line.
    for (size_t i = 0; i < steps.size(); ++i)
    {
        if (steps[i].treeKey == treeKey)
        {
            selectIndex(static_cast<int>(i));
            return true;
        }
    }

    const std::string choicePrefix = "choice:";
    if (treeKey.rfind(choicePrefix, 0) == 0)
    {
        const std::string ptr = treeKey.substr(choicePrefix.size());
        for (size_t i = 0; i < steps.size(); ++i)
        {
            if (steps[i].objectPointer == ptr)
            {
                selectIndex(static_cast<int>(i));
                return true;
            }
        }
        // Parent choice with only nested children: jump to first descendant step.
        for (size_t i = 0; i < steps.size(); ++i)
        {
            if (steps[i].objectPointer.rfind(ptr + "/", 0) == 0)
            {
                selectIndex(static_cast<int>(i));
                return true;
            }
        }
    }

    // Milestone row: phase:/scene/speakPhases/N — jump to first line of that phase.
    const std::string phasePrefix = "phase:";
    if (treeKey.rfind(phasePrefix, 0) == 0)
    {
        const std::string ptr = treeKey.substr(phasePrefix.size());
        for (size_t i = 0; i < steps.size(); ++i)
        {
            if (steps[i].objectPointer == ptr
                || steps[i].objectPointer.rfind(ptr + "/", 0) == 0)
            {
                selectIndex(static_cast<int>(i));
                return true;
            }
        }
    }

    const std::string narrPrefix = "narrative-conv:";
    if (treeKey.rfind(narrPrefix, 0) == 0)
    {
        std::string rest = treeKey.substr(narrPrefix.size());
        const size_t slash = rest.rfind('/');
        if (slash != std::string::npos)
        {
            const std::string ptr = rest.substr(0, slash);
            const std::string leaf = rest.substr(slash + 1);
            DialogWalkStep::Field field = DialogWalkStep::Field::Intro;
            if (leaf == "resumeIntro")
                field = DialogWalkStep::Field::ResumeIntro;
            else if (leaf == "text")
                field = DialogWalkStep::Field::LineText;
            else if (leaf == "response")
                field = DialogWalkStep::Field::Response;
            if (selectObjectField(ptr, field))
                return true;
            // Fallback: any step on this phase object.
            for (size_t i = 0; i < steps.size(); ++i)
            {
                if (steps[i].objectPointer == ptr)
                {
                    selectIndex(static_cast<int>(i));
                    return true;
                }
            }
        }
    }
    return false;
}

bool DialogWalkthrough::selectObjectField(
    const std::string& objectPointer,
    DialogWalkStep::Field field)
{
    for (size_t i = 0; i < steps.size(); ++i)
    {
        if (steps[i].objectPointer == objectPointer && steps[i].field == field)
        {
            selectIndex(static_cast<int>(i));
            return true;
        }
    }
    return false;
}

int DialogWalkthrough::utf8Prev(const std::string& buffer, int at)
{
    if (at <= 0)
        return 0;
    int i = at - 1;
    while (i > 0
           && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        --i;
    return i;
}

int DialogWalkthrough::utf8Next(const std::string& buffer, int at)
{
    const int n = static_cast<int>(buffer.size());
    if (at >= n)
        return n;
    int i = at + 1;
    while (i < n
           && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        ++i;
    return i;
}

bool DialogWalkthrough::hasSelection() const
{
    return selectAnchor >= 0 && selectAnchor != cursor;
}

void DialogWalkthrough::selectionRange(int& outStart, int& outEnd) const
{
    outStart = std::min(selectAnchor, cursor);
    outEnd = std::max(selectAnchor, cursor);
}

void DialogWalkthrough::clearSelection()
{
    selectAnchor = -1;
}

bool DialogWalkthrough::deleteSelection(std::string& buffer)
{
    if (!hasSelection())
        return false;
    int start = 0;
    int end = 0;
    selectionRange(start, end);
    start = std::clamp(start, 0, static_cast<int>(buffer.size()));
    end = std::clamp(end, 0, static_cast<int>(buffer.size()));
    if (end <= start)
    {
        clearSelection();
        return false;
    }
    buffer.erase(static_cast<size_t>(start), static_cast<size_t>(end - start));
    cursor = start;
    clearSelection();
    return true;
}

void DialogWalkthrough::setCursor(int pos, bool extendSelection, int bufferSize)
{
    pos = std::clamp(pos, 0, bufferSize);
    if (extendSelection)
    {
        if (selectAnchor < 0)
            selectAnchor = cursor;
    }
    else
        clearSelection();
    cursor = pos;
}

void DialogWalkthrough::selectWordAt(const std::string& buffer, int pos)
{
    const int n = static_cast<int>(buffer.size());
    if (n <= 0)
    {
        selectAnchor = 0;
        cursor = 0;
        return;
    }
    pos = std::clamp(pos, 0, n);
    auto isWord = [](unsigned char ch) {
        if (ch >= 0x80)
            return true;
        return std::isalnum(ch) != 0 || ch == '_' || ch == '\'';
    };
    int at = pos;
    if (at >= n || !isWord(static_cast<unsigned char>(buffer[static_cast<size_t>(at)])))
    {
        if (at > 0 && isWord(static_cast<unsigned char>(buffer[static_cast<size_t>(at - 1)])))
            --at;
        else
        {
            if (at >= n)
            {
                selectAnchor = n;
                cursor = n;
                return;
            }
            selectAnchor = at;
            cursor = at + 1;
            return;
        }
    }
    int start = at;
    while (start > 0
           && isWord(static_cast<unsigned char>(buffer[static_cast<size_t>(start - 1)])))
        --start;
    int end = at + 1;
    while (end < n && isWord(static_cast<unsigned char>(buffer[static_cast<size_t>(end)])))
        ++end;
    selectAnchor = start;
    cursor = end;
}

void DialogWalkthrough::ensureCaretVisible(
    const std::vector<EditorVisualLine>& lines,
    float lineHeight)
{
    if (lines.empty() || textField.height < 8.0f)
        return;
    const std::string& buf = editTtsText ? ttsTextBuffer : textBuffer;
    const int lineIndex = visualLineIndexForCursor(
        lines, cursor, static_cast<int>(buf.size()));
    const float caretTop = static_cast<float>(lineIndex) * lineHeight;
    const float caretBottom = caretTop + lineHeight;
    const float pad = 8.0f;
    const float viewH = textField.height - pad * 2.0f;
    if (caretTop < textScroll)
        textScroll = caretTop;
    if (caretBottom > textScroll + viewH)
        textScroll = caretBottom - viewH;
    if (textScroll < 0.0f)
        textScroll = 0.0f;
}

bool DialogWalkthrough::currentObjectIsChoice() const
{
    const nlohmann::json* obj = currentObject();
    return obj != nullptr && obj->is_object() && obj->contains("label");
}

void DialogWalkthrough::loadChoiceFlowFromObject(const nlohmann::json& obj)
{
    editingChoice = obj.contains("label");
    if (!editingChoice)
        return;
    choiceLabel = obj.value("label", "");
    choiceClosePhase = obj.value("closePhase", true);
    choiceStartPhase = obj.value("startPhase", "");
    choiceSkipIntro = obj.value("skipIntroOnStartPhase", false);
    choiceResumeId = obj.value("resumeChoiceId", "");
    choiceGrantFlag = obj.value("grantStoryFlag", "");
    choiceExitSceneId = obj.value("exitSceneId", "");
}

void DialogWalkthrough::applyChoiceFlowToObject(nlohmann::json& obj)
{
    if (!editingChoice)
        return;

    if (choiceLabel.empty())
    {
        error = "Player option (label) cannot be empty";
        choiceLabel = obj.value("label", "New option");
    }
    obj["label"] = choiceLabel;
    obj["closePhase"] = choiceClosePhase;

    if (choiceStartPhase.empty())
        obj.erase("startPhase");
    else
        obj["startPhase"] = choiceStartPhase;

    if (choiceSkipIntro && !choiceStartPhase.empty())
        obj["skipIntroOnStartPhase"] = true;
    else
        obj.erase("skipIntroOnStartPhase");

    if (choiceResumeId.empty())
        obj.erase("resumeChoiceId");
    else
        obj["resumeChoiceId"] = choiceResumeId;

    if (choiceGrantFlag.empty())
        obj.erase("grantStoryFlag");
    else
        obj["grantStoryFlag"] = choiceGrantFlag;

    if (choiceExitSceneId.empty())
        obj.erase("exitSceneId");
    else
    {
        obj["exitSceneId"] = choiceExitSceneId;
        // Leave wins over startPhase — clear startPhase when leaving.
        obj.erase("startPhase");
        obj.erase("skipIntroOnStartPhase");
        choiceStartPhase.clear();
        choiceSkipIntro = false;
    }
}

void DialogWalkthrough::refreshPhaseIdOptions()
{
    phaseIdOptions.clear();
    phaseIdOptions.push_back("(none)");
    if (docs == nullptr || selectionSceneId == nullptr || selectionSceneId->empty())
        return;
    if (!docs->conversationsRoot.contains(*selectionSceneId)
        || !docs->conversationsRoot[*selectionSceneId].is_object())
        return;
    const nlohmann::json& sceneNode = docs->conversationsRoot[*selectionSceneId];
    if (!sceneNode.contains("speakPhases") || !sceneNode["speakPhases"].is_array())
        return;
    for (const nlohmann::json& phase : sceneNode["speakPhases"])
    {
        if (!phase.is_object())
            continue;
        const std::string id = phase.value("id", "");
        if (!id.empty())
            phaseIdOptions.push_back(id);
    }
}

void DialogWalkthrough::handleFlowFieldTyping()
{
    if (flowFocus == FlowFocus::None || voiceMenuOpen || startPhaseMenuOpen)
        return;

    std::string* target = nullptr;
    switch (flowFocus)
    {
    case FlowFocus::Label:
        target = &choiceLabel;
        break;
    case FlowFocus::StartPhase:
        target = &choiceStartPhase;
        break;
    case FlowFocus::ResumeId:
        target = &choiceResumeId;
        break;
    case FlowFocus::GrantFlag:
        target = &choiceGrantFlag;
        break;
    case FlowFocus::ExitScene:
        target = &choiceExitSceneId;
        break;
    default:
        break;
    }
    if (target == nullptr)
        return;

    // Drain main-text path: flow fields own typing while focused.
    textFieldFocused = false;

    if (IsKeyPressed(KEY_BACKSPACE) && !target->empty())
    {
        // UTF-8 safe enough for ids/labels: erase last code unit cluster.
        size_t i = target->size();
        do
        {
            --i;
        } while (i > 0
                 && (static_cast<unsigned char>((*target)[i]) & 0xC0) == 0x80);
        target->erase(i);
        dirtyStep = true;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE))
    {
        flowFocus = FlowFocus::None;
        textFieldFocused = true;
        return;
    }

    for (;;)
    {
        const int ch = GetCharPressed();
        if (ch <= 0)
            break;
        if (ch < 32)
            continue;
        // Encode codepoint as UTF-8.
        char bytes[5] = {};
        int n = 0;
        if (ch < 0x80)
            bytes[n++] = static_cast<char>(ch);
        else if (ch < 0x800)
        {
            bytes[n++] = static_cast<char>(0xC0 | (ch >> 6));
            bytes[n++] = static_cast<char>(0x80 | (ch & 0x3F));
        }
        else if (ch < 0x10000)
        {
            bytes[n++] = static_cast<char>(0xE0 | (ch >> 12));
            bytes[n++] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            bytes[n++] = static_cast<char>(0x80 | (ch & 0x3F));
        }
        else
        {
            bytes[n++] = static_cast<char>(0xF0 | (ch >> 18));
            bytes[n++] = static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
            bytes[n++] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            bytes[n++] = static_cast<char>(0x80 | (ch & 0x3F));
        }
        target->append(bytes, static_cast<size_t>(n));
        dirtyStep = true;
    }
}

bool DialogWalkthrough::handleStartPhaseMenuClick(Vector2 mouse)
{
    if (!startPhaseMenuOpen)
        return false;
    if (startPhaseMenuRect.width < 1.0f)
        return false;

    if (CheckCollisionPointRec(mouse, startPhaseMenuRect))
    {
        const float rowH = 22.0f;
        const int first =
            static_cast<int>(std::floor(startPhaseMenuScroll + 0.001f));
        int i = first
            + static_cast<int>((mouse.y - startPhaseMenuRect.y - 2.0f) / rowH);
        if (i >= 0 && i < static_cast<int>(phaseIdOptions.size()))
        {
            if (i == 0)
                choiceStartPhase.clear();
            else
                choiceStartPhase = phaseIdOptions[static_cast<size_t>(i)];
            dirtyStep = true;
        }
        startPhaseMenuOpen = false;
        ignoreInputFrames = 1;
        return true;
    }

    if (CheckCollisionPointRec(mouse, startPhaseBtnRect))
        return false;
    startPhaseMenuOpen = false;
    ignoreInputFrames = 1;
    return true;
}

void DialogWalkthrough::drawStartPhaseMenu(Font font)
{
    if (!startPhaseMenuOpen)
    {
        startPhaseMenuRect = {0, 0, 0, 0};
        return;
    }
    const int count = static_cast<int>(phaseIdOptions.size());
    const float rowH = 22.0f;
    const int visible = std::min(kStartPhaseMenuVisibleRows, std::max(1, count));
    const float menuH = static_cast<float>(visible) * rowH + 4.0f;
    const float maxScroll =
        static_cast<float>(std::max(0, count - visible));
    startPhaseMenuScroll = std::clamp(startPhaseMenuScroll, 0.0f, maxScroll);
    startPhaseMenuRect = {
        startPhaseBtnRect.x,
        startPhaseBtnRect.y + startPhaseBtnRect.height + 2.0f,
        std::max(startPhaseBtnRect.width, 160.0f),
        menuH};
    if (startPhaseMenuRect.y + startPhaseMenuRect.height
        > lastPane.y + lastPane.height - 4.0f)
        startPhaseMenuRect.y =
            startPhaseBtnRect.y - startPhaseMenuRect.height - 2.0f;

    DrawRectangleRec(startPhaseMenuRect, Color{36, 32, 44, 255});
    DrawRectangleLinesEx(startPhaseMenuRect, 1.0f, kPanelBorder);
    const Vector2 mouse = GetMousePosition();
    const int first = static_cast<int>(std::floor(startPhaseMenuScroll + 0.001f));
    float my = startPhaseMenuRect.y + 2.0f;
    for (int row = 0; row < visible; ++row)
    {
        const int i = first + row;
        if (i < 0 || i >= count)
            break;
        const std::string& opt = phaseIdOptions[static_cast<size_t>(i)];
        const bool selected =
            (i == 0 && choiceStartPhase.empty())
            || (i > 0 && opt == choiceStartPhase);
        const Rectangle r = {
            startPhaseMenuRect.x + 2.0f,
            my,
            startPhaseMenuRect.width - 4.0f,
            rowH - 2.0f};
        if (selected)
            DrawRectangleRec(r, kSelection);
        else if (CheckCollisionPointRec(mouse, r))
            DrawRectangleRec(r, Color{60, 54, 72, 220});
        DrawTextEx(
            font, opt.c_str(), {r.x + 8.0f, r.y + 3.0f}, kFontSmall, 1.0f, kTextPrimary);
        my += rowH;
    }
}

bool DialogWalkthrough::currentChoiceArrayLocation(
    std::string& arrayPointerOut,
    size_t& indexOut) const
{
    if (index < 0 || index >= static_cast<int>(steps.size()))
        return false;
    const std::string& ptr = steps[static_cast<size_t>(index)].objectPointer;
    // Expect …/choices/<n>
    const std::string marker = "/choices/";
    const size_t pos = ptr.rfind(marker);
    if (pos == std::string::npos)
        return false;
    arrayPointerOut = ptr.substr(0, pos + std::string("/choices").size());
    try
    {
        indexOut = static_cast<size_t>(std::stoul(ptr.substr(pos + marker.size())));
    }
    catch (...)
    {
        return false;
    }
    return true;
}

bool DialogWalkthrough::currentPhasePointer(std::string& phasePointerOut) const
{
    if (index < 0 || index >= static_cast<int>(steps.size()))
        return false;
    const DialogWalkStep& step = steps[static_cast<size_t>(index)];
    if (step.field != DialogWalkStep::Field::Intro
        && step.field != DialogWalkStep::Field::ResumeIntro
        && step.field != DialogWalkStep::Field::LineText)
        return false;
    // Phase narrative steps point at the phase object (not a choice).
    if (step.objectPointer.find("/choices/") != std::string::npos)
        return false;
    phasePointerOut = step.objectPointer;
    return !phasePointerOut.empty();
}

std::string DialogWalkthrough::allocateChoiceId() const
{
    std::set<std::string> used;
    if (docs != nullptr && selectionSceneId != nullptr && !selectionSceneId->empty()
        && docs->conversationsRoot.contains(*selectionSceneId))
    {
        const nlohmann::json& sceneNode = docs->conversationsRoot[*selectionSceneId];
        std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& node) {
            if (node.is_object())
            {
                if (node.contains("id") && node["id"].is_string()
                    && node.contains("label"))
                    used.insert(node["id"].get<std::string>());
                for (auto it = node.begin(); it != node.end(); ++it)
                    walk(it.value());
            }
            else if (node.is_array())
            {
                for (const auto& child : node)
                    walk(child);
            }
        };
        walk(sceneNode);
    }
    for (int n = 1; n < 10000; ++n)
    {
        const std::string id = "choice_" + std::to_string(n);
        if (used.count(id) == 0)
            return id;
    }
    return "choice_new";
}

bool DialogWalkthrough::addChoiceNearCurrent()
{
    if (docs == nullptr || !docs->conversationsLoaded)
        return false;
    if (dirtyStep)
        applyCurrentStep();

    nlohmann::json* array = nullptr;
    std::string arrayPointer;
    size_t unusedIndex = 0;
    std::string phasePointer;
    if (currentChoiceArrayLocation(arrayPointer, unusedIndex))
    {
        array = docs->conversationJsonAt(arrayPointer);
    }
    else if (currentPhasePointer(phasePointer))
    {
        nlohmann::json* phase = docs->conversationJsonAt(phasePointer);
        if (phase == nullptr || !phase->is_object())
            return false;
        if (!phase->contains("choices") || !(*phase)["choices"].is_array())
            (*phase)["choices"] = nlohmann::json::array();
        arrayPointer = conversationPointerJoin(phasePointer, "choices");
        array = &(*phase)["choices"];
    }
    else
    {
        error = "Select a phase line or choice to add a speech option";
        return false;
    }

    if (array == nullptr || !array->is_array())
    {
        // Fallback via pointer lookup after ensuring choices exists.
        array = docs->conversationJsonAt(arrayPointer);
    }
    if (array == nullptr || !array->is_array())
    {
        error = "Could not find choices array";
        return false;
    }

    const std::string newId = allocateChoiceId();
    nlohmann::json neu = nlohmann::json::object();
    neu["id"] = newId;
    neu["label"] = "New option";
    neu["response"] = "";
    neu["closePhase"] = true;
    array->push_back(std::move(neu));
    const size_t newIndex = array->size() - 1;
    const std::string newPointer = conversationPointerIndex(arrayPointer, newIndex);

    docs->markDirty();
    if (onDirty)
        onDirty();
    rebuildSteps();
    // Select the new choice response step.
    for (size_t i = 0; i < steps.size(); ++i)
    {
        if (steps[i].objectPointer == newPointer)
        {
            selectIndex(static_cast<int>(i));
            break;
        }
    }
    if (onTreeRebuild)
        onTreeRebuild();
    status = "Added speech option " + newId;
    confirmDeleteChoice = false;
    return true;
}

bool DialogWalkthrough::deleteCurrentChoice()
{
    if (!editingChoice || docs == nullptr)
        return false;
    std::string arrayPointer;
    size_t choiceIndex = 0;
    if (!currentChoiceArrayLocation(arrayPointer, choiceIndex))
    {
        error = "Not a choice step";
        return false;
    }
    nlohmann::json* array = docs->conversationJsonAt(arrayPointer);
    if (array == nullptr || !array->is_array())
    {
        error = "Choices array missing";
        return false;
    }
    if (choiceIndex >= array->size())
    {
        error = "Choice index out of range";
        return false;
    }

    const std::string removedId =
        (*array)[choiceIndex].value("id", std::string("choice"));
    array->erase(array->begin() + static_cast<std::ptrdiff_t>(choiceIndex));
    docs->markDirty();
    if (onDirty)
        onDirty();
    rebuildSteps();
    if (!steps.empty())
        selectIndex(std::min(index, static_cast<int>(steps.size()) - 1));
    else
    {
        index = 0;
        loadCurrentStep();
    }
    if (onTreeRebuild)
        onTreeRebuild();
    status = "Deleted speech option " + removedId;
    confirmDeleteChoice = false;
    return true;
}

float DialogWalkthrough::drawChoiceFlowPanel(
    Font font,
    Font bold,
    Rectangle editor,
    float startY,
    bool canClick,
    Vector2 mouse)
{
    if (!editingChoice)
        return 0.0f;

    float y = startY;
    const float x = editor.x + 10.0f;
    const float w = editor.width - 20.0f;

    DrawTextEx(bold, "Speech option / flow", {x, y}, kFontSmall, 1.0f, kTextPrimary);
    y += 18.0f;

    auto drawField = [&](const char* label,
                         const std::string& value,
                         FlowFocus focus,
                         float fieldW) -> Rectangle {
        DrawTextEx(font, label, {x, y}, kFontTiny, 1.0f, kTextMuted);
        y += 14.0f;
        const Rectangle box = {x, y, fieldW, 26.0f};
        const bool focused = flowFocus == focus;
        DrawRectangleRec(box, Color{22, 20, 28, 255});
        DrawRectangleLinesEx(
            box, 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
        const std::string shown =
            value.empty() ? std::string("(click to type)") : value;
        DrawTextEx(
            font,
            truncateOneLine(shown, 64).c_str(),
            {box.x + 8.0f, box.y + 5.0f},
            kFontSmall,
            1.0f,
            value.empty() ? kTextMuted : kTextPrimary);
        if (canClick && CheckCollisionPointRec(mouse, box))
        {
            flowFocus = focus;
            textFieldFocused = false;
            startPhaseMenuOpen = false;
            voiceMenuOpen = false;
        }
        y += 30.0f;
        return box;
    };

    drawField("Player option (label)", choiceLabel, FlowFocus::Label, w);

    // Toggles row
    const Rectangle closeBtn = {x, y, 150.0f, 26.0f};
    const Rectangle skipBtn = {x + 158.0f, y, 170.0f, 26.0f};
    drawEditorButton(
        font,
        closeBtn,
        choiceClosePhase ? "Close phase: ON" : "Close phase: off",
        choiceClosePhase,
        true);
    drawEditorButton(
        font,
        skipBtn,
        choiceSkipIntro ? "Skip intro: ON" : "Skip intro: off",
        choiceSkipIntro,
        !choiceStartPhase.empty());
    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, closeBtn))
        {
            choiceClosePhase = !choiceClosePhase;
            dirtyStep = true;
            flowFocus = FlowFocus::None;
        }
        else if (
            !choiceStartPhase.empty() && CheckCollisionPointRec(mouse, skipBtn))
        {
            choiceSkipIntro = !choiceSkipIntro;
            dirtyStep = true;
            flowFocus = FlowFocus::None;
        }
    }
    y += 32.0f;

    DrawTextEx(font, "Start phase", {x, y}, kFontTiny, 1.0f, kTextMuted);
    y += 14.0f;
    startPhaseBtnRect = {x, y, std::min(220.0f, w * 0.45f), 26.0f};
    const std::string startLabel =
        choiceStartPhase.empty() ? "(none)" : choiceStartPhase;
    drawEditorButton(
        font,
        startPhaseBtnRect,
        (startLabel + (startPhaseMenuOpen ? "  ^" : "  v")).c_str(),
        !choiceStartPhase.empty(),
        choiceExitSceneId.empty());
    if (canClick && choiceExitSceneId.empty()
        && CheckCollisionPointRec(mouse, startPhaseBtnRect))
    {
        startPhaseMenuOpen = !startPhaseMenuOpen;
        flowFocus = FlowFocus::None;
        textFieldFocused = false;
        if (startPhaseMenuOpen)
            refreshPhaseIdOptions();
    }
    y += 30.0f;

    drawField("Resume choice id", choiceResumeId, FlowFocus::ResumeId, w * 0.55f);
    drawField("Grant story flag", choiceGrantFlag, FlowFocus::GrantFlag, w * 0.55f);
    drawField(
        "Leave to scene (exitSceneId)  —  MOVE-like; clears startPhase",
        choiceExitSceneId,
        FlowFocus::ExitScene,
        w);

    DrawTextEx(
        font,
        "Reply text below is the NPC/narrator response after this option.",
        {x, y},
        kFontTiny,
        1.0f,
        kTextMuted);
    y += 16.0f;

    return y - startY;
}

void DialogWalkthrough::handleTextTyping()
{
    if (flowFocus != FlowFocus::None)
        return;
    if (!textFieldFocused || voiceMenuOpen)
        return;

    std::string& buf = editTtsText ? ttsTextBuffer : textBuffer;
    cursor = std::clamp(cursor, 0, static_cast<int>(buf.size()));
    const int bufSize = static_cast<int>(buf.size());
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

    // Copy / cut / paste / select-all (match VariableEditor).
    if (ctrl && IsKeyPressed(KEY_A))
    {
        selectAnchor = 0;
        cursor = bufSize;
        preferredCaretX = -1.0f;
        while (GetCharPressed() > 0)
        {
        }
        return;
    }
    if (ctrl && IsKeyPressed(KEY_C) && hasSelection())
    {
        int start = 0;
        int end = 0;
        selectionRange(start, end);
        SetClipboardText(
            buf.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)).c_str());
        while (GetCharPressed() > 0)
        {
        }
        return;
    }
    if (ctrl && IsKeyPressed(KEY_X) && hasSelection())
    {
        int start = 0;
        int end = 0;
        selectionRange(start, end);
        SetClipboardText(
            buf.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)).c_str());
        deleteSelection(buf);
        dirtyStep = true;
        preferredCaretX = -1.0f;
        while (GetCharPressed() > 0)
        {
        }
        return;
    }
    if (ctrl && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            deleteSelection(buf);
            cursor = std::clamp(cursor, 0, static_cast<int>(buf.size()));
            const std::string paste(clip);
            buf.insert(static_cast<size_t>(cursor), paste);
            cursor += static_cast<int>(paste.size());
            clearSelection();
            dirtyStep = true;
            preferredCaretX = -1.0f;
        }
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    int codepoint = GetCharPressed();
    while (codepoint > 0)
    {
        if (codepoint >= 32 && codepoint != 127)
        {
            deleteSelection(buf);
            insertUtf8(buf, cursor, codepoint);
            clearSelection();
            dirtyStep = true;
            preferredCaretX = -1.0f;
        }
        codepoint = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
    {
        if (hasSelection())
            deleteSelection(buf);
        else
            backspaceUtf8(buf, cursor);
        dirtyStep = true;
        preferredCaretX = -1.0f;
    }
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE))
    {
        if (hasSelection())
            deleteSelection(buf);
        else if (cursor < static_cast<int>(buf.size()))
        {
            const int next = utf8Next(buf, cursor);
            buf.erase(static_cast<size_t>(cursor), static_cast<size_t>(next - cursor));
        }
        dirtyStep = true;
        preferredCaretX = -1.0f;
    }
    // Enter inserts newline (replaces selection).
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        deleteSelection(buf);
        insertUtf8(buf, cursor, '\n');
        clearSelection();
        dirtyStep = true;
        preferredCaretX = -1.0f;
    }
    if ((IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) && !alt)
    {
        setCursor(utf8Prev(buf, cursor), shift, static_cast<int>(buf.size()));
        preferredCaretX = -1.0f;
    }
    if ((IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) && !alt)
    {
        setCursor(utf8Next(buf, cursor), shift, static_cast<int>(buf.size()));
        preferredCaretX = -1.0f;
    }
    if ((IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) && !alt
        && textField.width > 8.0f)
    {
        if (shift && selectAnchor < 0)
            selectAnchor = cursor;
        const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
        const float fontSize = kFontEdit;
        const auto lines = layoutWrappedTextLines(
            font, buf, textField.width - 16.0f, fontSize);
        cursor = moveCursorVertical(
            font, lines, buf, cursor, -1, fontSize, preferredCaretX);
        if (!shift)
            clearSelection();
    }
    if ((IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) && !alt
        && textField.width > 8.0f)
    {
        if (shift && selectAnchor < 0)
            selectAnchor = cursor;
        const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
        const float fontSize = kFontEdit;
        const auto lines = layoutWrappedTextLines(
            font, buf, textField.width - 16.0f, fontSize);
        cursor = moveCursorVertical(
            font, lines, buf, cursor, +1, fontSize, preferredCaretX);
        if (!shift)
            clearSelection();
    }
    if (IsKeyPressed(KEY_HOME))
    {
        setCursor(0, shift, static_cast<int>(buf.size()));
        preferredCaretX = -1.0f;
    }
    if (IsKeyPressed(KEY_END))
    {
        setCursor(static_cast<int>(buf.size()), shift, static_cast<int>(buf.size()));
        preferredCaretX = -1.0f;
    }
}

bool DialogWalkthrough::handleVoiceMenuClick(Vector2 mouse)
{
    if (!voiceMenuOpen)
        return false;
    if (voiceMenuRect.width < 1.0f)
        return false;

    if (CheckCollisionPointRec(mouse, voiceMenuRect))
    {
        const std::vector<std::string>& voices = builtinVoiceIds();
        const float rowH = 22.0f;
        const int first =
            static_cast<int>(std::floor(voiceMenuScroll + 0.001f));
        int i = first
            + static_cast<int>((mouse.y - voiceMenuRect.y - 2.0f) / rowH);
        if (i >= 0 && i < static_cast<int>(voices.size()))
        {
            ttsVoice = normalizeVoiceId(voices[static_cast<size_t>(i)]);
            dirtyStep = true;
            if (docs != nullptr && !docs->resourceDir.empty())
                rememberTtsDefaultVoice(docs->resourceDir, ttsVoice);
        }
        voiceMenuOpen = false;
        ignoreInputFrames = 1;
        return true; // consume click
    }

    // Click outside menu closes it and still consumes so we don't hit buttons under it.
    if (CheckCollisionPointRec(mouse, voiceBtnRect))
        return false; // let voice button toggle handle it
    voiceMenuOpen = false;
    ignoreInputFrames = 1;
    return true;
}

void DialogWalkthrough::drawVoiceMenu(Font font)
{
    if (!voiceMenuOpen)
    {
        voiceMenuRect = {0, 0, 0, 0};
        return;
    }
    const std::vector<std::string>& voices = builtinVoiceIds();
    const int voiceCount = static_cast<int>(voices.size());
    const float rowH = 22.0f;
    const int visible = std::min(kVoiceMenuVisibleRows, voiceCount);
    const float menuH = static_cast<float>(visible) * rowH + 4.0f;
    const float maxScroll = static_cast<float>(std::max(0, voiceCount - visible));
    voiceMenuScroll = std::clamp(voiceMenuScroll, 0.0f, maxScroll);
    voiceMenuRect = {
        voiceBtnRect.x,
        voiceBtnRect.y + voiceBtnRect.height + 2.0f,
        std::max(voiceBtnRect.width, 140.0f),
        menuH};
    // Keep menu on-screen inside lastPane.
    if (voiceMenuRect.y + voiceMenuRect.height > lastPane.y + lastPane.height - 4.0f)
        voiceMenuRect.y = voiceBtnRect.y - voiceMenuRect.height - 2.0f;

    DrawRectangleRec(voiceMenuRect, Color{36, 32, 44, 255});
    DrawRectangleLinesEx(voiceMenuRect, 1.0f, kPanelBorder);
    const Vector2 mouse = GetMousePosition();
    const int first = static_cast<int>(std::floor(voiceMenuScroll + 0.001f));
    float my = voiceMenuRect.y + 2.0f;
    for (int row = 0; row < visible; ++row)
    {
        const int i = first + row;
        if (i < 0 || i >= voiceCount)
            break;
        const std::string& v = voices[static_cast<size_t>(i)];
        const Rectangle r = {
            voiceMenuRect.x + 2.0f, my, voiceMenuRect.width - 4.0f, rowH - 2.0f};
        const bool hov = CheckCollisionPointRec(mouse, r);
        const bool selected = (v == ttsVoice);
        if (selected)
            DrawRectangleRec(r, kSelection);
        else if (hov)
            DrawRectangleRec(r, Color{60, 54, 72, 220});
        DrawTextEx(
            font,
            v.c_str(),
            {r.x + 8.0f, r.y + 3.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);
        my += rowH;
    }
}

void DialogWalkthrough::handleInput(Rectangle pane)
{
    lastPane = pane;
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }

    if (docs == nullptr)
        return;

    ensureConversationSceneSelected();

    const std::string scene =
        selectionSceneId != nullptr ? *selectionSceneId : std::string();
    if (scene != lastBuiltScene || (steps.empty() && !scene.empty() && docs->conversationsLoaded))
    {
        lastBuiltScene = scene;
        rebuildSteps();
    }

    const Vector2 mouse = GetMousePosition();
    const bool canClick = editorMousePressed(MOUSE_BUTTON_LEFT);

    // Voice / start-phase menus are modal for clicks — handle first.
    if (canClick && voiceMenuOpen && handleVoiceMenuClick(mouse))
        return;
    if (canClick && startPhaseMenuOpen && handleStartPhaseMenuClick(mouse))
        return;

    if (parchment != nullptr && editorMousePressed(MOUSE_BUTTON_RIGHT)
        && textField.width > 1.0f && CheckCollisionPointRec(mouse, textField))
    {
        fieldContextOpen = true;
        fieldContextRect = {mouse.x, mouse.y, 160.0f, 28.0f};
        if (fieldContextRect.x + fieldContextRect.width > GetScreenWidth())
            fieldContextRect.x = GetScreenWidth() - fieldContextRect.width - 4.0f;
        if (fieldContextRect.y + fieldContextRect.height > GetScreenHeight())
            fieldContextRect.y = GetScreenHeight() - fieldContextRect.height - 4.0f;
    }
    if (fieldContextOpen && editorMousePressed(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(mouse, fieldContextRect) && parchment != nullptr
            && docs != nullptr)
        {
            std::string* target = editTtsText ? &ttsTextBuffer : &textBuffer;
            if (editTtsText && ttsAudio.empty())
                ensureDefaultAudioPath();
            parchment->openEditor(
                target,
                editTtsText,
                editTtsText ? "TTS dialog" : "Dialog text",
                docs->resourceDir,
                docs->assetRoot,
                editTtsText ? textBuffer : std::string{},
                editTtsText ? ttsVoice : std::string{},
                editTtsText ? ttsAudio : std::string{},
                editTtsText ? &ttsAudio : nullptr);
            parchment->onClosed = [this]() {
                dirtyStep = true;
                cursor = static_cast<int>(
                    (editTtsText ? ttsTextBuffer : textBuffer).size());
                ignoreInputFrames = 1;
            };
            fieldContextOpen = false;
            ignoreInputFrames = 1;
            return; // keep walkthrough state; don't fall through to other clicks
        }
        fieldContextOpen = false;
    }

    if (steps.empty())
        return;

    if (IsKeyPressed(KEY_LEFT) && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)))
        goPrev();
    if (IsKeyPressed(KEY_RIGHT) && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)))
        goNext();
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
         || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
        && IsKeyPressed(KEY_S))
    {
        applyCurrentStep();
        if (docs != nullptr)
            docs->saveConversationsDocument();
    }

    if (startPhaseMenuOpen
        && startPhaseMenuRect.height > 1.0f
        && CheckCollisionPointRec(mouse, startPhaseMenuRect))
    {
        const int count = static_cast<int>(phaseIdOptions.size());
        const int visible =
            std::min(kStartPhaseMenuVisibleRows, std::max(1, count));
        const float maxScroll =
            static_cast<float>(std::max(0, count - visible));
        startPhaseMenuScroll = std::clamp(
            startPhaseMenuScroll - GetMouseWheelMove(), 0.0f, maxScroll);
    }
    else if (voiceMenuOpen
        && voiceMenuRect.height > 1.0f
        && CheckCollisionPointRec(mouse, voiceMenuRect))
    {
        const int voiceCount = static_cast<int>(builtinVoiceIds().size());
        const int visible = std::min(kVoiceMenuVisibleRows, voiceCount);
        const float maxScroll =
            static_cast<float>(std::max(0, voiceCount - visible));
        voiceMenuScroll = std::clamp(
            voiceMenuScroll - GetMouseWheelMove(), 0.0f, maxScroll);
    }
    else if (CheckCollisionPointRec(mouse, listPanel))
    {
        listScroll -= GetMouseWheelMove() * kRowH * 2.0f;
        if (listScroll < 0.0f)
            listScroll = 0.0f;
    }
    else if (CheckCollisionPointRec(mouse, textField))
    {
        textScroll -= GetMouseWheelMove() * kLineH * 2.0f;
        if (textScroll < 0.0f)
            textScroll = 0.0f;
    }

    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, textField))
        {
            textFieldFocused = true;
            flowFocus = FlowFocus::None;
            startPhaseMenuOpen = false;
            const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
            const std::string& buf = editTtsText ? ttsTextBuffer : textBuffer;
            const float pad = 8.0f;
            const float fontSize = kFontEdit;
            const float lineHeight = fontSize + 4.0f;
            const auto lines = layoutWrappedTextLines(
                font, buf, textField.width - pad * 2.0f, fontSize);
            const int pos = cursorIndexFromClick(
                font,
                lines,
                buf,
                textField,
                pad,
                fontSize,
                lineHeight,
                textScroll,
                mouse);
            const double now = GetTime();
            const bool isDoubleClick = !shift && lastClickTime >= 0.0
                && (now - lastClickTime) <= 0.4
                && std::abs(pos - lastClickPos) <= 2;
            if (isDoubleClick)
            {
                selectWordAt(buf, pos);
                mouseSelecting = false;
                lastClickTime = -1.0;
                lastClickPos = -1;
            }
            else
            {
                setCursor(pos, shift, static_cast<int>(buf.size()));
                mouseSelecting = !shift;
                if (!shift)
                    selectAnchor = cursor;
                lastClickTime = now;
                lastClickPos = pos;
            }
            preferredCaretX = -1.0f;
        }
        else if (!CheckCollisionPointRec(mouse, voiceBtnRect) && !voiceMenuOpen)
        {
            textFieldFocused = false;
            mouseSelecting = false;
        }
    }
    else if (mouseSelecting && editorMouseDown(MOUSE_BUTTON_LEFT)
             && CheckCollisionPointRec(mouse, textField))
    {
        const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
        const std::string& buf = editTtsText ? ttsTextBuffer : textBuffer;
        const float pad = 8.0f;
        const float fontSize = kFontEdit;
        const float lineHeight = fontSize + 4.0f;
        const auto lines = layoutWrappedTextLines(
            font, buf, textField.width - pad * 2.0f, fontSize);
        if (selectAnchor < 0)
            selectAnchor = cursor;
        cursor = cursorIndexFromClick(
            font,
            lines,
            buf,
            textField,
            pad,
            fontSize,
            lineHeight,
            textScroll,
            mouse);
        cursor = std::clamp(cursor, 0, static_cast<int>(buf.size()));
        preferredCaretX = -1.0f;
    }
    if (editorMouseReleased(MOUSE_BUTTON_LEFT))
        mouseSelecting = false;

    handleFlowFieldTyping();
    handleTextTyping();
}

void DialogWalkthrough::draw(Rectangle pane)
{
    lastPane = pane;
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    // Clicks on the voice menu are handled in handleInput (before draw) so they
    // never fall through to buttons underneath.
    const bool canClick =
        ignoreInputFrames <= 0 && !voiceMenuOpen && !startPhaseMenuOpen
        && editorMousePressed(MOUSE_BUTTON_LEFT);

    DrawRectangleRec(pane, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(pane, 1.0f, kPanelInnerEdge);

    // Scene is chosen from the left tree (expand a scene root). Title bar shows which.
    const float sceneBarH = 28.0f;
    const Rectangle sceneBar = {pane.x + 8.0f, pane.y + 8.0f, pane.width - 16.0f, sceneBarH};
    DrawRectangleRec(sceneBar, Color{28, 26, 36, 255});
    DrawRectangleLinesEx(sceneBar, 1.0f, kPanelInnerEdge);
    const std::string sceneTitle =
        (selectionSceneId == nullptr || selectionSceneId->empty())
        ? "Dialog editor   |   expand a scene in the left tree"
        : ("Dialog editor   |   " + *selectionSceneId);
    DrawTextEx(
        bold,
        sceneTitle.c_str(),
        {sceneBar.x + 10.0f, sceneBar.y + 6.0f},
        kFontSmall,
        1.0f,
        kPanelBorder);

    if (selectionSceneId == nullptr || selectionSceneId->empty())
    {
        DrawTextEx(
            font,
            "Left tree: [+/-] expands all scenes. Click a scene name to load its dialog steps here.",
            {pane.x + 16.0f, sceneBar.y + sceneBarH + 16.0f},
            kFontBody,
            1.0f,
            kTextMuted);
        return;
    }

    if (steps.empty())
    {
        DrawTextEx(
            font,
            ("No dialog lines in \"" + *selectionSceneId
             + "\". This scene has no speakPhases entries.")
                .c_str(),
            {pane.x + 16.0f, sceneBar.y + sceneBarH + 16.0f},
            kFontBody,
            1.0f,
            kTextMuted);
        return;
    }

    // --- Nav toolbar ---
    const Rectangle toolbar = {
        pane.x + 8.0f, sceneBar.y + sceneBarH + 6.0f, pane.width - 16.0f, kToolbarH};
    DrawRectangleRec(toolbar, Color{32, 30, 40, 255});
    DrawRectangleLinesEx(toolbar, 1.0f, kPanelInnerEdge);

    const float btnH = 28.0f;
    const float by = toolbar.y + (toolbar.height - btnH) * 0.5f;
    float bx = toolbar.x + 8.0f;

    const Rectangle prevBtn = {bx, by, 72.0f, btnH};
    bx += 80.0f;
    const Rectangle nextBtn = {bx, by, 72.0f, btnH};
    bx += 80.0f;
    const Rectangle saveBtn = {bx, by, 84.0f, btnH};
    bx += 100.0f;

    // Text / TTS side switch (same slider pattern as VariableEditor).
    const float sideTrackW = 56.0f;
    const float sideTrackH = 22.0f;
    const Rectangle sideTrack = {
        bx,
        by + (btnH - sideTrackH) * 0.5f,
        sideTrackW,
        sideTrackH};
    const Rectangle sideHit = {bx, by, sideTrackW + 44.0f, btnH};
    bx += sideTrackW + 52.0f;

    // TTS enabled for this line + voice
    const Rectangle ttsToggle = {bx, by, 110.0f, btnH};
    bx += 118.0f;
    voiceBtnRect = {bx, by, 130.0f, btnH};
    bx += 138.0f;

    const bool canAddChoice = editingChoice || [&]() {
        std::string phasePtr;
        return currentPhasePointer(phasePtr);
    }();
    const Rectangle addChoiceBtn = {bx, by, 108.0f, btnH};
    bx += 116.0f;
    const Rectangle delChoiceBtn = {bx, by, confirmDeleteChoice ? 120.0f : 108.0f, btnH};

    // ASCII-only labels: UI fonts often lack ◀/▶ and draw them as '?'.
    drawEditorButton(font, prevBtn, "Prev", false, index > 0);
    drawEditorButton(font, nextBtn, "Next", false, index + 1 < static_cast<int>(steps.size()));
    drawEditorButton(font, saveBtn, dirtyStep ? "Save *" : "Save", true, true);
    drawEditorButton(font, addChoiceBtn, "+ Option", false, canAddChoice);
    drawEditorButton(
        font,
        delChoiceBtn,
        confirmDeleteChoice ? "Confirm del" : "Del option",
        confirmDeleteChoice,
        editingChoice);

    DrawRectangleRounded(sideTrack, 0.5f, 6, Color{44, 42, 52, 255});
    DrawRectangleLinesEx(sideTrack, 1.0f, kPanelBorder);
    if (editTtsText)
    {
        DrawRectangleRec(
            {sideTrack.x + sideTrack.width * 0.5f, sideTrack.y + 1.0f,
             sideTrack.width * 0.5f - 1.0f, sideTrack.height - 2.0f},
            kPanelAccent);
    }
    else
    {
        DrawRectangleRec(
            {sideTrack.x + 1.0f, sideTrack.y + 1.0f,
             sideTrack.width * 0.5f - 1.0f, sideTrack.height - 2.0f},
            kPanelAccent);
    }
    {
        const float knobSize = sideTrackH - 6.0f;
        const float knobX = editTtsText
            ? (sideTrack.x + sideTrack.width - knobSize - 3.0f)
            : (sideTrack.x + 3.0f);
        DrawRectangleRounded(
            {knobX, sideTrack.y + 3.0f, knobSize, knobSize},
            0.5f,
            6,
            kTextPrimary);
    }
    DrawTextEx(
        font,
        editTtsText ? "TTS" : "text",
        {sideTrack.x + sideTrack.width + 8.0f,
         sideTrack.y + (sideTrack.height - kFontTiny) * 0.5f},
        kFontTiny,
        1.0f,
        kPanelBorder);

    drawEditorButton(
        font, ttsToggle, ttsEnabled ? "Speech: ON" : "Speech: off", ttsEnabled, true);
    drawEditorButton(
        font,
        voiceBtnRect,
        (std::string("Voice: ") + ttsVoice).c_str(),
        false,
        true);

    const std::string counter = std::to_string(index + 1) + " / "
        + std::to_string(static_cast<int>(steps.size()));
    const Vector2 counterSz = MeasureTextEx(font, counter.c_str(), kFontSmall, 1.0f);
    DrawTextEx(
        bold,
        counter.c_str(),
        {toolbar.x + toolbar.width - counterSz.x - 12.0f,
         toolbar.y + (toolbar.height - counterSz.y) * 0.5f},
        kFontSmall,
        1.0f,
        kTextPrimary);

    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, prevBtn) && index > 0)
            goPrev();
        else if (
            CheckCollisionPointRec(mouse, nextBtn)
            && index + 1 < static_cast<int>(steps.size()))
            goNext();
        else if (CheckCollisionPointRec(mouse, saveBtn))
            applyCurrentStep();
        else if (CheckCollisionPointRec(mouse, sideHit))
        {
            editTtsText = !editTtsText;
            textFieldFocused = true;
            flowFocus = FlowFocus::None;
            startPhaseMenuOpen = false;
            clearSelection();
            mouseSelecting = false;
            if (editTtsText)
            {
                if (ttsTextBuffer.empty())
                    ttsTextBuffer = textBuffer;
                cursor = static_cast<int>(ttsTextBuffer.size());
            }
            else
                cursor = static_cast<int>(textBuffer.size());
            preferredCaretX = -1.0f;
        }
        else if (CheckCollisionPointRec(mouse, ttsToggle))
        {
            ttsEnabled = !ttsEnabled;
            dirtyStep = true;
            if (ttsEnabled)
                ensureDefaultAudioPath();
        }
        else if (CheckCollisionPointRec(mouse, voiceBtnRect))
        {
            voiceMenuOpen = !voiceMenuOpen;
            if (voiceMenuOpen)
            {
                const std::vector<std::string>& voices = builtinVoiceIds();
                const int voiceCount = static_cast<int>(voices.size());
                int selected = 0;
                for (int i = 0; i < voiceCount; ++i)
                {
                    if (voices[static_cast<size_t>(i)] == ttsVoice)
                    {
                        selected = i;
                        break;
                    }
                }
                voiceMenuScroll = static_cast<float>(std::max(
                    0, selected - kVoiceMenuVisibleRows / 2));
            }
        }
        else if (canAddChoice && CheckCollisionPointRec(mouse, addChoiceBtn))
        {
            addChoiceNearCurrent();
        }
        else if (editingChoice && CheckCollisionPointRec(mouse, delChoiceBtn))
        {
            if (confirmDeleteChoice)
                deleteCurrentChoice();
            else
                confirmDeleteChoice = true;
        }
        else if (confirmDeleteChoice)
        {
            // Click elsewhere cancels delete confirm.
            confirmDeleteChoice = false;
        }
    }

    // Body: list | editor
    const float bodyY = toolbar.y + toolbar.height + 8.0f;
    const float bodyH = pane.y + pane.height - bodyY - 8.0f;
    listPanel = {pane.x + 8.0f, bodyY, kListW, bodyH};
    const Rectangle editor = {
        listPanel.x + listPanel.width + 8.0f,
        bodyY,
        pane.x + pane.width - (listPanel.x + listPanel.width + 16.0f),
        bodyH};

    DrawRectangleRec(listPanel, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(listPanel, 1.0f, kPanelInnerEdge);
    DrawRectangleRec(editor, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(editor, 1.0f, kPanelInnerEdge);

    // Step list
    BeginScissorMode(
        static_cast<int>(listPanel.x),
        static_cast<int>(listPanel.y),
        static_cast<int>(listPanel.width),
        static_cast<int>(listPanel.height));
    float ly = listPanel.y + 4.0f - listScroll;
    for (size_t i = 0; i < steps.size(); ++i)
    {
        const Rectangle row = {
            listPanel.x + 2.0f, ly, listPanel.width - 4.0f, kRowH - 1.0f};
        const bool selected = static_cast<int>(i) == index;
        const bool hover =
            !voiceMenuOpen && CheckCollisionPointRec(mouse, row)
            && CheckCollisionPointRec(mouse, listPanel);
        if (selected)
            DrawRectangleRec(row, kSelection);
        else if (hover)
            DrawRectangleRec(row, Color{50, 46, 58, 200});
        const std::string label = truncateOneLine(steps[i].stepLabel, 28);
        DrawTextEx(
            font,
            label.c_str(),
            {row.x + 6.0f, row.y + 3.0f},
            kFontTiny,
            1.0f,
            selected ? kTextPrimary : kTextMuted);
        if (canClick && hover)
            selectIndex(static_cast<int>(i));
        ly += kRowH;
    }
    EndScissorMode();

    // Editor header — always show scene id so shops cannot be confused.
    const DialogWalkStep& step = steps[static_cast<size_t>(index)];
    float ey = editor.y + 8.0f;
    DrawTextEx(
        bold,
        (*selectionSceneId).c_str(),
        {editor.x + 10.0f, ey},
        kFontSmall,
        1.0f,
        kPanelBorder);
    ey += 18.0f;
    DrawTextEx(
        font,
        truncateOneLine(step.breadcrumb, 72).c_str(),
        {editor.x + 10.0f, ey},
        kFontTiny,
        1.0f,
        kTextMuted);
    ey += 18.0f;

    ey += drawChoiceFlowPanel(font, bold, editor, ey, canClick, mouse);

    // Mode banner — Text vs TTS (toolbar slider).
    const Rectangle modeBanner = {editor.x + 10.0f, ey, editor.width - 20.0f, 28.0f};
    const Color modeFill = editTtsText ? Color{48, 40, 70, 255} : Color{40, 52, 44, 255};
    const Color modeEdge = editTtsText ? Color{140, 120, 200, 255} : Color{100, 160, 110, 255};
    DrawRectangleRec(modeBanner, modeFill);
    DrawRectangleLinesEx(modeBanner, 1.0f, modeEdge);
    const char* modeTitle = editTtsText
        ? "TTS   -   spoken script sent to the voice API"
        : (editingChoice
               ? "text   -   on-screen reply after this speech option"
               : "text   -   on-screen dialog the player reads");
    DrawTextEx(
        font,
        modeTitle,
        {modeBanner.x + 8.0f, modeBanner.y + 7.0f},
        kFontTiny,
        1.0f,
        modeEdge);
    ey += 36.0f;

    const float metaH = 78.0f;
    textField = {
        editor.x + 10.0f,
        ey,
        editor.width - 20.0f,
        std::max(80.0f, editor.y + editor.height - ey - metaH - 12.0f)};
    DrawRectangleRec(textField, Color{12, 11, 16, 255});
    const bool fieldHover = CheckCollisionPointRec(mouse, textField);
    DrawRectangleLinesEx(
        textField,
        textFieldFocused ? 2.0f : 1.0f,
        textFieldFocused ? modeEdge : (fieldHover ? kPanelBorder : kPanelInnerEdge));

    const std::string& showBuf = editTtsText ? ttsTextBuffer : textBuffer;
    cursor = std::clamp(cursor, 0, static_cast<int>(showBuf.size()));
    if (selectAnchor > static_cast<int>(showBuf.size()))
        selectAnchor = static_cast<int>(showBuf.size());
    const float pad = 8.0f;
    const float fontSize = kFontEdit;
    const float lineHeight = fontSize + 4.0f;
    const std::vector<EditorVisualLine> lines = layoutWrappedTextLines(
        font, showBuf, textField.width - pad * 2.0f, fontSize);
    ensureCaretVisible(lines, lineHeight);

    // Selection highlight behind text (same layout as caret / click mapping).
    if (textFieldFocused && hasSelection() && !lines.empty())
    {
        int selStart = 0;
        int selEnd = 0;
        selectionRange(selStart, selEnd);
        BeginScissorMode(
            static_cast<int>(textField.x),
            static_cast<int>(textField.y),
            static_cast<int>(textField.width),
            static_cast<int>(textField.height));
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const float y =
                textField.y + pad + static_cast<float>(i) * lineHeight - textScroll;
            if (y + lineHeight < textField.y || y > textField.y + textField.height)
                continue;
            const int lineSelStart = std::max(selStart, lines[i].start);
            const int lineSelEnd = std::min(selEnd, lines[i].end);
            if (lineSelStart >= lineSelEnd)
                continue;
            const float x0 =
                textField.x + pad + caretXOnVisualLine(font, lines[i], lineSelStart, fontSize);
            const float x1 =
                textField.x + pad + caretXOnVisualLine(font, lines[i], lineSelEnd, fontSize);
            DrawRectangleRec(
                {x0, y, std::max(2.0f, x1 - x0), fontSize + 2.0f},
                Color{70, 90, 140, 180});
        }
        EndScissorMode();
    }

    if (showBuf.empty() && !textFieldFocused)
    {
        DrawTextEx(
            font,
            "(empty  -  click and type here)",
            {textField.x + pad, textField.y + pad},
            fontSize,
            1.0f,
            kTextMuted);
    }
    else if (editTtsText)
    {
        if (docs != nullptr)
            ensureTtsSyntaxThemeLoaded(docs->resourceDir);
        std::vector<Color> colors;
        buildTtsHighlightColors(showBuf, colors);
        drawVisualTextLinesColored(
            font,
            lines,
            colors,
            textField,
            pad,
            fontSize,
            lineHeight,
            textScroll,
            kTextPrimary);
    }
    else
    {
        drawVisualTextLines(
            font,
            lines,
            textField,
            pad,
            fontSize,
            lineHeight,
            textScroll,
            kTextPrimary);
    }

    // Caret uses the same visual lines as the drawn text (hidden while selecting).
    if (textFieldFocused && !hasSelection())
    {
        const int lineIndex = visualLineIndexForCursor(
            lines, cursor, static_cast<int>(showBuf.size()));
        const EditorVisualLine& line = lines[static_cast<size_t>(lineIndex)];
        const float caretX =
            textField.x + pad + caretXOnVisualLine(font, line, cursor, fontSize);
        const float caretY =
            textField.y + pad + static_cast<float>(lineIndex) * lineHeight - textScroll;
        if (caretY + lineHeight >= textField.y
            && caretY <= textField.y + textField.height)
        {
            DrawRectangle(
                static_cast<int>(caretX),
                static_cast<int>(caretY),
                2,
                static_cast<int>(fontSize + 2.0f),
                modeEdge);
        }
    }

    float my = textField.y + textField.height + 8.0f;
    DrawTextEx(
        font,
        (std::string("Speech ") + (ttsEnabled ? "ON" : "off") + "   |   Voice: " + ttsVoice).c_str(),
        {editor.x + 10.0f, my},
        kFontTiny,
        1.0f,
        ttsEnabled ? Color{160, 200, 140, 255} : kTextMuted);
    my += 16.0f;
    DrawTextEx(
        font,
        truncateOneLine(
            ttsAudio.empty() ? "Audio: (none  -  turn Speech ON to assign a path)"
                             : ("Audio: " + ttsAudio),
            78)
            .c_str(),
        {editor.x + 10.0f, my},
        kFontTiny,
        1.0f,
        kTextMuted);
    my += 16.0f;
    if (!status.empty())
        DrawTextEx(
            font, status.c_str(), {editor.x + 10.0f, my}, kFontTiny, 1.0f, Color{120, 180, 120, 255});
    if (!error.empty())
        DrawTextEx(
            font, error.c_str(), {editor.x + 10.0f, my}, kFontTiny, 1.0f, Color{220, 100, 90, 255});

    DrawTextEx(
        font,
        editingChoice
            ? "Speech option fields above  |  Right-click reply: full screen  |  Ctrl/Cmd+S saves conversations.json"
            : "Right-click text: Edit full screen  |  text/TTS slider  |  Ctrl/Cmd+C V X A  |  Alt+Left/Right  |  Ctrl+S",
        {editor.x + 10.0f, editor.y + editor.height - 18.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    // Draw menus LAST so they paint above the text field and list.
    drawVoiceMenu(font);
    drawStartPhaseMenu(font);

    if (fieldContextOpen)
    {
        DrawRectangleRec(fieldContextRect, Color{32, 28, 40, 245});
        DrawRectangleLinesEx(fieldContextRect, 1.0f, kPanelBorder);
        DrawTextEx(
            font,
            "Edit full screen",
            {fieldContextRect.x + 10.0f, fieldContextRect.y + 6.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);
    }
}

} // namespace timberline_editor
