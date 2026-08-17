/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Dialog walkthrough: linear navigation of conversation lines + TTS.
 ******************************************************************************/

#include "DialogWalkthrough.h"

#include "ConversationHelpers.h"
#include "EditorButton.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "TtsVoiceMarkup.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
        compact += "…";
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
        breadcrumb.empty() ? label : (breadcrumb + " › " + label);

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
        const std::string phaseCrumb = actor + " › " + phaseId;

        if (phase.contains("intro") && phase["intro"].is_string())
        {
            DialogWalkStep step;
            step.sceneId = *selectionSceneId;
            step.objectPointer = phasePointer;
            step.field = DialogWalkStep::Field::Intro;
            step.treeKey = "narrative-conv:" + phasePointer + "/intro";
            step.breadcrumb = phaseCrumb + " › Intro";
            step.stepLabel = phaseId + " · intro";
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
            step.breadcrumb = phaseCrumb + " › Resume intro";
            step.stepLabel = phaseId + " · resume";
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
                    phaseCrumb + " › lines",
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
    cursor = 0;
    textScroll = 0.0f;

    textBuffer.clear();
    ttsTextBuffer.clear();
    ttsVoice.clear();
    ttsAudio.clear();
    ttsEnabled = false;

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

    cursor = static_cast<int>(textBuffer.size());

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
        error = "Cannot save — object missing";
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
            return selectObjectField(ptr, field);
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

void DialogWalkthrough::handleTextTyping()
{
    if (!textFieldFocused || voiceMenuOpen)
        return;

    std::string& buf = editTtsText ? ttsTextBuffer : textBuffer;
    cursor = std::clamp(cursor, 0, static_cast<int>(buf.size()));

    int codepoint = GetCharPressed();
    while (codepoint > 0)
    {
        insertUtf8(buf, cursor, codepoint);
        dirtyStep = true;
        preferredCaretX = -1.0f;
        codepoint = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
    {
        backspaceUtf8(buf, cursor);
        dirtyStep = true;
        preferredCaretX = -1.0f;
    }
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE))
    {
        if (cursor < static_cast<int>(buf.size()))
        {
            const int next = utf8Next(buf, cursor);
            buf.erase(static_cast<size_t>(cursor), static_cast<size_t>(next - cursor));
            dirtyStep = true;
            preferredCaretX = -1.0f;
        }
    }
    // Enter inserts newline.
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        insertUtf8(buf, cursor, '\n');
        dirtyStep = true;
        preferredCaretX = -1.0f;
    }
    const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if ((IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) && !alt)
    {
        cursor = utf8Prev(buf, cursor);
        preferredCaretX = -1.0f;
    }
    if ((IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) && !alt)
    {
        cursor = utf8Next(buf, cursor);
        preferredCaretX = -1.0f;
    }
    if ((IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) && !alt
        && textField.width > 8.0f)
    {
        const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
        const float fontSize = kFontEdit;
        const auto lines = layoutWrappedTextLines(
            font, buf, textField.width - 16.0f, fontSize);
        cursor = moveCursorVertical(
            font, lines, buf, cursor, -1, fontSize, preferredCaretX);
    }
    if ((IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) && !alt
        && textField.width > 8.0f)
    {
        const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
        const float fontSize = kFontEdit;
        const auto lines = layoutWrappedTextLines(
            font, buf, textField.width - 16.0f, fontSize);
        cursor = moveCursorVertical(
            font, lines, buf, cursor, +1, fontSize, preferredCaretX);
    }
    if (IsKeyPressed(KEY_HOME))
    {
        cursor = 0;
        preferredCaretX = -1.0f;
    }
    if (IsKeyPressed(KEY_END))
    {
        cursor = static_cast<int>(buf.size());
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
        int i = static_cast<int>((mouse.y - voiceMenuRect.y - 2.0f) / rowH);
        if (i >= 0 && i < static_cast<int>(voices.size()))
        {
            ttsVoice = voices[static_cast<size_t>(i)];
            dirtyStep = true;
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
    const float rowH = 22.0f;
    const float menuH = static_cast<float>(voices.size()) * rowH + 4.0f;
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
    float my = voiceMenuRect.y + 2.0f;
    for (const std::string& v : voices)
    {
        const Rectangle row = {
            voiceMenuRect.x + 2.0f, my, voiceMenuRect.width - 4.0f, rowH - 2.0f};
        const bool hov = CheckCollisionPointRec(mouse, row);
        const bool selected = (v == ttsVoice);
        if (selected)
            DrawRectangleRec(row, kSelection);
        else if (hov)
            DrawRectangleRec(row, Color{60, 54, 72, 220});
        DrawTextEx(
            font,
            v.c_str(),
            {row.x + 8.0f, row.y + 3.0f},
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
    const bool canClick = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    // Voice menu is modal for clicks — handle first so it is never under other controls.
    if (canClick && voiceMenuOpen && handleVoiceMenuClick(mouse))
        return;

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

    if (CheckCollisionPointRec(mouse, listPanel))
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

    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, textField))
        {
            textFieldFocused = true;
            // Place caret at click — layout matches draw.
            const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
            const std::string& buf = editTtsText ? ttsTextBuffer : textBuffer;
            const float pad = 8.0f;
            const float fontSize = kFontEdit;
            const float lineHeight = fontSize + 4.0f;
            const auto lines = layoutWrappedTextLines(
                font, buf, textField.width - pad * 2.0f, fontSize);
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
        else if (!CheckCollisionPointRec(mouse, voiceBtnRect) && !voiceMenuOpen)
            textFieldFocused = false;
    }

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
        ignoreInputFrames <= 0 && !voiceMenuOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    DrawRectangleRec(pane, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(pane, 1.0f, kPanelInnerEdge);

    // Scene is chosen from the left tree (expand a scene root). Title bar shows which.
    const float sceneBarH = 28.0f;
    const Rectangle sceneBar = {pane.x + 8.0f, pane.y + 8.0f, pane.width - 16.0f, sceneBarH};
    DrawRectangleRec(sceneBar, Color{28, 26, 36, 255});
    DrawRectangleLinesEx(sceneBar, 1.0f, kPanelInnerEdge);
    const std::string sceneTitle =
        (selectionSceneId == nullptr || selectionSceneId->empty())
        ? "Dialog editor  ·  expand a scene in the left tree"
        : ("Dialog editor  ·  " + *selectionSceneId);
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

    // Mode: which buffer is being edited
    const Rectangle dialogModeBtn = {bx, by, 118.0f, btnH};
    bx += 124.0f;
    const Rectangle ttsModeBtn = {bx, by, 118.0f, btnH};
    bx += 128.0f;

    // TTS enabled for this line + voice
    const Rectangle ttsToggle = {bx, by, 110.0f, btnH};
    bx += 118.0f;
    voiceBtnRect = {bx, by, 130.0f, btnH};

    drawEditorButton(font, prevBtn, "◀ Prev", false, index > 0);
    drawEditorButton(font, nextBtn, "Next ▶", false, index + 1 < static_cast<int>(steps.size()));
    drawEditorButton(font, saveBtn, dirtyStep ? "Save *" : "Save", true, true);
    drawEditorButton(font, dialogModeBtn, "Dialog text", !editTtsText, true);
    drawEditorButton(font, ttsModeBtn, "TTS script", editTtsText, true);
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
        else if (CheckCollisionPointRec(mouse, dialogModeBtn))
        {
            editTtsText = false;
            textFieldFocused = true;
            cursor = static_cast<int>(textBuffer.size());
        }
        else if (CheckCollisionPointRec(mouse, ttsModeBtn))
        {
            editTtsText = true;
            textFieldFocused = true;
            if (ttsTextBuffer.empty())
                ttsTextBuffer = textBuffer;
            cursor = static_cast<int>(ttsTextBuffer.size());
        }
        else if (CheckCollisionPointRec(mouse, ttsToggle))
        {
            ttsEnabled = !ttsEnabled;
            dirtyStep = true;
            if (ttsEnabled)
                ensureDefaultAudioPath();
        }
        else if (CheckCollisionPointRec(mouse, voiceBtnRect))
            voiceMenuOpen = !voiceMenuOpen;
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

    if (!step.playerLabel.empty())
    {
        DrawTextEx(font, "Player choice:", {editor.x + 10.0f, ey}, kFontTiny, 1.0f, kTextMuted);
        ey += 16.0f;
        DrawTextEx(
            font,
            truncateOneLine(step.playerLabel, 80).c_str(),
            {editor.x + 10.0f, ey},
            kFontSmall,
            1.0f,
            kTextPrimary);
        ey += 22.0f;
    }

    // Mode banner — high contrast so Dialog vs TTS script is obvious.
    const Rectangle modeBanner = {editor.x + 10.0f, ey, editor.width - 20.0f, 28.0f};
    const Color modeFill = editTtsText ? Color{48, 40, 70, 255} : Color{40, 52, 44, 255};
    const Color modeEdge = editTtsText ? Color{140, 120, 200, 255} : Color{100, 160, 110, 255};
    DrawRectangleRec(modeBanner, modeFill);
    DrawRectangleLinesEx(modeBanner, 1.0f, modeEdge);
    const char* modeTitle = editTtsText
        ? "Editing TTS SCRIPT  —  words sent to the voice API (may differ from on-screen)"
        : "Editing DIALOG TEXT  —  words shown to the player on screen";
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
    const float pad = 8.0f;
    const float fontSize = kFontEdit;
    const float lineHeight = fontSize + 4.0f;
    const std::vector<EditorVisualLine> lines = layoutWrappedTextLines(
        font, showBuf, textField.width - pad * 2.0f, fontSize);
    ensureCaretVisible(lines, lineHeight);

    if (showBuf.empty() && !textFieldFocused)
    {
        DrawTextEx(
            font,
            "(empty — click and type here)",
            {textField.x + pad, textField.y + pad},
            fontSize,
            1.0f,
            kTextMuted);
    }
    else if (editTtsText)
    {
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

    // Caret uses the same visual lines as the drawn text.
    if (textFieldFocused)
    {
        const int lineIndex = visualLineIndexForCursor(
            lines, cursor, static_cast<int>(showBuf.size()));
        const EditorVisualLine& line = lines[static_cast<size_t>(lineIndex)];
        const float caretX =
            textField.x + pad + caretXOnVisualLine(font, line, cursor, fontSize);
        const float caretY =
            textField.y + pad + static_cast<float>(lineIndex) * lineHeight - textScroll;
        // Solid caret while focused — position matches insertion point.
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
        (std::string("Speech ") + (ttsEnabled ? "ON" : "off") + "  ·  Voice: " + ttsVoice).c_str(),
        {editor.x + 10.0f, my},
        kFontTiny,
        1.0f,
        ttsEnabled ? Color{160, 200, 140, 255} : kTextMuted);
    my += 16.0f;
    DrawTextEx(
        font,
        truncateOneLine(
            ttsAudio.empty() ? "Audio: (none — turn Speech ON to assign a path)"
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
        "Dialog text = on screen  ·  TTS script = voice API  ·  Speech ON stores tts/voice/audio  ·  Alt+←/→  ·  Ctrl+S  ·  Enter = newline",
        {editor.x + 10.0f, editor.y + editor.height - 18.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    // Draw voice menu LAST so it paints above the text field and list.
    drawVoiceMenu(font);
}

} // namespace timberline_editor
