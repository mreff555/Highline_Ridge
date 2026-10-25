/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "ActorDialogAuthoringDialog.h"
#include "EditorAudio.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorPrefs.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "ImageCompression.h"
#include "PlatformPath.h"
#include "SceneAuthoring.h"
#include "TtsVoiceMarkup.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

using timberline_engine::builtinVoiceIds;
using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::loadAssetBytesFromFile;
using timberline_engine::normalizeVoiceId;
using timberline_engine::pathJoin;

namespace timberline_editor
{

namespace
{

constexpr int kFocusLineId = 0;
constexpr int kFocusActorId = 1;
constexpr int kFocusActorName = 2;
constexpr int kFocusBagText = 3;
constexpr int kFocusBagTts = 4;
constexpr int kFocusTabActor = 5;
constexpr int kFocusInventory = 6;
constexpr int kFocusStoryFlag = 7;
constexpr int kFocusRequiresPhase = 8;
constexpr int kFocusStatusActor = 9;
constexpr int kFocusStatusKey = 10;
constexpr int kFocusOnZeroLucidity = 11;
constexpr int kFocusHealth = 12;
constexpr int kFocusEnergy = 13;
constexpr int kFocusResolve = 14;
constexpr int kFocusLucidity = 15;
constexpr int kFocusCharisma = 16;
constexpr int kFocusMoney = 17;
constexpr int kFocusActorOpinion = 18;
constexpr int kFocusActorTab = 19;
constexpr int kFocusReqMoney = 20;
constexpr int kFocusInsuffMoney = 21;
constexpr int kFocusMinDay = 22;
constexpr int kFocusMaxDay = 23;
constexpr int kFocusLucBelow = 24;
constexpr int kFocusLucAbove = 25;
constexpr int kFocusDaysFlag = 26;
constexpr int kFocusDaysMin = 27;
constexpr int kFocusDaysMax = 28;
constexpr int kFocusOpinionActor = 29;
constexpr int kFocusOpinionAtLeast = 30;
constexpr int kFocusOpinionAtMost = 31;

const char* bagSlotLabel(int index, bool opener)
{
    if (index == 1 && opener)
        return "Resume intro";
    return "On-screen";
}

std::string ellipsizeUi(
    Font font,
    const std::string& text,
    float fontSize,
    float maxWidth)
{
    if (text.empty()
        || MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x <= maxWidth)
        return text;
    std::string out = text;
    const std::string ellipsis = "...";
    while (!out.empty()
           && MeasureTextEx(font, (out + ellipsis).c_str(), fontSize, 1.0f).x > maxWidth)
        out.pop_back();
    return out + ellipsis;
}

void drawClippedFieldText(
    Font font,
    const Rectangle& field,
    const std::string& text,
    const char* placeholder,
    float fontSize,
    Color color)
{
    BeginScissorMode(
        (int)field.x + 2, (int)field.y + 2, (int)field.width - 4, (int)field.height - 4);
    const std::string shown = text.empty()
        ? std::string(placeholder ? placeholder : "")
        : ellipsizeUi(font, text, fontSize, field.width - 16.0f);
    DrawTextEx(
        font,
        shown.c_str(),
        {field.x + 8.0f, field.y + 6.0f},
        fontSize,
        1.0f,
        color);
    EndScissorMode();
}

void insertUtf8(std::string& buffer, int codepoint)
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
    else
    {
        bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    }
    buffer.append(bytes, bytes + size);
}

void backspaceUtf8(std::string& buffer)
{
    if (buffer.empty())
        return;
    int i = static_cast<int>(buffer.size()) - 1;
    while (i > 0
           && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        --i;
    buffer.erase(static_cast<size_t>(i));
}

std::string sanitizeLineId(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (unsigned char ch : raw)
    {
        if (std::isalnum(ch) || ch == '_' || ch == '-' || ch == ':')
            out.push_back(static_cast<char>(ch));
        else if (ch == ' ' || ch == '/' || ch == '.')
        {
            if (!out.empty() && out.back() != '_')
                out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

std::string formatSignedInt(int v)
{
    return std::to_string(v);
}

std::string formatFloat(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return buf;
}

bool assetRelExists(DocumentWorkspace* docs, const std::string& rel)
{
    if (docs == nullptr || rel.empty())
        return false;
    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    std::vector<std::string> candidates = buildAssetSearchPaths(assetRoot, rel);
    if (!docs->resourceDir.empty())
    {
        std::string stripped = rel;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(10);
        candidates.push_back(pathJoin(docs->resourceDir, stripped));
    }
    for (const std::string& path : candidates)
    {
        if (FileExists(path.c_str()) || FileExists(compressedAssetPath(path).c_str()))
            return true;
    }
    return false;
}

bool pointerEndsWith(const std::string& pointer, const char* suffix)
{
    const size_t n = std::char_traits<char>::length(suffix);
    return pointer.size() >= n && pointer.compare(pointer.size() - n, n, suffix) == 0;
}

bool pointerIsPhaseObject(const std::string& pointer)
{
    const std::string marker = "/speakPhases/";
    const size_t pos = pointer.rfind(marker);
    if (pos == std::string::npos)
        return false;
    const std::string rest = pointer.substr(pos + marker.size());
    if (rest.empty())
        return false;
    for (unsigned char ch : rest)
    {
        if (ch == '/')
            return false;
        if (!std::isdigit(ch))
            return false;
    }
    return true;
}

bool pointerIsPhaseOpener(const std::string& pointer)
{
    return pointerEndsWith(pointer, "/intro") || pointerEndsWith(pointer, "/text")
        || pointerEndsWith(pointer, "/resumeIntro") || pointerIsPhaseObject(pointer);
}

std::string bagPointerFrom(const std::string& pointer)
{
    if (pointerEndsWith(pointer, "/intro") || pointerEndsWith(pointer, "/text")
        || pointerEndsWith(pointer, "/resumeIntro"))
    {
        const size_t slash = pointer.rfind('/');
        if (slash != std::string::npos)
            return pointer.substr(0, slash);
    }
    return pointer;
}

std::string detectTextFieldKey(const std::string& pointer, const nlohmann::json* obj)
{
    if (pointerEndsWith(pointer, "/intro"))
        return "intro";
    if (pointerEndsWith(pointer, "/text"))
        return "text";
    if (obj != nullptr && obj->is_object())
    {
        if (obj->contains("label"))
            return "response";
        if (pointer.find("/lines/") != std::string::npos)
            return "text";
        if (obj->contains("intro") && obj->value("intro", std::string()).size() > 0)
            return "intro";
        if (obj->contains("response") && !obj->contains("type"))
            return "response";
    }
    return "text";
}

void setOrEraseString(nlohmann::json& obj, const char* key, const std::string& value)
{
    if (value.empty())
        obj.erase(key);
    else
        obj[key] = value;
}

void setOrEraseFloat(nlohmann::json& obj, const char* key, float value)
{
    if (value == 0.0f)
        obj.erase(key);
    else
        obj[key] = value;
}

void setOrEraseInt(nlohmann::json& obj, const char* key, int value, bool writeZero = false)
{
    if (!writeZero && value == 0)
        obj.erase(key);
    else
        obj[key] = value;
}

void setOrEraseBool(nlohmann::json& obj, const char* key, bool value)
{
    if (!value)
        obj.erase(key);
    else
        obj[key] = true;
}

nlohmann::json statusToJson(
    float health,
    float energy,
    float resolve,
    float lucidity,
    float charisma,
    float money,
    const std::string& actor,
    int actorOpinion,
    float actorTab,
    const std::string& key,
    const std::string& onZeroLucidity)
{
    nlohmann::json st = nlohmann::json::object();
    if (health != 0.0f)
        st["health"] = health;
    if (energy != 0.0f)
        st["energy"] = energy;
    if (resolve != 0.0f)
        st["resolve"] = resolve;
    if (lucidity != 0.0f)
        st["lucidity"] = lucidity;
    if (charisma != 0.0f)
        st["charisma"] = charisma;
    if (money != 0.0f)
        st["money"] = money;
    if (!actor.empty())
        st["actor"] = actor;
    if (actorOpinion != 0)
        st["actorOpinion"] = actorOpinion;
    if (actorTab != 0.0f)
        st["actorTab"] = actorTab;
    if (!key.empty())
        st["key"] = key;
    if (!onZeroLucidity.empty())
        st["onZeroLucidity"] = onZeroLucidity;
    return st;
}

} // namespace

ActorDialogAuthoringDialog::~ActorDialogAuthoringDialog()
{
    generateCancel.store(true);
    joinGenerateThread();
    stopPreviewVoice();
}

void ActorDialogAuthoringDialog::joinGenerateThread()
{
    if (generateThread.joinable())
        generateThread.join();
}

void ActorDialogAuthoringDialog::resetWorkingState()
{
    loadedJsonPointer.clear();
    loadedLineId.clear();
    textFieldKey = "text";
    isOpener = false;
    lineId.clear();
    actorId.clear();
    actorName.clear();
    for (ActorDialogBagEdit& bag : bags)
        bag = ActorDialogBagEdit{};
    selectedBag = 0;
    playerResponsesEnabled = false;
    statusHealth = 0.0f;
    statusEnergy = 0.0f;
    statusResolve = 0.0f;
    statusLucidity = 0.0f;
    statusCharisma = 0.0f;
    statusMoney = 0.0f;
    statusActor.clear();
    statusActorOpinion = 0;
    statusActorTab = 0.0f;
    statusKey.clear();
    statusOnZeroLucidity.clear();
    requiresMoney = 0.0f;
    requiresInsufficientMoney = 0.0f;
    requiresPositiveActorTab = false;
    requiresPayActorTabInFull = false;
    requiresInsufficientForActorTab = false;
    requiresSaloonRoomAvailable = false;
    tabActor.clear();
    requiresLightSource = false;
    requiresInventoryItem.clear();
    requiresFlag.clear();
    requiresPhase.clear();
    minDay = 0;
    maxDay = 0;
    requiresLucidityBelow = 0.0f;
    requiresLucidityAbove = 0.0f;
    requiresRecentCollapse = false;
    requiresRecentSleep = false;
    requiresDaysSinceFlag.clear();
    requiresDaysSinceMin = 0;
    requiresDaysSinceMax = 0;
    opinionActor.clear();
    hasActorOpinionAtLeast = false;
    requiresActorOpinionAtLeast = 0;
    hasActorOpinionAtMost = false;
    requiresActorOpinionAtMost = 0;
    focusField = kFocusLineId;
    focusDraft.clear();
    scrollY = 0.0f;
    voiceMenuOpen = false;
    itemSuggestOpen = false;
    status.clear();
    error.clear();
}

void ActorDialogAuthoringDialog::copyNodeToDialog(const DialogFlowNode& n)
{
    lineId = n.lineId.empty()
        ? (n.sourceChoiceId.empty() ? n.sourcePhaseId : n.sourceChoiceId)
        : n.lineId;
    actorId = n.actorId;
    actorName = n.actorName;
    bags[0].text = n.dialogText;
    bags[0].ttsText = n.dialogTts;
    bags[0].ttsVoice = n.defaultVoice;
    bags[0].ttsAudio = n.dialogTtsAudio;
    bags[0].ttsAudioSegments = n.dialogTtsAudioSegments;
    bags[1].text = n.resumeIntroText;
    bags[1].ttsText = n.resumeIntroTts;
    bags[1].ttsVoice = n.resumeIntroVoice;
    bags[1].ttsAudio = n.resumeIntroTtsAudio;
    playerResponsesEnabled = n.playerResponsesEnabled;
}

void ActorDialogAuthoringDialog::copyDialogToNode(DialogFlowNode& n) const
{
    n.lineId = lineId;
    n.actorId = actorId;
    n.actorName = actorName;
    n.dialogText = bags[0].text;
    n.dialogTts = bags[0].ttsText;
    n.defaultVoice = bags[0].ttsVoice;
    n.dialogTtsAudio = bags[0].ttsAudio.empty() ? defaultBagAudioPath(0) : bags[0].ttsAudio;
    n.dialogTtsAudioSegments = bags[0].ttsAudioSegments;
    n.resumeIntroText = bags[1].text;
    n.resumeIntroTts = bags[1].ttsText;
    n.resumeIntroVoice = bags[1].ttsVoice;
    n.resumeIntroTtsAudio =
        bags[1].ttsAudio.empty() && !bags[1].text.empty() ? defaultBagAudioPath(1)
                                                          : bags[1].ttsAudio;
    n.playerResponsesEnabled = playerResponsesEnabled;
}

nlohmann::json* ActorDialogAuthoringDialog::resolveBagObject()
{
    return const_cast<nlohmann::json*>(
        static_cast<const ActorDialogAuthoringDialog*>(this)->resolveBagObject());
}

const nlohmann::json* ActorDialogAuthoringDialog::resolveBagObject() const
{
    if (docs == nullptr)
        return nullptr;
    const std::string pointer =
        loadedJsonPointer.empty() && node != nullptr ? node->jsonPointer : loadedJsonPointer;
    if (pointer.empty())
        return nullptr;
    const std::string bagPtr = bagPointerFrom(pointer);
    const nlohmann::json* obj = docs->conversationJsonAt(bagPtr);
    if (obj != nullptr && obj->is_object())
        return obj;
    return nullptr;
}

void ActorDialogAuthoringDialog::loadFromNodeAndJson()
{
    resetWorkingState();
    if (node != nullptr)
        copyNodeToDialog(*node);

    loadedJsonPointer = node != nullptr ? node->jsonPointer : std::string{};
    isOpener = pointerIsPhaseOpener(loadedJsonPointer);

    nlohmann::json* obj = resolveBagObject();
    if (obj != nullptr)
    {
        textFieldKey = detectTextFieldKey(loadedJsonPointer, obj);
        const std::string id = obj->value("id", lineId);
        if (!id.empty())
            lineId = id;
        loadedLineId = lineId;
        const std::string aid = obj->value("actorId", actorId);
        if (!aid.empty())
            actorId = aid;
        const std::string aname = obj->value("actorName", actorName);
        if (!aname.empty())
            actorName = aname;

        auto loadMainText = [&]() {
            if (textFieldKey == "intro")
                bags[0].text = obj->value("intro", bags[0].text);
            else if (textFieldKey == "response")
                bags[0].text = obj->value("response", bags[0].text);
            else
                bags[0].text = obj->value("text", bags[0].text);
        };
        loadMainText();
        bags[0].ttsText = obj->value("ttsText", bags[0].ttsText);
        bags[0].ttsAudio = obj->value("ttsAudio", bags[0].ttsAudio);
        {
            const std::string v = obj->value("ttsVoice", bags[0].ttsVoice);
            if (!v.empty())
                bags[0].ttsVoice = normalizeVoiceId(v);
        }
        bags[0].ttsAudioSegments.clear();
        if (obj->contains("ttsAudioSegments") && (*obj)["ttsAudioSegments"].is_array())
        {
            for (const auto& s : (*obj)["ttsAudioSegments"])
            {
                if (s.is_string() && !s.get<std::string>().empty())
                    bags[0].ttsAudioSegments.push_back(s.get<std::string>());
            }
        }

        if (isOpener)
        {
            bags[1].text = obj->value("resumeIntro", bags[1].text);
            bags[1].ttsText = obj->value("resumeTtsText", bags[1].ttsText);
            bags[1].ttsAudio = obj->value("resumeTtsAudio", bags[1].ttsAudio);
            const std::string rv = obj->value("resumeTtsVoice", bags[1].ttsVoice);
            if (!rv.empty())
                bags[1].ttsVoice = normalizeVoiceId(rv);
        }

        const nlohmann::json& st = obj->value("status", nlohmann::json::object());
        if (st.is_object())
        {
            statusHealth = st.value("health", 0.0f);
            statusEnergy = st.value("energy", 0.0f);
            statusResolve = st.value("resolve", st.value("tenacity", 0.0f));
            statusLucidity = st.value("lucidity", 0.0f);
            statusCharisma = st.value("charisma", 0.0f);
            statusMoney = st.value("money", 0.0f);
            statusActorOpinion = st.value("actorOpinion", 0);
            statusActorTab = st.value("actorTab", 0.0f);
            statusKey = st.value("key", std::string());
            statusOnZeroLucidity = st.value("onZeroLucidity", std::string());
            const nlohmann::json& actorNode = st.value("actor", nlohmann::json());
            if (actorNode.is_string())
                statusActor = actorNode.get<std::string>();
            else if (actorNode.is_object())
                statusActor = actorNode.value("id", std::string());
        }

        requiresMoney = obj->value("requiresMoney", 0.0f);
        requiresInsufficientMoney = obj->value("requiresInsufficientMoney", 0.0f);
        requiresPositiveActorTab = obj->value("requiresPositiveActorTab", false);
        requiresPayActorTabInFull = obj->value("requiresPayActorTabInFull", false);
        requiresInsufficientForActorTab =
            obj->value("requiresInsufficientForActorTab", false);
        requiresSaloonRoomAvailable = obj->value("requiresSaloonRoomAvailable", false);
        tabActor = obj->value("tabActor", std::string());
        requiresLightSource = obj->value("requiresLightSource", false);
        requiresInventoryItem = obj->value("requiresInventoryItem", std::string());
        requiresFlag = obj->value("requiresFlag", std::string());
        requiresPhase = obj->value("requiresPhase", std::string());
        minDay = obj->value("minDay", 0);
        maxDay = obj->value("maxDay", 0);
        requiresLucidityBelow = obj->value("requiresLucidityBelow", 0.0f);
        requiresLucidityAbove = obj->value("requiresLucidityAbove", 0.0f);
        requiresRecentCollapse = obj->value("requiresRecentCollapse", false);
        requiresRecentSleep = obj->value("requiresRecentSleep", false);
        requiresDaysSinceFlag = obj->value("requiresDaysSinceFlag", std::string());
        requiresDaysSinceMin = obj->value("requiresDaysSinceMin", 0);
        requiresDaysSinceMax = obj->value("requiresDaysSinceMax", 0);
        if (obj->contains("requiresDaysSince") && (*obj)["requiresDaysSince"].is_object())
        {
            const nlohmann::json& since = (*obj)["requiresDaysSince"];
            requiresDaysSinceFlag = since.value("flag", requiresDaysSinceFlag);
            requiresDaysSinceMin = since.value("min", requiresDaysSinceMin);
            requiresDaysSinceMax = since.value("max", requiresDaysSinceMax);
        }
        opinionActor = obj->value("opinionActor", std::string());
        if (obj->contains("requiresActorOpinionAtLeast"))
        {
            hasActorOpinionAtLeast = true;
            requiresActorOpinionAtLeast = obj->value("requiresActorOpinionAtLeast", 0);
        }
        if (obj->contains("requiresActorOpinionAtMost"))
        {
            hasActorOpinionAtMost = true;
            requiresActorOpinionAtMost = obj->value("requiresActorOpinionAtMost", 0);
        }

        const bool hasChoices = obj->contains("choices") && (*obj)["choices"].is_array()
            && !(*obj)["choices"].empty();
        playerResponsesEnabled = obj->value("playerResponsesEnabled", playerResponsesEnabled);
        if (hasChoices)
            playerResponsesEnabled = true;
    }

    if (flow != nullptr && node != nullptr
        && !flow->playerResponsesUnder(node->id).empty())
        playerResponsesEnabled = true;

    if (bags[0].ttsAudio.empty())
        bags[0].ttsAudio = defaultBagAudioPath(0);
    if (isOpener && bags[1].ttsAudio.empty() && !bags[1].text.empty())
        bags[1].ttsAudio = defaultBagAudioPath(1);
}

void ActorDialogAuthoringDialog::openForActorDialog(
    const std::string& inSceneId,
    DialogFlowNode* inNode)
{
    if (inNode == nullptr)
        return;
    joinGenerateThread();
    stopPreviewVoice();
    sceneId = inSceneId;
    node = inNode;
    loadFromNodeAndJson();
    open = true;
    ignoreInputFrames = 2;
    waitMouseRelease = true;
    generateBusy = false;
    generateKind = 0;
    generateResultPending = false;
}

void ActorDialogAuthoringDialog::closeDialog()
{
    generateCancel.store(true);
    joinGenerateThread();
    generateBusy = false;
    generateKind = 0;
    pendingTtsJobsPath.clear();
    stopPreviewVoice();
    open = false;
    waitMouseRelease = false;
    voiceMenuOpen = false;
    itemSuggestOpen = false;
    node = nullptr;
}

ActorDialogBagEdit& ActorDialogAuthoringDialog::activeBag()
{
    const int idx = (isOpener && selectedBag == 1) ? 1 : 0;
    return bags[idx];
}

const ActorDialogBagEdit& ActorDialogAuthoringDialog::activeBag() const
{
    const int idx = (isOpener && selectedBag == 1) ? 1 : 0;
    return bags[idx];
}

std::string ActorDialogAuthoringDialog::defaultBagAudioPath(int bagIndex) const
{
    const std::string scene = sceneId.empty() ? std::string("scene") : sceneId;
    std::string leaf = lineId.empty()
        ? (node != nullptr
               ? (node->sourceChoiceId.empty()
                      ? (node->sourcePhaseId.empty()
                             ? ("node_" + std::to_string(node->id))
                             : node->sourcePhaseId)
                      : node->sourceChoiceId)
               : std::string("line"))
        : lineId;
    for (char& ch : leaf)
    {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-'
              || ch == ':'))
            ch = '_';
    }
    if (bagIndex == 1)
        return "resources/audio/tts/" + scene + "/" + leaf + "_resume.mp3";
    return "resources/audio/tts/" + scene + "/" + leaf + ".mp3";
}

std::string ActorDialogAuthoringDialog::bagAudioPath(int bagIndex) const
{
    const ActorDialogBagEdit& bag = bags[bagIndex == 1 ? 1 : 0];
    return bag.ttsAudio.empty() ? defaultBagAudioPath(bagIndex) : bag.ttsAudio;
}

bool ActorDialogAuthoringDialog::bagAudioExists(int bagIndex) const
{
    return assetRelExists(docs, bagAudioPath(bagIndex));
}

std::string ActorDialogAuthoringDialog::effectiveApiKey() const
{
    return sessionKeys != nullptr ? sessionKeys->xaiKey : std::string{};
}

bool ActorDialogAuthoringDialog::lineIdIsUnique(const std::string& candidate) const
{
    if (docs == nullptr || sceneId.empty() || candidate.empty())
        return true;
    if (!docs->conversationsLoaded || !docs->conversationsRoot.contains(sceneId)
        || !docs->conversationsRoot[sceneId].is_object())
        return true;
    const nlohmann::json& scene = docs->conversationsRoot[sceneId];
    if (!scene.contains("speakPhases") || !scene["speakPhases"].is_array())
        return true;

    const std::string bagPtr = bagPointerFrom(loadedJsonPointer);
    const bool isLine = bagPtr.find("/lines/") != std::string::npos;
    const bool isChoice = textFieldKey == "response";

    const auto& phases = scene["speakPhases"];
    for (size_t pi = 0; pi < phases.size(); ++pi)
    {
        const nlohmann::json& phase = phases[pi];
        if (!phase.is_object())
            continue;
        const std::string phasePtr =
            "/" + sceneId + "/speakPhases/" + std::to_string(pi);
        if (!isLine && !isChoice)
        {
            const std::string pid = phase.value("id", std::string());
            if (pid == candidate && phasePtr != bagPtr)
                return false;
        }
        if (isLine && phase.contains("lines") && phase["lines"].is_array())
        {
            const auto& lines = phase["lines"];
            for (size_t li = 0; li < lines.size(); ++li)
            {
                if (!lines[li].is_object())
                    continue;
                const std::string linePtr = phasePtr + "/lines/" + std::to_string(li);
                const std::string lid = lines[li].value("id", std::string());
                if (lid == candidate && linePtr != bagPtr)
                    return false;
            }
        }
    }

    if (isChoice)
    {
        const nlohmann::json* self = resolveBagObject();
        if (self == nullptr)
            return true;
        const size_t slash = bagPtr.rfind('/');
        if (slash == std::string::npos)
            return true;
        const std::string parentPtr = bagPtr.substr(0, slash);
        const nlohmann::json* parent = docs->conversationJsonAt(parentPtr);
        if (parent == nullptr || !parent->is_array())
            return true;
        for (size_t i = 0; i < parent->size(); ++i)
        {
            const nlohmann::json& sib = (*parent)[i];
            if (!sib.is_object())
                continue;
            const std::string sibPtr = parentPtr + "/" + std::to_string(i);
            if (sibPtr == bagPtr)
                continue;
            if (sib.value("id", std::string()) == candidate)
                return false;
        }
    }
    return true;
}

void ActorDialogAuthoringDialog::rewritePhaseRefs(
    nlohmann::json& nodeJson,
    const std::string& oldId,
    const std::string& newId)
{
    if (oldId.empty() || oldId == newId)
        return;
    if (nodeJson.is_array())
    {
        for (auto& child : nodeJson)
            rewritePhaseRefs(child, oldId, newId);
        return;
    }
    if (!nodeJson.is_object())
        return;
    if (nodeJson.value("startPhase", std::string()) == oldId)
        nodeJson["startPhase"] = newId;
    if (nodeJson.value("requiresPhase", std::string()) == oldId)
        nodeJson["requiresPhase"] = newId;
    for (auto it = nodeJson.begin(); it != nodeJson.end(); ++it)
    {
        if (it.value().is_structured())
            rewritePhaseRefs(it.value(), oldId, newId);
    }
}

bool ActorDialogAuthoringDialog::applyRename()
{
    const std::string next = sanitizeLineId(lineId);
    if (next.empty())
    {
        error = "Id cannot be empty.";
        return false;
    }
    if (!lineIdIsUnique(next))
    {
        error = "Id \"" + next + "\" is already used in this scene.";
        return false;
    }
    lineId = next;
    status = "Id \"" + next + "\" is unique — Apply to save.";
    error.clear();
    return true;
}

void ActorDialogAuthoringDialog::applyPlayerResponsesToggle(bool enabled)
{
    playerResponsesEnabled = enabled;
    if (node == nullptr)
        return;
    node->playerResponsesEnabled = enabled;
    if (flow == nullptr)
        return;
    if (!enabled)
    {
        const auto responses = flow->playerResponsesUnder(node->id);
        for (const auto& link : responses)
            flow->removePlayerResponse(node->id, link.playerNodeId);
        flow->trimActorOutWiresToPortCount(node->id);
        status = "Player responses Off — unlinked wires (choices left in JSON).";
        error.clear();
    }
    else
    {
        status = "Player responses On — 5 out-ports.";
        error.clear();
    }
}

void ActorDialogAuthoringDialog::addPlayerResponseRow()
{
    if (flow == nullptr || node == nullptr)
        return;
    if (!playerResponsesEnabled)
        applyPlayerResponsesToggle(true);
    const int id = flow->addPlayerResponseUnder(node->id);
    if (id < 0)
    {
        error = flow->status.empty() ? "No free player-response port." : flow->status;
        return;
    }
    waitMouseRelease = true;
    if (parchment != nullptr)
    {
        const auto prev = parchment->onClosed;
        parchment->onClosed = [this, prev]() {
            if (prev)
                prev();
            waitMouseRelease = true;
        };
    }
    status = "Player Dialog created and linked.";
    error.clear();
}

bool ActorDialogAuthoringDialog::applyChanges()
{
    commitNumericFocus();
    if (node == nullptr)
    {
        error = "No Actor Dialog node bound.";
        return false;
    }
    lineId = sanitizeLineId(lineId);
    if (lineId.empty())
    {
        error = "Set a phase / line id.";
        return false;
    }
    if (!lineIdIsUnique(lineId))
    {
        error = "Id \"" + lineId + "\" is already used in this scene.";
        return false;
    }
    if (bags[0].ttsAudio.empty())
        bags[0].ttsAudio = defaultBagAudioPath(0);
    if (isOpener && bags[1].ttsAudio.empty()
        && (!bags[1].text.empty() || !bags[1].ttsText.empty()))
        bags[1].ttsAudio = defaultBagAudioPath(1);

    copyDialogToNode(*node);

    if (docs == nullptr)
    {
        error = "Document not wired.";
        return false;
    }
    if (!docs->conversationsLoaded)
        docs->loadConversationsDocument();
    if (!docs->conversationsRoot.is_object())
        docs->conversationsRoot = nlohmann::json::object();

    nlohmann::json* obj = resolveBagObject();
    if (obj == nullptr)
    {
        docs->markDirty();
        status = "Saved editor-local (no JSON pointer yet).";
        error.clear();
        if (onSaved)
            onSaved();
        return true;
    }

    const std::string oldId = loadedLineId;
    (*obj)["id"] = lineId;
    setOrEraseString(*obj, "actorId", actorId);
    setOrEraseString(*obj, "actorName", actorName);

    if (textFieldKey == "intro")
        (*obj)["intro"] = bags[0].text;
    else if (textFieldKey == "response")
        (*obj)["response"] = bags[0].text;
    else
        (*obj)["text"] = bags[0].text;

    const bool hasTts = !bags[0].ttsText.empty() || !bags[0].ttsAudio.empty();
    (*obj)["tts"] = hasTts;
    (*obj)["ttsText"] = bags[0].ttsText;
    setOrEraseString(*obj, "ttsVoice", bags[0].ttsVoice);
    (*obj)["ttsAudio"] = bags[0].ttsAudio.empty() ? defaultBagAudioPath(0) : bags[0].ttsAudio;
    if (bags[0].ttsAudioSegments.size() > 1)
        (*obj)["ttsAudioSegments"] = bags[0].ttsAudioSegments;
    else
        obj->erase("ttsAudioSegments");
    obj->erase("ttsTextSha256");

    if (isOpener)
    {
        setOrEraseString(*obj, "resumeIntro", bags[1].text);
        const bool hasResumeTts = !bags[1].ttsText.empty() || !bags[1].ttsAudio.empty();
        if (hasResumeTts || !bags[1].text.empty())
        {
            (*obj)["resumeTts"] = hasResumeTts;
            setOrEraseString(*obj, "resumeTtsText", bags[1].ttsText);
            setOrEraseString(*obj, "resumeTtsVoice", bags[1].ttsVoice);
            if (!bags[1].ttsAudio.empty())
                (*obj)["resumeTtsAudio"] = bags[1].ttsAudio;
            obj->erase("resumeTtsTextSha256");
        }
    }

    nlohmann::json st = statusToJson(
        statusHealth,
        statusEnergy,
        statusResolve,
        statusLucidity,
        statusCharisma,
        statusMoney,
        statusActor,
        statusActorOpinion,
        statusActorTab,
        statusKey,
        statusOnZeroLucidity);
    if (st.empty())
        obj->erase("status");
    else
        (*obj)["status"] = st;

    setOrEraseFloat(*obj, "requiresMoney", requiresMoney);
    setOrEraseFloat(*obj, "requiresInsufficientMoney", requiresInsufficientMoney);
    setOrEraseBool(*obj, "requiresPositiveActorTab", requiresPositiveActorTab);
    setOrEraseBool(*obj, "requiresPayActorTabInFull", requiresPayActorTabInFull);
    setOrEraseBool(*obj, "requiresInsufficientForActorTab", requiresInsufficientForActorTab);
    setOrEraseBool(*obj, "requiresSaloonRoomAvailable", requiresSaloonRoomAvailable);
    setOrEraseString(*obj, "tabActor", tabActor);
    setOrEraseBool(*obj, "requiresLightSource", requiresLightSource);
    setOrEraseString(*obj, "requiresInventoryItem", requiresInventoryItem);
    setOrEraseString(*obj, "requiresFlag", requiresFlag);
    setOrEraseString(*obj, "requiresPhase", requiresPhase);
    setOrEraseInt(*obj, "minDay", minDay);
    setOrEraseInt(*obj, "maxDay", maxDay);
    setOrEraseFloat(*obj, "requiresLucidityBelow", requiresLucidityBelow);
    setOrEraseFloat(*obj, "requiresLucidityAbove", requiresLucidityAbove);
    setOrEraseBool(*obj, "requiresRecentCollapse", requiresRecentCollapse);
    setOrEraseBool(*obj, "requiresRecentSleep", requiresRecentSleep);
    if (!requiresDaysSinceFlag.empty())
    {
        nlohmann::json since = nlohmann::json::object();
        since["flag"] = requiresDaysSinceFlag;
        if (requiresDaysSinceMin > 0)
            since["min"] = requiresDaysSinceMin;
        if (requiresDaysSinceMax > 0)
            since["max"] = requiresDaysSinceMax;
        (*obj)["requiresDaysSince"] = since;
        (*obj)["requiresDaysSinceFlag"] = requiresDaysSinceFlag;
        setOrEraseInt(*obj, "requiresDaysSinceMin", requiresDaysSinceMin);
        setOrEraseInt(*obj, "requiresDaysSinceMax", requiresDaysSinceMax);
    }
    else
    {
        obj->erase("requiresDaysSince");
        obj->erase("requiresDaysSinceFlag");
        obj->erase("requiresDaysSinceMin");
        obj->erase("requiresDaysSinceMax");
    }
    setOrEraseString(*obj, "opinionActor", opinionActor);
    if (hasActorOpinionAtLeast)
        (*obj)["requiresActorOpinionAtLeast"] = requiresActorOpinionAtLeast;
    else
        obj->erase("requiresActorOpinionAtLeast");
    if (hasActorOpinionAtMost)
        (*obj)["requiresActorOpinionAtMost"] = requiresActorOpinionAtMost;
    else
        obj->erase("requiresActorOpinionAtMost");

    (*obj)["playerResponsesEnabled"] = playerResponsesEnabled;

    if (!oldId.empty() && oldId != lineId && docs->conversationsRoot.contains(sceneId)
        && docs->conversationsRoot[sceneId].is_object()
        && docs->conversationsRoot[sceneId].contains("speakPhases"))
    {
        rewritePhaseRefs(docs->conversationsRoot[sceneId]["speakPhases"], oldId, lineId);
    }
    loadedLineId = lineId;

    docs->markDirty();
    status = "Saved Actor Dialog " + lineId;
    error.clear();
    if (onSaved)
        onSaved();
    return true;
}

bool ActorDialogAuthoringDialog::focusedIsNumeric() const
{
    return (focusField >= kFocusHealth && focusField <= kFocusActorTab)
        || (focusField >= kFocusReqMoney && focusField <= kFocusLucAbove)
        || focusField == kFocusDaysMin || focusField == kFocusDaysMax
        || focusField == kFocusOpinionAtLeast || focusField == kFocusOpinionAtMost;
}

void ActorDialogAuthoringDialog::beginNumericFocus(int field, const std::string& initial)
{
    commitNumericFocus();
    focusField = field;
    focusDraft = initial;
    itemSuggestOpen = false;
    voiceMenuOpen = false;
}

void ActorDialogAuthoringDialog::commitNumericFocus()
{
    if (!focusedIsNumeric())
        return;
    const char* s = focusDraft.c_str();
    auto asInt = [&]() { return static_cast<int>(std::strtol(s, nullptr, 10)); };
    auto asFloat = [&]() { return std::strtof(s, nullptr); };
    switch (focusField)
    {
    case kFocusHealth:
        statusHealth = asFloat();
        break;
    case kFocusEnergy:
        statusEnergy = asFloat();
        break;
    case kFocusResolve:
        statusResolve = asFloat();
        break;
    case kFocusLucidity:
        statusLucidity = asFloat();
        break;
    case kFocusCharisma:
        statusCharisma = asFloat();
        break;
    case kFocusMoney:
        statusMoney = asFloat();
        break;
    case kFocusActorOpinion:
        statusActorOpinion = asInt();
        break;
    case kFocusActorTab:
        statusActorTab = asFloat();
        break;
    case kFocusReqMoney:
        requiresMoney = std::max(0.0f, asFloat());
        break;
    case kFocusInsuffMoney:
        requiresInsufficientMoney = std::max(0.0f, asFloat());
        break;
    case kFocusMinDay:
        minDay = std::max(0, asInt());
        break;
    case kFocusMaxDay:
        maxDay = std::max(0, asInt());
        break;
    case kFocusLucBelow:
        requiresLucidityBelow = std::max(0.0f, asFloat());
        break;
    case kFocusLucAbove:
        requiresLucidityAbove = std::max(0.0f, asFloat());
        break;
    case kFocusDaysMin:
        requiresDaysSinceMin = std::max(0, asInt());
        break;
    case kFocusDaysMax:
        requiresDaysSinceMax = std::max(0, asInt());
        break;
    case kFocusOpinionAtLeast:
        requiresActorOpinionAtLeast = asInt();
        hasActorOpinionAtLeast = true;
        break;
    case kFocusOpinionAtMost:
        requiresActorOpinionAtMost = asInt();
        hasActorOpinionAtMost = true;
        break;
    default:
        break;
    }
    focusDraft.clear();
}

std::string* ActorDialogAuthoringDialog::focusedString()
{
    switch (focusField)
    {
    case kFocusLineId:
        return &lineId;
    case kFocusActorId:
        return &actorId;
    case kFocusActorName:
        return &actorName;
    case kFocusBagText:
        return &activeBag().text;
    case kFocusBagTts:
        return &activeBag().ttsText;
    case kFocusTabActor:
        return &tabActor;
    case kFocusInventory:
        return &requiresInventoryItem;
    case kFocusStoryFlag:
        return &requiresFlag;
    case kFocusRequiresPhase:
        return &requiresPhase;
    case kFocusStatusActor:
        return &statusActor;
    case kFocusStatusKey:
        return &statusKey;
    case kFocusOnZeroLucidity:
        return &statusOnZeroLucidity;
    case kFocusDaysFlag:
        return &requiresDaysSinceFlag;
    case kFocusOpinionActor:
        return &opinionActor;
    default:
        return nullptr;
    }
}

void ActorDialogAuthoringDialog::typeIntoFocusedField()
{
    if (focusedIsNumeric())
    {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_ESCAPE))
        {
            if (IsKeyPressed(KEY_ESCAPE))
            {
                focusDraft.clear();
                focusField = kFocusLineId;
            }
            else
                commitNumericFocus();
            return;
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        {
            if (!focusDraft.empty())
                focusDraft.pop_back();
            return;
        }
        const bool allowMinus = (focusField >= kFocusHealth && focusField <= kFocusActorTab)
            || focusField == kFocusOpinionAtLeast || focusField == kFocusOpinionAtMost;
        const bool allowDot = (focusField >= kFocusHealth && focusField <= kFocusMoney)
            || focusField == kFocusActorTab || focusField == kFocusReqMoney
            || focusField == kFocusInsuffMoney || focusField == kFocusLucBelow
            || focusField == kFocusLucAbove;
        int ch = GetCharPressed();
        while (ch > 0)
        {
            if (ch >= '0' && ch <= '9')
                focusDraft.push_back(static_cast<char>(ch));
            else if (allowDot && ch == '.' && focusDraft.find('.') == std::string::npos)
                focusDraft.push_back('.');
            else if (allowMinus && ch == '-' && focusDraft.empty())
                focusDraft.push_back('-');
            ch = GetCharPressed();
        }
        return;
    }

    std::string* buffer = focusedString();
    if (buffer == nullptr)
        return;
    const bool multiline = (focusField == kFocusBagText || focusField == kFocusBagTts);
    if (focusField == kFocusInventory && itemSuggestOpen && IsKeyPressed(KEY_TAB))
    {
        const std::vector<std::string> suggestions = inventoryItemSuggestions(8);
        if (!suggestions.empty())
            applyInventoryItemSuggestion(suggestions.front());
        return;
    }
    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (cp == '\n' || cp == '\r')
        {
            if (multiline)
                buffer->push_back('\n');
        }
        else if (cp >= 32)
        {
            insertUtf8(*buffer, cp);
            if (focusField == kFocusInventory)
                itemSuggestOpen = true;
        }
        cp = GetCharPressed();
    }
    if (multiline
        && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)
            || IsKeyPressedRepeat(KEY_ENTER) || IsKeyPressedRepeat(KEY_KP_ENTER)))
        buffer->push_back('\n');
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
    {
        backspaceUtf8(*buffer);
        if (focusField == kFocusInventory)
            itemSuggestOpen = true;
    }
    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            buffer->append(clip);
            if (focusField == kFocusInventory)
                itemSuggestOpen = true;
        }
    }
}

std::string ActorDialogAuthoringDialog::inventoryItemTokenPrefix() const
{
    const std::string& s = requiresInventoryItem;
    const size_t comma = s.find_last_of(",;\n");
    std::string token = (comma == std::string::npos) ? s : s.substr(comma + 1);
    size_t a = 0;
    while (a < token.size()
           && (token[a] == ' ' || token[a] == '\t' || token[a] == '\r'))
        ++a;
    return token.substr(a);
}

void ActorDialogAuthoringDialog::applyInventoryItemSuggestion(const std::string& itemId)
{
    if (itemId.empty())
        return;
    const std::string& s = requiresInventoryItem;
    const size_t comma = s.find_last_of(",;\n");
    if (comma == std::string::npos)
        requiresInventoryItem = itemId;
    else
    {
        std::string head = s.substr(0, comma + 1);
        if (head.empty() || (head.back() != ' ' && head.back() != '\t'))
            head.push_back(' ');
        requiresInventoryItem = head + itemId;
    }
    itemSuggestOpen = false;
}

std::vector<std::string> ActorDialogAuthoringDialog::inventoryItemSuggestions(int maxCount) const
{
    std::vector<std::string> out;
    if (docs == nullptr || maxCount <= 0)
        return out;
    if (!docs->itemsLoaded)
        const_cast<DocumentWorkspace*>(docs)->loadItemsDocument();
    const std::string prefix = inventoryItemTokenPrefix();
    std::string prefixLower = prefix;
    for (char& ch : prefixLower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const std::vector<std::string> ids = docs->itemIds();
    for (const std::string& id : ids)
    {
        if (prefixLower.empty())
            out.push_back(id);
        else
        {
            std::string idLower = id;
            for (char& ch : idLower)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (idLower.rfind(prefixLower, 0) != 0
                && idLower.find(prefixLower) == std::string::npos)
                continue;
            out.push_back(id);
        }
        if (static_cast<int>(out.size()) >= maxCount * 3)
            break;
    }
    std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
        std::string al = a;
        std::string bl = b;
        for (char& ch : al)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (char& ch : bl)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const bool ap = !prefixLower.empty() && al.rfind(prefixLower, 0) == 0;
        const bool bp = !prefixLower.empty() && bl.rfind(prefixLower, 0) == 0;
        if (ap != bp)
            return ap;
        return al < bl;
    });
    if (static_cast<int>(out.size()) > maxCount)
        out.resize(static_cast<size_t>(maxCount));
    return out;
}

void ActorDialogAuthoringDialog::stopPreviewVoice()
{
    if (previewVoiceLoaded && IsMusicValid(previewVoice))
    {
        if (IsMusicStreamPlaying(previewVoice))
            StopMusicStream(previewVoice);
        UnloadMusicStream(previewVoice);
    }
    previewVoice = {};
    previewVoiceLoaded = false;
    previewVoicePlaying = false;
    if (!previewVoiceTempFile.empty())
    {
        std::remove(previewVoiceTempFile.c_str());
        previewVoiceTempFile.clear();
    }
}

void ActorDialogAuthoringDialog::updatePreviewVoice()
{
    if (!previewVoiceLoaded || !previewVoicePlaying)
        return;
    if (!IsMusicValid(previewVoice))
    {
        stopPreviewVoice();
        return;
    }
    UpdateMusicStream(previewVoice);
    if (!IsMusicStreamPlaying(previewVoice))
        stopPreviewVoice();
}

void ActorDialogAuthoringDialog::startPreviewVoice()
{
    if (docs == nullptr || generateBusy.load())
        return;
    const int bagIndex = (isOpener && selectedBag == 1) ? 1 : 0;
    if (!bagAudioExists(bagIndex))
    {
        error = "No TTS audio on disk yet — Generate voice first.";
        return;
    }
    stopPreviewVoice();
    if (!editorAudioDeviceReady())
    {
        error = "Audio device not ready.";
        return;
    }
    const std::string rel = bagAudioPath(bagIndex);
    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    std::vector<std::string> candidates = buildAssetSearchPaths(assetRoot, rel);
    if (!docs->resourceDir.empty())
    {
        std::string stripped = rel;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(10);
        candidates.push_back(pathJoin(docs->resourceDir, stripped));
    }
    Music music{};
    std::string tempFile;
    bool ok = false;
    for (const std::string& path : candidates)
    {
        if (FileExists(path.c_str()))
        {
            music = LoadMusicStream(path.c_str());
            if (IsMusicValid(music))
            {
                ok = true;
                break;
            }
        }
        const std::string compressed = compressedAssetPath(path);
        if (!FileExists(compressed.c_str()))
            continue;
        std::vector<unsigned char> bytes;
        if (!loadAssetBytesFromFile(compressed, bytes) || bytes.empty())
            continue;
        const std::string tmp = pathJoin(
            GetApplicationDirectory() ? GetApplicationDirectory() : ".",
            "editor_actor_tts_preview.mp3");
        std::ofstream out(tmp.c_str(), std::ios::binary);
        if (!out)
            continue;
        out.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        out.close();
        music = LoadMusicStream(tmp.c_str());
        if (IsMusicValid(music))
        {
            tempFile = tmp;
            ok = true;
            break;
        }
        std::remove(tmp.c_str());
    }
    if (!ok)
    {
        error = "Could not load Actor Dialog TTS audio.";
        return;
    }
    music.looping = false;
    previewVoice = music;
    previewVoiceLoaded = true;
    previewVoicePlaying = true;
    previewVoiceTempFile = tempFile;
    SetMusicVolume(previewVoice, 1.0f);
    PlayMusicStream(previewVoice);
    status = std::string("Previewing ") + bagSlotLabel(bagIndex, isOpener) + " TTS…";
    error.clear();
}

void ActorDialogAuthoringDialog::openParchmentForBag(bool tts)
{
    if (parchment == nullptr || docs == nullptr)
        return;
    ActorDialogBagEdit& bag = activeBag();
    const int bagIndex = (isOpener && selectedBag == 1) ? 1 : 0;
    if (tts && bag.ttsAudio.empty())
        bag.ttsAudio = defaultBagAudioPath(bagIndex);
    if (tts && bag.ttsText.empty() && !bag.text.empty())
        bag.ttsText = bag.text;
    const std::string label = std::string("Actor Dialog ")
        + bagSlotLabel(bagIndex, isOpener) + (tts ? " TTS" : " text");
    parchment->openEditor(
        tts ? &bag.ttsText : &bag.text,
        tts,
        label,
        docs->resourceDir,
        docs->assetRoot,
        tts ? bag.text : std::string{},
        tts ? bag.ttsVoice : std::string{},
        tts ? bag.ttsAudio : std::string{},
        tts ? &bag.ttsAudio : nullptr,
        tts ? &bag.ttsAudioSegments : nullptr);
    parchment->onClosed = [this]() { waitMouseRelease = true; };
    waitMouseRelease = true;
}

void ActorDialogAuthoringDialog::startTtsDialogGenerate()
{
    if (docs == nullptr || generateBusy.load())
        return;
    ActorDialogBagEdit& bag = activeBag();
    const int bagIndex = (isOpener && selectedBag == 1) ? 1 : 0;
    const std::string source =
        sceneTtsTextHasWord(bag.ttsText) ? bag.ttsText : bag.text;
    if (!sceneTtsTextHasWord(source))
    {
        error = "Add on-screen text (or TTS text) before Generate TTS dialog.";
        return;
    }
    if (sessionKeys == nullptr || !sessionKeys->xaiReady())
    {
        error = "Options → Configure API keys — set a valid xAI key "
                "before Generate TTS dialog.";
        return;
    }

    const std::string styleBlock = formatGenerationStyleBlock(
        loadGenerationStyleFilter(docs->resourceDir));
    const char* ttsEmbellishRules =
        "Return ONLY the speakable text (no markdown fences, no commentary). "
        "Do NOT rewrite plot, add new facts, or change meaning. Allowed edits only: "
        "spelling fixes, grammar fixes, and light TTS life-like markup "
        "([pause], [long-pause], [sigh], [laugh], <whisper>, <soft>, <emphasis>, "
        "<slow>, <fast>, {{voice:ID}}...{{/voice}} for quoted speech). "
        "Keep 1890s Highline Ridge tone. Narrator prose stays unwrapped.\n";
    const std::string prompt =
        std::string("Prepare the following Actor Dialog ")
        + bagSlotLabel(bagIndex, isOpener)
        + " text as spoken Timberline TTS narration. " + ttsEmbellishRules
        + styleBlock + "\nSOURCE:\n" + source;

    const std::string root = docs->assetRoot.empty() ? "." : docs->assetRoot;
    const std::string authoringDir =
        pathJoin(pathJoin(root, "resources"), ".authoring");
#if !defined(_WIN32)
    const std::string mkdirCmd = "mkdir -p '" + authoringDir + "'";
    std::system(mkdirCmd.c_str());
#endif
    const std::string stem = sceneId + "_actor_"
        + (lineId.empty() ? std::string("line") : sanitizeLineId(lineId))
        + (bagIndex == 1 ? "_resume" : "");
    const std::string outRel = "resources/.authoring/" + stem + "_tts.txt";
    pendingTtsJobsPath = pathJoin(authoringDir, stem + "_tts_jobs.json");

    nlohmann::json jobsRoot;
    jobsRoot["sceneId"] = sceneId;
    jobsRoot["itemId"] = sceneId;
    jobsRoot["kind"] = "actor_tts";
    jobsRoot["jobs"] = nlohmann::json::array(
        {nlohmann::json{
            {"type", "generate_scene_description_tts_text"},
            {"prompt", prompt},
            {"outPath", outRel},
            {"action", "actor_tts"},
            {"sourceText", source}}});
    {
        std::ofstream out(pendingTtsJobsPath.c_str());
        if (!out)
        {
            error = "Failed to write TTS jobs file.";
            pendingTtsJobsPath.clear();
            return;
        }
        out << jobsRoot.dump(2);
    }

    generateCancel.store(false);
    generateBusy = true;
    generateKind = 1;
    generateBag = bagIndex;
    generateResultPending = false;
    status = std::string("Generating ") + bagSlotLabel(bagIndex, isOpener) + " TTS dialog…";
    error.clear();
    const std::string keySnap = effectiveApiKey();
    const std::string jobsSnap = pendingTtsJobsPath;
    const std::string assetRoot = docs->assetRoot;
    const std::string resourceDir = docs->resourceDir;
    joinGenerateThread();
    generateThread = std::thread([this, keySnap, jobsSnap, assetRoot, resourceDir]() {
        const std::string msg = runSceneAuthoringAiJobsFile(
            assetRoot, resourceDir, jobsSnap, keySnap, &generateCancel);
        std::lock_guard<std::mutex> lock(generateMutex);
        generateResultStatus = msg;
        generateResultPending = true;
    });
}

void ActorDialogAuthoringDialog::startVoiceGenerate()
{
    if (docs == nullptr || generateBusy.load())
        return;
    ActorDialogBagEdit& bag = activeBag();
    const int bagIndex = (isOpener && selectedBag == 1) ? 1 : 0;
    if (!sceneTtsTextHasWord(bag.ttsText))
    {
        error = "Add Actor Dialog TTS text before generating voice.";
        return;
    }
    if (bag.ttsVoice.empty())
    {
        error = "Pick a voice before Generate voice.";
        return;
    }
    if (sessionKeys == nullptr || !sessionKeys->xaiReady())
    {
        error = "Options → Configure API keys — set a valid xAI key "
                "before generating voice.";
        return;
    }
    if (bag.ttsAudio.empty())
        bag.ttsAudio = defaultBagAudioPath(bagIndex);
    if (!applyChanges())
        return;

    generateCancel.store(false);
    generateBusy = true;
    generateKind = 2;
    generateBag = bagIndex;
    generateResultPending = false;
    generateResultPath.clear();
    generateResultSegments.clear();
    status = std::string("Generating ") + bagSlotLabel(bagIndex, isOpener) + " voice…";
    error.clear();
    const std::string keySnap = effectiveApiKey();
    const std::string assetRoot = docs->assetRoot;
    const std::string resourceDir = docs->resourceDir;
    const std::string voiceSnap = bag.ttsVoice;
    const std::string audioSnap = bag.ttsAudio;
    const std::string textSnap = bag.ttsText;
    joinGenerateThread();
    generateThread = std::thread(
        [this, keySnap, assetRoot, resourceDir, voiceSnap, audioSnap, textSnap]() {
            std::string written;
            std::vector<std::string> segments;
            const std::string err = runSynthesizeTtsAudio(
                assetRoot,
                resourceDir,
                textSnap,
                voiceSnap,
                keySnap,
                audioSnap,
                written,
                &segments);
            std::lock_guard<std::mutex> lock(generateMutex);
            if (!err.empty())
            {
                generateResultStatus = err;
                generateResultPath.clear();
                generateResultSegments.clear();
            }
            else
            {
                generateResultStatus = "Voice written.";
                generateResultPath = written;
                generateResultSegments = std::move(segments);
            }
            generateResultPending = true;
        });
}

void ActorDialogAuthoringDialog::pollGenerateResult()
{
    updatePreviewVoice();
    if (!generateResultPending)
        return;
    std::lock_guard<std::mutex> lock(generateMutex);
    if (!generateResultPending)
        return;
    const int kind = generateKind;
    const int bagIndex = generateBag;
    status = generateResultStatus;
    generateResultPending = false;
    generateBusy = false;
    generateKind = 0;

    if (kind == 1 && !pendingTtsJobsPath.empty())
    {
        try
        {
            std::ifstream jobsIn(pendingTtsJobsPath.c_str());
            if (jobsIn)
            {
                nlohmann::json root;
                jobsIn >> root;
                if (root.contains("jobs") && root["jobs"].is_array())
                {
                    for (const auto& job : root["jobs"])
                    {
                        if (!job.is_object())
                            continue;
                        std::string text = job.value("resultText", "");
                        if (text.empty())
                        {
                            const std::string outRel = job.value("outPath", "");
                            if (!outRel.empty() && docs != nullptr)
                            {
                                const std::string rootDir =
                                    docs->assetRoot.empty() ? "." : docs->assetRoot;
                                std::ifstream tf(pathJoin(rootDir, outRel).c_str());
                                if (tf)
                                    text.assign(
                                        (std::istreambuf_iterator<char>(tf)),
                                        std::istreambuf_iterator<char>());
                            }
                        }
                        while (!text.empty()
                               && (text.back() == '\n' || text.back() == '\r'
                                   || text.back() == ' ' || text.back() == '\t'))
                            text.pop_back();
                        if (!text.empty() && (bagIndex == 0 || bagIndex == 1))
                        {
                            bags[bagIndex].ttsText = text;
                            if (bags[bagIndex].ttsAudio.empty())
                                bags[bagIndex].ttsAudio = defaultBagAudioPath(bagIndex);
                            (void)applyChanges();
                            status = std::string(bagSlotLabel(bagIndex, isOpener))
                                + " TTS dialog ready — Generate voice next.";
                        }
                    }
                }
            }
        }
        catch (const nlohmann::json::exception&)
        {
        }
        pendingTtsJobsPath.clear();
        return;
    }

    if (kind == 2 && (bagIndex == 0 || bagIndex == 1))
    {
        if (!generateResultPath.empty())
        {
            bags[bagIndex].ttsAudio = generateResultPath;
            bags[bagIndex].ttsAudioSegments = generateResultSegments;
            (void)applyChanges();
            status = std::string(bagSlotLabel(bagIndex, isOpener)) + " voice ready.";
        }
        else if (error.empty() && status.find("Voice") == std::string::npos)
            error = status;
        generateResultPath.clear();
        generateResultSegments.clear();
    }
}

void ActorDialogAuthoringDialog::handleInput(int screenW, int screenH)
{
    (void)screenW;
    (void)screenH;
    if (!open)
        return;
    pollGenerateResult();
    if (parchment != nullptr && parchment->blocksInput())
        return;
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }
    if (waitMouseRelease)
    {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            waitMouseRelease = false;
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE))
    {
        closeDialog();
        return;
    }
    {
        const Vector2 mouse = GetMousePosition();
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && voiceMenuOpen && voiceMenuRect.height > 1.0f
            && CheckCollisionPointRec(mouse, voiceMenuRect))
        {
            const int voiceCount = static_cast<int>(builtinVoiceIds().size());
            const int visible = std::min(kVoiceMenuVisibleRows, voiceCount);
            const float maxScroll =
                static_cast<float>(std::max(0, voiceCount - visible));
            voiceMenuScroll = std::clamp(voiceMenuScroll - wheel, 0.0f, maxScroll);
        }
        else if (wheel != 0.0f && lastScrollClip.width > 1.0f
                 && CheckCollisionPointRec(mouse, lastScrollClip))
        {
            const float maxScroll =
                std::max(0.0f, lastContentH - lastScrollClip.height);
            scrollY = std::clamp(scrollY - wheel * 28.0f, 0.0f, maxScroll);
        }
    }
    typeIntoFocusedField();
}

void ActorDialogAuthoringDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;
    pollGenerateResult();

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool busy = generateBusy.load();
    const bool canClick =
        !waitMouseRelease && ignoreInputFrames <= 0 && !busy
        && (parchment == nullptr || !parchment->blocksInput())
        && editorMousePressed(MOUSE_BUTTON_LEFT);

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);
    const float dialogW = std::min(760.0f, screenW - 32.0f);
    const float dialogH = std::min(720.0f, screenH - 24.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f, (screenH - dialogH) * 0.5f, dialogW, dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    const float pad = 16.0f;
    const float fieldX = dialog.x + pad;
    const float fieldW = dialog.width - pad * 2.0f;
    float y = dialog.y + pad;

    DrawTextEx(bold, "Edit Actor Dialog", {fieldX, y}, kFontHeading, 1.0f, kTextPrimary);
    y += 28.0f;
    DrawTextEx(
        font,
        sceneId.empty() ? "(no scene)" : sceneId.c_str(),
        {fieldX, y},
        kFontTiny,
        1.0f,
        kTextMuted);
    y += 18.0f;

    const float footerH = 52.0f;
    const Rectangle scrollClip = {
        fieldX, y, fieldW, dialog.y + dialogH - footerH - y};
    lastScrollClip = scrollClip;

    float contentY = 0.0f;
    auto advance = [&](float dy) { contentY += dy; };
    auto rowY = [&]() { return scrollClip.y + contentY - scrollY; };

    BeginScissorMode(
        (int)scrollClip.x,
        (int)scrollClip.y,
        (int)scrollClip.width,
        (int)scrollClip.height);

    auto reenterScroll = [&]() {
        BeginScissorMode(
            (int)scrollClip.x,
            (int)scrollClip.y,
            (int)scrollClip.width,
            (int)scrollClip.height);
    };

    auto stringField =
        [&](int fieldId, const char* label, std::string& value, const char* placeholder) {
            DrawTextEx(font, label, {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
            advance(16.0f);
            Rectangle field = {fieldX, rowY(), fieldW, 28.0f};
            if (fieldId == kFocusInventory)
                itemFieldRect = field;
            DrawRectangleRec(field, Color{24, 22, 32, 255});
            DrawRectangleLinesEx(
                field,
                focusField == fieldId ? 2.0f : 1.0f,
                focusField == fieldId ? kPanelBorder : kPanelInnerEdge);
            drawClippedFieldText(
                font,
                field,
                value,
                placeholder,
                kFontSmall,
                value.empty() ? kTextMuted : kTextPrimary);
            reenterScroll();
            if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                && CheckCollisionPointRec(mouse, field))
            {
                commitNumericFocus();
                focusField = fieldId;
                itemSuggestOpen = (fieldId == kFocusInventory);
                if (fieldId == kFocusInventory && docs != nullptr && !docs->itemsLoaded)
                    docs->loadItemsDocument();
            }
            advance(36.0f);
        };

    auto numericField =
        [&](int fieldId, const char* label, const std::string& shown, float width = 80.0f) {
            DrawTextEx(
                font, label, {fieldX, rowY() + 4.0f}, kFontSmall, 1.0f, kTextPrimary);
            Rectangle field = {
                fieldX + fieldW - width, rowY(), width, 26.0f};
            const bool focused = focusField == fieldId;
            DrawRectangleRec(
                field, focused ? Color{50, 48, 62, 255} : Color{24, 22, 32, 255});
            DrawRectangleLinesEx(
                field, focused ? 2.0f : 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
            const std::string text = focused
                ? (focusDraft.empty() ? "|" : focusDraft)
                : shown;
            DrawTextEx(
                font,
                text.c_str(),
                {field.x + 6.0f, field.y + 4.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);
            if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                && CheckCollisionPointRec(mouse, field))
                beginNumericFocus(fieldId, shown);
            advance(30.0f);
        };

    auto rowToggle = [&](const char* description, bool& value) {
        const float rowH = 28.0f;
        const float sw = 44.0f;
        const float sh = 22.0f;
        const Rectangle track = {fieldX, rowY() + 3.0f, sw, sh};
        bool toggled = false;
        drawOnOffSwitch(font, track, value, description, false, toggled);
        const Rectangle hit = {fieldX, rowY(), fieldW, rowH};
        if (canClick && !busy && CheckCollisionPointRec(mouse, scrollClip)
            && CheckCollisionPointRec(mouse, hit))
            value = !value;
        advance(rowH + 4.0f);
    };

    {
        DrawTextEx(font, "Phase / line id", {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
        advance(16.0f);
        const float renameW = 88.0f;
        Rectangle idField = {fieldX, rowY(), fieldW - renameW - 8.0f, 28.0f};
        DrawRectangleRec(idField, Color{24, 22, 32, 255});
        DrawRectangleLinesEx(
            idField,
            focusField == kFocusLineId ? 2.0f : 1.0f,
            focusField == kFocusLineId ? kPanelBorder : kPanelInnerEdge);
        drawClippedFieldText(
            font,
            idField,
            lineId,
            "(unique within scene speakPhases / lines)",
            kFontSmall,
            lineId.empty() ? kTextMuted : kTextPrimary);
        reenterScroll();
        if (canClick && CheckCollisionPointRec(mouse, scrollClip)
            && CheckCollisionPointRec(mouse, idField))
        {
            commitNumericFocus();
            focusField = kFocusLineId;
        }
        Rectangle renameBtn = {idField.x + idField.width + 8.0f, rowY(), renameW, 28.0f};
        drawEditorButton(font, renameBtn, "Rename", true, !busy);
        if (canClick && CheckCollisionPointRec(mouse, scrollClip)
            && CheckCollisionPointRec(mouse, renameBtn))
            applyRename();
        advance(36.0f);
    }

    stringField(kFocusActorId, "Actor id", actorId, "(e.g. bartender)");
    stringField(kFocusActorName, "Actor name", actorName, "(display name)");

    DrawTextEx(bold, "Dialog", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);

    if (isOpener)
    {
        const float tabW = (fieldW - 8.0f) / 2.0f;
        for (int i = 0; i < 2; ++i)
        {
            Rectangle tab = {fieldX + static_cast<float>(i) * (tabW + 8.0f), rowY(), tabW, 26.0f};
            const bool on = selectedBag == i;
            drawEditorButton(font, tab, bagSlotLabel(i, true), on, true);
            if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                && CheckCollisionPointRec(mouse, tab))
            {
                commitNumericFocus();
                selectedBag = i;
                focusField = kFocusBagText;
                stopPreviewVoice();
            }
        }
        advance(34.0f);
    }
    else
        selectedBag = 0;

    DrawTextEx(font, "On-screen text", {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
    advance(16.0f);
    Rectangle textField = {fieldX, rowY(), fieldW, 72.0f};
    DrawRectangleRec(textField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        textField,
        focusField == kFocusBagText ? 2.0f : 1.0f,
        focusField == kFocusBagText ? kPanelBorder : kPanelInnerEdge);
    BeginScissorMode(
        (int)textField.x + 2,
        (int)textField.y + 2,
        (int)textField.width - 4,
        (int)textField.height - 4);
    DrawTextEx(
        font,
        activeBag().text.empty() ? "(empty)" : activeBag().text.c_str(),
        {textField.x + 8.0f, textField.y + 6.0f},
        kFontSmall,
        1.0f,
        activeBag().text.empty() ? kTextMuted : kTextPrimary);
    EndScissorMode();
    reenterScroll();
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, textField))
    {
        commitNumericFocus();
        focusField = kFocusBagText;
    }
    advance(80.0f);

    DrawTextEx(font, "TTS text", {fieldX, rowY()}, kFontTiny, 1.0f, kTextMuted);
    advance(16.0f);
    Rectangle ttsField = {fieldX, rowY(), fieldW, 72.0f};
    DrawRectangleRec(ttsField, Color{24, 22, 32, 255});
    DrawRectangleLinesEx(
        ttsField,
        focusField == kFocusBagTts ? 2.0f : 1.0f,
        focusField == kFocusBagTts ? kPanelBorder : kPanelInnerEdge);
    BeginScissorMode(
        (int)ttsField.x + 2,
        (int)ttsField.y + 2,
        (int)ttsField.width - 4,
        (int)ttsField.height - 4);
    DrawTextEx(
        font,
        activeBag().ttsText.empty() ? "(empty — Generate TTS dialog or Edit TTS)"
                                    : activeBag().ttsText.c_str(),
        {ttsField.x + 8.0f, ttsField.y + 6.0f},
        kFontSmall,
        1.0f,
        activeBag().ttsText.empty() ? kTextMuted : kTextPrimary);
    EndScissorMode();
    reenterScroll();
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, ttsField))
    {
        commitNumericFocus();
        focusField = kFocusBagTts;
    }
    advance(80.0f);

    const float btnW = (fieldW - 8.0f) * 0.5f;
    const float btnH = 28.0f;
    Rectangle editTextBtn = {fieldX, rowY(), btnW, btnH};
    Rectangle editTtsBtn = {fieldX + btnW + 8.0f, rowY(), btnW, btnH};
    drawEditorButton(font, editTextBtn, "Edit text", false, !busy);
    drawEditorButton(font, editTtsBtn, "Edit TTS", false, !busy);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, editTextBtn))
        openParchmentForBag(false);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, editTtsBtn))
        openParchmentForBag(true);
    advance(btnH + 8.0f);

    voiceBtnRect = {fieldX, rowY(), btnW, btnH};
    Rectangle genTtsSlot = {fieldX + btnW + 8.0f, rowY(), btnW, btnH};
    Rectangle ttsKeyIcon{};
    Rectangle genTtsBtn = aiFieldWithKeyIcon(genTtsSlot, &ttsKeyIcon);
    const bool xaiOk = sessionKeys != nullptr && sessionKeys->xaiReady();
    const bool canTtsDialog =
        !busy && xaiOk
        && (sceneTtsTextHasWord(activeBag().ttsText)
            || sceneTtsTextHasWord(activeBag().text));
    drawEditorButton(
        font,
        voiceBtnRect,
        ("Voice: " + (activeBag().ttsVoice.empty() ? std::string("Off") : activeBag().ttsVoice))
            .c_str(),
        true,
        !busy);
    drawApiKeyStatusIcon(
        bold,
        ttsKeyIcon,
        sessionKeys != nullptr ? sessionKeys->xaiValidity : ApiKeyValidity::Missing);
    drawEditorButton(
        font,
        genTtsBtn,
        (busy && generateKind == 1) ? "Working..." : "Generate TTS dialog",
        canTtsDialog,
        canTtsDialog);
    if (canClick && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, voiceBtnRect))
    {
        voiceMenuOpen = !voiceMenuOpen;
        if (voiceMenuOpen)
        {
            const auto& voices = builtinVoiceIds();
            const int voiceCount = static_cast<int>(voices.size());
            int selected = 0;
            for (int i = 0; i < voiceCount; ++i)
            {
                if (voices[static_cast<size_t>(i)] == activeBag().ttsVoice)
                {
                    selected = i;
                    break;
                }
            }
            voiceMenuScroll = static_cast<float>(
                std::max(0, selected - kVoiceMenuVisibleRows / 2));
        }
    }
    if (canClick && canTtsDialog && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, genTtsBtn))
        startTtsDialogGenerate();
    advance(btnH + 8.0f);

    Rectangle genVoiceSlot = {fieldX, rowY(), btnW, btnH};
    Rectangle voiceKeyIcon{};
    Rectangle genVoiceBtn = aiFieldWithKeyIcon(genVoiceSlot, &voiceKeyIcon);
    Rectangle previewBtn = {fieldX + btnW + 8.0f, rowY(), btnW, btnH};
    const int bagIndex = (isOpener && selectedBag == 1) ? 1 : 0;
    const bool canGen =
        !busy && sceneTtsTextHasWord(activeBag().ttsText) && xaiOk
        && !activeBag().ttsVoice.empty();
    const bool canPrev = !busy && bagAudioExists(bagIndex);
    drawApiKeyStatusIcon(
        bold,
        voiceKeyIcon,
        sessionKeys != nullptr ? sessionKeys->xaiValidity : ApiKeyValidity::Missing);
    drawEditorButton(
        font,
        genVoiceBtn,
        (busy && generateKind == 2) ? "Working..." : "Generate voice",
        canGen,
        canGen);
    drawEditorButton(
        font,
        previewBtn,
        previewVoicePlaying ? "Stop preview" : "Play preview",
        true,
        canPrev);
    if (canClick && canGen && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, genVoiceBtn))
        startVoiceGenerate();
    if (canClick && canPrev && CheckCollisionPointRec(mouse, scrollClip)
        && CheckCollisionPointRec(mouse, previewBtn))
    {
        if (previewVoicePlaying)
            stopPreviewVoice();
        else
            startPreviewVoice();
    }
    advance(btnH + 8.0f);
    DrawTextEx(
        font,
        bagAudioPath(bagIndex).c_str(),
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(18.0f);

    DrawTextEx(bold, "Player responses", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);
    {
        const float rowH = 28.0f;
        const float sw = 44.0f;
        const float sh = 22.0f;
        const Rectangle track = {fieldX, rowY() + 3.0f, sw, sh};
        bool toggled = false;
        drawOnOffSwitch(
            font, track, playerResponsesEnabled, "Player responses", false, toggled);
        const float plusW = 28.0f;
        Rectangle plusBtn = {
            fieldX + fieldW - plusW, rowY(), plusW, rowH};
        if (playerResponsesEnabled)
        {
            drawEditorButton(font, plusBtn, "+", true, !busy);
            if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                && CheckCollisionPointRec(mouse, plusBtn))
                addPlayerResponseRow();
        }
        const Rectangle hit = {
            fieldX, rowY(), fieldW - (playerResponsesEnabled ? plusW + 8.0f : 0.0f), rowH};
        if (canClick && !busy && CheckCollisionPointRec(mouse, scrollClip)
            && CheckCollisionPointRec(mouse, hit))
            applyPlayerResponsesToggle(!playerResponsesEnabled);
        advance(rowH + 6.0f);
        DrawTextEx(
            font,
            playerResponsesEnabled ? "On → 5 out-ports. Off → 1 bottom port."
                                   : "Off → 1 bottom out-port.",
            {fieldX, rowY()},
            kFontTiny,
            1.0f,
            kTextMuted);
        advance(16.0f);
    }
    if (playerResponsesEnabled && flow != nullptr && node != nullptr)
    {
        const auto responses = flow->playerResponsesUnder(node->id);
        if (responses.empty())
        {
            DrawTextEx(
                font,
                "(none — click + to add)",
                {fieldX, rowY()},
                kFontSmall,
                1.0f,
                kTextMuted);
            advance(22.0f);
        }
        else
        {
            for (const auto& link : responses)
            {
                const DialogFlowNode* player = flow->findNode(link.playerNodeId);
                if (player == nullptr)
                    continue;
                const Color swatch = dialogActorResponsePortColor(link.childIndex);
                const float cy = rowY() + 10.0f;
                DrawCircleV({fieldX + 7.0f, cy}, 6.0f, swatch);
                DrawCircleLines((int)(fieldX + 7.0f), (int)cy, 6.0f, Color{30, 28, 36, 255});
                std::string preview = player->playerDialogText.empty()
                    ? "(empty)"
                    : player->playerDialogText;
                if (preview.find('\n') != std::string::npos)
                    preview = preview.substr(0, preview.find('\n'));
                const float xW = 22.0f;
                const float textMax = fieldW - 28.0f - xW - 8.0f;
                preview = ellipsizeUi(font, preview, kFontSmall, textMax);
                DrawTextEx(
                    font,
                    preview.c_str(),
                    {fieldX + 18.0f, rowY() + 2.0f},
                    kFontSmall,
                    1.0f,
                    player->playerDialogText.empty() ? kTextMuted : kTextPrimary);
                Rectangle xRect = {fieldX + fieldW - xW, rowY(), xW, 22.0f};
                DrawTextEx(
                    bold,
                    "X",
                    {xRect.x + 4.0f, xRect.y + 1.0f},
                    kFontSmall,
                    1.0f,
                    Color{210, 90, 90, 255});
                if (canClick && CheckCollisionPointRec(mouse, scrollClip)
                    && CheckCollisionPointRec(mouse, xRect))
                    flow->removePlayerResponse(node->id, player->id);
                advance(24.0f);
            }
        }
    }

    DrawTextEx(bold, "Status deltas", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);
    DrawTextEx(
        font,
        "Applied when this line plays. H/E/R/L/C/money plus actor opinion/tab.",
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(18.0f);
    numericField(kFocusHealth, "Health", formatFloat(statusHealth));
    numericField(kFocusEnergy, "Energy", formatFloat(statusEnergy));
    numericField(kFocusResolve, "Resolve", formatFloat(statusResolve));
    numericField(kFocusLucidity, "Lucidity", formatFloat(statusLucidity));
    numericField(kFocusCharisma, "Charisma", formatFloat(statusCharisma));
    numericField(kFocusMoney, "Money", formatFloat(statusMoney));
    stringField(kFocusStatusActor, "Status actor", statusActor, "(for opinion / tab)");
    numericField(kFocusActorOpinion, "Actor opinion", formatSignedInt(statusActorOpinion));
    numericField(kFocusActorTab, "Actor tab", formatFloat(statusActorTab));
    stringField(kFocusStatusKey, "Once key", statusKey, "(status.key)");
    stringField(
        kFocusOnZeroLucidity, "On zero lucidity", statusOnZeroLucidity, "(e.g. restart_cabin)");

    DrawTextEx(bold, "Availability", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);
    DrawTextEx(
        font,
        "Same money / tab / room / light / item / flag gates as Speak choices.",
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(18.0f);
    numericField(kFocusReqMoney, "Requires money ($)", formatFloat(requiresMoney));
    numericField(
        kFocusInsuffMoney,
        "Requires insufficient money ($)",
        formatFloat(requiresInsufficientMoney));
    rowToggle("Requires positive actor tab", requiresPositiveActorTab);
    rowToggle("Requires pay actor tab in full", requiresPayActorTabInFull);
    rowToggle("Requires insufficient for actor tab", requiresInsufficientForActorTab);
    rowToggle("Requires saloon room available", requiresSaloonRoomAvailable);
    stringField(kFocusTabActor, "Tab actor", tabActor, "(e.g. bartender)");
    rowToggle("Requires a light source", requiresLightSource);
    stringField(
        kFocusInventory,
        "Requires inventory item id(s)",
        requiresInventoryItem,
        "(none — type to autocomplete)");
    stringField(kFocusStoryFlag, "Requires story flag", requiresFlag, "(none)");
    stringField(kFocusRequiresPhase, "Requires phase", requiresPhase, "(none)");
    numericField(kFocusMinDay, "Min day", std::to_string(minDay));
    numericField(kFocusMaxDay, "Max day", std::to_string(maxDay));
    numericField(kFocusLucBelow, "Requires lucidity below", formatFloat(requiresLucidityBelow));
    numericField(kFocusLucAbove, "Requires lucidity above", formatFloat(requiresLucidityAbove));
    rowToggle("Requires recent collapse", requiresRecentCollapse);
    rowToggle("Requires recent sleep", requiresRecentSleep);
    stringField(
        kFocusDaysFlag, "Requires days-since flag", requiresDaysSinceFlag, "(none)");
    numericField(kFocusDaysMin, "Days-since min", std::to_string(requiresDaysSinceMin));
    numericField(kFocusDaysMax, "Days-since max", std::to_string(requiresDaysSinceMax));
    stringField(kFocusOpinionActor, "Opinion actor", opinionActor, "(none)");
    numericField(
        kFocusOpinionAtLeast,
        "Opinion at least",
        formatSignedInt(requiresActorOpinionAtLeast));
    numericField(
        kFocusOpinionAtMost, "Opinion at most", formatSignedInt(requiresActorOpinionAtMost));

    DrawTextEx(bold, "Media", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);
    DrawTextEx(
        font, "Image: Uses scene default (P1)", {fieldX, rowY()}, kFontSmall, 1.0f, kTextMuted);
    advance(18.0f);
    DrawTextEx(
        font, "Music: Uses scene default (P1)", {fieldX, rowY()}, kFontSmall, 1.0f, kTextMuted);
    advance(18.0f);
    DrawTextEx(
        font,
        "Ambient: Uses scene default (P1)",
        {fieldX, rowY()},
        kFontSmall,
        1.0f,
        kTextMuted);
    advance(22.0f);

    lastContentH = contentY;
    const float maxScroll = std::max(0.0f, lastContentH - scrollClip.height);
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);
    EndScissorMode();

    if (maxScroll > 1.0f)
    {
        const float trackX = dialog.x + dialog.width - 10.0f;
        const Rectangle track = {trackX, scrollClip.y, 4.0f, scrollClip.height};
        DrawRectangleRec(track, Color{40, 36, 48, 255});
        const float thumbH = std::max(
            24.0f, scrollClip.height * (scrollClip.height / lastContentH));
        const float thumbY =
            scrollClip.y + (scrollClip.height - thumbH) * (scrollY / maxScroll);
        DrawRectangleRec({trackX, thumbY, 4.0f, thumbH}, kPanelAccent);
    }

    const float btnY = dialog.y + dialogH - 44.0f;
    Rectangle saveBtn = {dialog.x + pad, btnY, 110.0f, 32.0f};
    Rectangle cancelBtn = {saveBtn.x + 120.0f, btnY, 110.0f, 32.0f};
    drawEditorButton(font, saveBtn, "Apply", true, !busy);
    drawEditorButton(font, cancelBtn, "Close", false, true);
    if (canClick && CheckCollisionPointRec(mouse, saveBtn))
        applyChanges();
    if (canClick && CheckCollisionPointRec(mouse, cancelBtn))
        closeDialog();

    const float msgX = cancelBtn.x + 120.0f;
    const float msgMaxW = dialog.x + dialog.width - pad - msgX;
    if (!error.empty())
    {
        const std::string shown = ellipsizeUi(font, error, kFontTiny, msgMaxW);
        DrawTextEx(
            font, shown.c_str(), {msgX, btnY + 8.0f}, kFontTiny, 1.0f, Color{220, 90, 80, 255});
    }
    else if (!status.empty())
    {
        const std::string shown = ellipsizeUi(font, status, kFontTiny, msgMaxW);
        DrawTextEx(
            font, shown.c_str(), {msgX, btnY + 8.0f}, kFontTiny, 1.0f, Color{120, 180, 120, 255});
    }

    if (itemSuggestOpen && focusField == kFocusInventory)
    {
        const std::vector<std::string> suggestions = inventoryItemSuggestions(10);
        if (!suggestions.empty())
        {
            const float rowH = 22.0f;
            const float menuH = rowH * static_cast<float>(suggestions.size()) + 6.0f;
            itemSuggestRect = {
                itemFieldRect.x,
                itemFieldRect.y + itemFieldRect.height + 2.0f,
                itemFieldRect.width,
                menuH};
            if (itemSuggestRect.y + itemSuggestRect.height > dialog.y + dialogH - footerH)
                itemSuggestRect.y = itemFieldRect.y - itemSuggestRect.height - 2.0f;
            DrawRectangleRec(itemSuggestRect, Color{36, 32, 44, 255});
            DrawRectangleLinesEx(itemSuggestRect, 1.0f, kPanelBorder);
            float sy = itemSuggestRect.y + 3.0f;
            for (const std::string& id : suggestions)
            {
                Rectangle row = {
                    itemSuggestRect.x + 2.0f,
                    sy,
                    itemSuggestRect.width - 4.0f,
                    rowH - 2.0f};
                if (CheckCollisionPointRec(mouse, row))
                {
                    DrawRectangleRec(row, Color{60, 54, 72, 220});
                    if (canClick)
                        applyInventoryItemSuggestion(id);
                }
                DrawTextEx(
                    font,
                    ellipsizeUi(font, id, kFontSmall, row.width - 12.0f).c_str(),
                    {row.x + 6.0f, row.y + 2.0f},
                    kFontSmall,
                    1.0f,
                    kTextPrimary);
                sy += rowH;
            }
        }
        if (canClick && !CheckCollisionPointRec(mouse, itemFieldRect)
            && !CheckCollisionPointRec(mouse, itemSuggestRect))
            itemSuggestOpen = false;
    }
    else if (focusField != kFocusInventory)
    {
        itemSuggestOpen = false;
    }

    if (voiceMenuOpen)
    {
        const auto& voices = builtinVoiceIds();
        const int voiceCount = static_cast<int>(voices.size());
        const float rowH = 22.0f;
        const int visible = std::min(kVoiceMenuVisibleRows, voiceCount + 1);
        const float maxVScroll =
            static_cast<float>(std::max(0, voiceCount + 1 - visible));
        voiceMenuScroll = std::clamp(voiceMenuScroll, 0.0f, maxVScroll);
        voiceMenuRect = {
            voiceBtnRect.x,
            voiceBtnRect.y + voiceBtnRect.height + 2.0f,
            160.0f,
            rowH * static_cast<float>(visible) + 4.0f};
        DrawRectangleRec(voiceMenuRect, Color{36, 32, 44, 255});
        DrawRectangleLinesEx(voiceMenuRect, 1.0f, kPanelBorder);
        const int first = static_cast<int>(std::floor(voiceMenuScroll + 0.001f));
        float my = voiceMenuRect.y + 2.0f;
        for (int row = 0; row < visible; ++row)
        {
            const int i = first + row;
            if (i < 0 || i >= voiceCount + 1)
                break;
            const std::string label = (i == 0) ? std::string("Off")
                                               : voices[static_cast<size_t>(i - 1)];
            Rectangle r = {
                voiceMenuRect.x + 2.0f, my, voiceMenuRect.width - 4.0f, rowH - 2.0f};
            if (CheckCollisionPointRec(mouse, r))
            {
                DrawRectangleRec(r, Color{60, 54, 72, 220});
                if (canClick)
                {
                    activeBag().ttsVoice = (i == 0) ? std::string{} : normalizeVoiceId(label);
                    voiceMenuOpen = false;
                }
            }
            DrawTextEx(font, label.c_str(), {r.x + 6.0f, r.y + 2.0f}, kFontSmall, 1.0f, kTextPrimary);
            my += rowH;
        }
        if (canClick && !CheckCollisionPointRec(mouse, voiceMenuRect)
            && !CheckCollisionPointRec(mouse, voiceBtnRect))
            voiceMenuOpen = false;
    }

    if (canClick && !CheckCollisionPointRec(mouse, dialog) && !voiceMenuOpen
        && !itemSuggestOpen)
        closeDialog();
}

} // namespace timberline_editor
