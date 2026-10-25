/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "FightAuthoringDialog.h"
#include "EditorAudio.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorPaths.h"
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

constexpr int kFocusEncounter = 0;
constexpr int kFocusCombatant = 1;
constexpr int kFocusBagText = 2;
constexpr int kFocusBagTts = 3;
constexpr int kFocusTabActor = 4;
constexpr int kFocusInventory = 5;
constexpr int kFocusStoryFlag = 6;
constexpr int kFocusEdgeHealth = 7;
constexpr int kFocusEdgeEnergy = 8;
constexpr int kFocusEdgeResolve = 9;
constexpr int kFocusEdgeLucidity = 10;
constexpr int kFocusEdgeCharisma = 11;
constexpr int kFocusLossHealth = 12;
constexpr int kFocusLossEnergy = 13;
constexpr int kFocusLossResolve = 14;
constexpr int kFocusLossLucidity = 15;
constexpr int kFocusLossCharisma = 16;
constexpr int kFocusRepDelta = 17;
constexpr int kFocusMoney = 18;
constexpr int kFocusInsuffMoney = 19;
constexpr int kFocusMinDay = 20;
constexpr int kFocusMaxDay = 21;
constexpr int kFocusLucBelow = 22;
constexpr int kFocusLucAbove = 23;
constexpr int kFocusDaysFlag = 24;
constexpr int kFocusDaysMin = 25;
constexpr int kFocusDaysMax = 26;
constexpr int kFocusOpinionActor = 27;
constexpr int kFocusOpinionAtLeast = 28;
constexpr int kFocusOpinionAtMost = 29;

const char* bagSlotKey(int index)
{
    switch (index)
    {
    case 1:
        return "win";
    case 2:
        return "lose";
    default:
        return "lead";
    }
}

const char* bagSlotLabel(int index)
{
    switch (index)
    {
    case 1:
        return "Win";
    case 2:
        return "Lose";
    default:
        return "Lead-up";
    }
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
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        size = 3;
    }
    else
    {
        bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
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

std::string sanitizeEncounterId(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (unsigned char ch : raw)
    {
        if (std::isalnum(ch) || ch == '_' || ch == '-')
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

std::string formatPct(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return buf;
}

std::string formatFloat(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return buf;
}

int clampEdge(int v)
{
    return std::clamp(v, -100, 100);
}

float clampPct(float v)
{
    if (!std::isfinite(v))
        return 0.0f;
    return std::clamp(v, 0.0f, 100.0f);
}

nlohmann::json bagToJson(const FightBagEdit& bag, const std::string& defaultAudio)
{
    nlohmann::json out = nlohmann::json::object();
    out["text"] = bag.text;
    const bool hasTts = !bag.ttsText.empty() || !bag.ttsAudio.empty();
    out["tts"] = hasTts;
    if (!bag.ttsVoice.empty())
        out["ttsVoice"] = bag.ttsVoice;
    out["ttsText"] = bag.ttsText;
    out["ttsAudio"] = bag.ttsAudio.empty() ? defaultAudio : bag.ttsAudio;
    if (bag.ttsAudioSegments.size() > 1)
        out["ttsAudioSegments"] = bag.ttsAudioSegments;
    return out;
}

nlohmann::json lossToJson(bool enabled, float pct)
{
    nlohmann::json out = nlohmann::json::object();
    out["enabled"] = enabled;
    out["percentOfCurrent"] = pct;
    return out;
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

nlohmann::json* lineObjectAt(DocumentWorkspace* docs, const std::string& pointer)
{
    if (docs == nullptr || pointer.empty())
        return nullptr;
    nlohmann::json* obj = docs->conversationJsonAt(pointer);
    if (obj != nullptr && obj->is_object())
        return obj;
    const size_t slash = pointer.rfind('/');
    if (slash == std::string::npos || slash == 0)
        return nullptr;
    obj = docs->conversationJsonAt(pointer.substr(0, slash));
    if (obj != nullptr && obj->is_object())
        return obj;
    return nullptr;
}

} // namespace

void fillFightNodeFromEncounterJson(
    DialogFlowNode& node,
    const nlohmann::json& encounter)
{
    if (!encounter.is_object())
        return;
    const std::string id = encounter.value("id", node.fightEncounterId);
    if (!id.empty())
        node.fightEncounterId = id;
    const std::string combatant = encounter.value("combatantId", node.combatantId);
    if (!combatant.empty())
        node.combatantId = combatant;
    else if (node.combatantId.empty() && !node.fightEncounterId.empty())
        node.combatantId = node.fightEncounterId;

    auto applyBag =
        [](const nlohmann::json& bag,
           std::string& text,
           std::string& tts,
           std::string& audio,
           std::vector<std::string>& segs,
           std::string& voice) {
            if (!bag.is_object())
                return;
            text = bag.value("text", text);
            tts = bag.value("ttsText", tts);
            audio = bag.value("ttsAudio", audio);
            const std::string v = bag.value("ttsVoice", voice);
            if (!v.empty())
                voice = normalizeVoiceId(v);
            segs.clear();
            if (bag.contains("ttsAudioSegments") && bag["ttsAudioSegments"].is_array())
            {
                for (const auto& entry : bag["ttsAudioSegments"])
                {
                    if (entry.is_string() && !entry.get<std::string>().empty())
                        segs.push_back(entry.get<std::string>());
                }
            }
        };
    applyBag(
        encounter.value("leadUp", nlohmann::json::object()),
        node.fightLeadText,
        node.fightLeadTts,
        node.fightLeadTtsAudio,
        node.fightLeadTtsAudioSegments,
        node.fightLeadVoice);
    applyBag(
        encounter.value("win", nlohmann::json::object()),
        node.fightWinText,
        node.fightWinTts,
        node.fightWinTtsAudio,
        node.fightWinTtsAudioSegments,
        node.fightWinVoice);
    applyBag(
        encounter.value("lose", nlohmann::json::object()),
        node.fightLoseText,
        node.fightLoseTts,
        node.fightLoseTtsAudio,
        node.fightLoseTtsAudioSegments,
        node.fightLoseVoice);

    const nlohmann::json& edge = encounter.value("actorEdge", nlohmann::json::object());
    if (edge.is_object())
    {
        node.fightActorAdvHealth = clampEdge(edge.value("health", node.fightActorAdvHealth));
        node.fightActorAdvEnergy = clampEdge(edge.value("energy", node.fightActorAdvEnergy));
        node.fightActorAdvResolve = clampEdge(edge.value("resolve", node.fightActorAdvResolve));
        node.fightActorAdvLucidity =
            clampEdge(edge.value("lucidity", node.fightActorAdvLucidity));
        node.fightActorAdvCharisma =
            clampEdge(edge.value("charisma", node.fightActorAdvCharisma));
    }

    node.fightCannotLose = encounter.value("cannotLose", node.fightCannotLose);
    node.fightPlayerDeath = encounter.value("playerDeath", node.fightPlayerDeath);
    if (node.fightCannotLose)
        node.fightPlayerDeath = false;
    node.playerDeathPossible = node.fightPlayerDeath;

    const nlohmann::json& loss = encounter.value("loss", nlohmann::json::object());
    if (loss.is_object())
    {
        auto one = [](const nlohmann::json& n, bool& on, float& pct) {
            if (!n.is_object())
                return;
            on = n.value("enabled", on);
            pct = clampPct(n.value("percentOfCurrent", pct));
        };
        one(loss.value("health", nlohmann::json::object()),
            node.fightLossHealth,
            node.fightLossHealthPct);
        one(loss.value("energy", nlohmann::json::object()),
            node.fightLossEnergy,
            node.fightLossEnergyPct);
        one(loss.value("resolve", nlohmann::json::object()),
            node.fightLossResolve,
            node.fightLossResolvePct);
        one(loss.value("lucidity", nlohmann::json::object()),
            node.fightLossLucidity,
            node.fightLossLucidityPct);
        one(loss.value("charisma", nlohmann::json::object()),
            node.fightLossCharisma,
            node.fightLossCharismaPct);
    }

    const nlohmann::json& rep = encounter.value("reputation", nlohmann::json::object());
    if (rep.is_object())
    {
        node.fightReputationChange = rep.value("enabled", node.fightReputationChange);
        node.fightReputationDelta = rep.value("delta", node.fightReputationDelta);
    }

    node.fightMinDay = encounter.value("minDay", node.fightMinDay);
    node.fightMaxDay = encounter.value("maxDay", node.fightMaxDay);
    node.fightRequiresLucidityBelow =
        encounter.value("requiresLucidityBelow", node.fightRequiresLucidityBelow);
    node.fightRequiresLucidityAbove =
        encounter.value("requiresLucidityAbove", node.fightRequiresLucidityAbove);
    node.fightRequiresRecentCollapse =
        encounter.value("requiresRecentCollapse", node.fightRequiresRecentCollapse);
    node.fightRequiresRecentSleep =
        encounter.value("requiresRecentSleep", node.fightRequiresRecentSleep);
    node.fightRequiresDaysSinceFlag =
        encounter.value("requiresDaysSinceFlag", node.fightRequiresDaysSinceFlag);
    node.fightRequiresDaysSinceMin =
        encounter.value("requiresDaysSinceMin", node.fightRequiresDaysSinceMin);
    node.fightRequiresDaysSinceMax =
        encounter.value("requiresDaysSinceMax", node.fightRequiresDaysSinceMax);
    if (encounter.contains("requiresDaysSince")
        && encounter["requiresDaysSince"].is_object())
    {
        const nlohmann::json& since = encounter["requiresDaysSince"];
        node.fightRequiresDaysSinceFlag =
            since.value("flag", node.fightRequiresDaysSinceFlag);
        node.fightRequiresDaysSinceMin =
            since.value("min", node.fightRequiresDaysSinceMin);
        node.fightRequiresDaysSinceMax =
            since.value("max", node.fightRequiresDaysSinceMax);
    }
    node.fightOpinionActor = encounter.value("opinionActor", node.fightOpinionActor);
    if (encounter.contains("requiresActorOpinionAtLeast"))
    {
        node.fightHasActorOpinionAtLeast = true;
        node.fightRequiresActorOpinionAtLeast =
            encounter.value("requiresActorOpinionAtLeast", 0);
    }
    if (encounter.contains("requiresActorOpinionAtMost"))
    {
        node.fightHasActorOpinionAtMost = true;
        node.fightRequiresActorOpinionAtMost =
            encounter.value("requiresActorOpinionAtMost", 0);
    }

    const nlohmann::json& avail =
        encounter.value("availability", nlohmann::json::object());
    const nlohmann::json& src = avail.is_object() ? avail : encounter;
    node.fightRequiresMoney = src.value("requiresMoney", node.fightRequiresMoney);
    node.fightRequiresInsufficientMoney =
        src.value("requiresInsufficientMoney", node.fightRequiresInsufficientMoney);
    node.fightRequiresPositiveActorTab =
        src.value("requiresPositiveActorTab", node.fightRequiresPositiveActorTab);
    node.fightRequiresPayActorTabInFull =
        src.value("requiresPayActorTabInFull", node.fightRequiresPayActorTabInFull);
    node.fightRequiresInsufficientForActorTab = src.value(
        "requiresInsufficientForActorTab", node.fightRequiresInsufficientForActorTab);
    node.fightRequiresSaloonRoomAvailable =
        src.value("requiresSaloonRoomAvailable", node.fightRequiresSaloonRoomAvailable);
    node.fightTabActor = src.value("tabActor", node.fightTabActor);
    node.fightRequiresLightSource =
        src.value("requiresLightSource", node.fightRequiresLightSource);
    node.fightRequiresInventoryItem =
        src.value("requiresInventoryItem", node.fightRequiresInventoryItem);
    node.fightRequiresStoryFlag =
        src.value("requiresStoryFlag", node.fightRequiresStoryFlag);
}

FightAuthoringDialog::~FightAuthoringDialog()
{
    generateCancel.store(true);
    joinGenerateThread();
    stopPreviewVoice();
}

void FightAuthoringDialog::joinGenerateThread()
{
    if (generateThread.joinable())
        generateThread.join();
}

void FightAuthoringDialog::resetWorkingState()
{
    encounterId.clear();
    combatantId.clear();
    loadedEncounterId.clear();
    for (FightBagEdit& bag : bags)
        bag = FightBagEdit{};
    selectedBag = 0;
    actorEdgeHealth = 0;
    actorEdgeEnergy = 0;
    actorEdgeResolve = 0;
    actorEdgeLucidity = 0;
    actorEdgeCharisma = 0;
    cannotLose = false;
    playerDeath = false;
    lossHealth = false;
    lossEnergy = false;
    lossResolve = false;
    lossLucidity = false;
    lossCharisma = false;
    lossHealthPct = 50.0f;
    lossEnergyPct = 50.0f;
    lossResolvePct = 50.0f;
    lossLucidityPct = 50.0f;
    lossCharismaPct = 50.0f;
    reputationChange = false;
    reputationDelta = 0;
    requiresMoney = 0.0f;
    requiresInsufficientMoney = 0.0f;
    requiresPositiveActorTab = false;
    requiresPayActorTabInFull = false;
    requiresInsufficientForActorTab = false;
    requiresSaloonRoomAvailable = false;
    tabActor.clear();
    requiresLightSource = false;
    requiresInventoryItem.clear();
    requiresStoryFlag.clear();
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
    focusField = kFocusEncounter;
    focusDraft.clear();
    scrollY = 0.0f;
    voiceMenuOpen = false;
    itemSuggestOpen = false;
    status.clear();
    error.clear();
}

void FightAuthoringDialog::copyNodeToDialog(const DialogFlowNode& n)
{
    encounterId = n.fightEncounterId.empty() ? n.combatantId : n.fightEncounterId;
    combatantId = n.combatantId;
    bags[0].text = n.fightLeadText;
    bags[0].ttsText = n.fightLeadTts;
    bags[0].ttsVoice = n.fightLeadVoice.empty() ? "leo" : n.fightLeadVoice;
    bags[0].ttsAudio = n.fightLeadTtsAudio;
    bags[0].ttsAudioSegments = n.fightLeadTtsAudioSegments;
    bags[1].text = n.fightWinText;
    bags[1].ttsText = n.fightWinTts;
    bags[1].ttsVoice = n.fightWinVoice.empty() ? "leo" : n.fightWinVoice;
    bags[1].ttsAudio = n.fightWinTtsAudio;
    bags[1].ttsAudioSegments = n.fightWinTtsAudioSegments;
    bags[2].text = n.fightLoseText;
    bags[2].ttsText = n.fightLoseTts;
    bags[2].ttsVoice = n.fightLoseVoice.empty() ? "leo" : n.fightLoseVoice;
    bags[2].ttsAudio = n.fightLoseTtsAudio;
    bags[2].ttsAudioSegments = n.fightLoseTtsAudioSegments;
    actorEdgeHealth = n.fightActorAdvHealth;
    actorEdgeEnergy = n.fightActorAdvEnergy;
    actorEdgeResolve = n.fightActorAdvResolve;
    actorEdgeLucidity = n.fightActorAdvLucidity;
    actorEdgeCharisma = n.fightActorAdvCharisma;
    cannotLose = n.fightCannotLose;
    playerDeath = n.fightPlayerDeath;
    lossHealth = n.fightLossHealth;
    lossEnergy = n.fightLossEnergy;
    lossResolve = n.fightLossResolve;
    lossLucidity = n.fightLossLucidity;
    lossCharisma = n.fightLossCharisma;
    lossHealthPct = n.fightLossHealthPct;
    lossEnergyPct = n.fightLossEnergyPct;
    lossResolvePct = n.fightLossResolvePct;
    lossLucidityPct = n.fightLossLucidityPct;
    lossCharismaPct = n.fightLossCharismaPct;
    reputationChange = n.fightReputationChange;
    reputationDelta = n.fightReputationDelta;
    requiresMoney = n.fightRequiresMoney;
    requiresInsufficientMoney = n.fightRequiresInsufficientMoney;
    requiresPositiveActorTab = n.fightRequiresPositiveActorTab;
    requiresPayActorTabInFull = n.fightRequiresPayActorTabInFull;
    requiresInsufficientForActorTab = n.fightRequiresInsufficientForActorTab;
    requiresSaloonRoomAvailable = n.fightRequiresSaloonRoomAvailable;
    tabActor = n.fightTabActor;
    requiresLightSource = n.fightRequiresLightSource;
    requiresInventoryItem = n.fightRequiresInventoryItem;
    requiresStoryFlag = n.fightRequiresStoryFlag;
    minDay = n.fightMinDay;
    maxDay = n.fightMaxDay;
    requiresLucidityBelow = n.fightRequiresLucidityBelow;
    requiresLucidityAbove = n.fightRequiresLucidityAbove;
    requiresRecentCollapse = n.fightRequiresRecentCollapse;
    requiresRecentSleep = n.fightRequiresRecentSleep;
    requiresDaysSinceFlag = n.fightRequiresDaysSinceFlag;
    requiresDaysSinceMin = n.fightRequiresDaysSinceMin;
    requiresDaysSinceMax = n.fightRequiresDaysSinceMax;
    opinionActor = n.fightOpinionActor;
    hasActorOpinionAtLeast = n.fightHasActorOpinionAtLeast;
    requiresActorOpinionAtLeast = n.fightRequiresActorOpinionAtLeast;
    hasActorOpinionAtMost = n.fightHasActorOpinionAtMost;
    requiresActorOpinionAtMost = n.fightRequiresActorOpinionAtMost;
}

void FightAuthoringDialog::copyDialogToNode(DialogFlowNode& n) const
{
    const std::string id = effectiveEncounterId();
    n.fightEncounterId = id;
    n.combatantId = combatantId.empty() ? id : combatantId;
    n.fightLeadText = bags[0].text;
    n.fightLeadTts = bags[0].ttsText;
    n.fightLeadVoice = bags[0].ttsVoice;
    n.fightLeadTtsAudio = bags[0].ttsAudio.empty() ? defaultBagAudioPath(0) : bags[0].ttsAudio;
    n.fightLeadTtsAudioSegments = bags[0].ttsAudioSegments;
    n.fightWinText = bags[1].text;
    n.fightWinTts = bags[1].ttsText;
    n.fightWinVoice = bags[1].ttsVoice;
    n.fightWinTtsAudio = bags[1].ttsAudio.empty() ? defaultBagAudioPath(1) : bags[1].ttsAudio;
    n.fightWinTtsAudioSegments = bags[1].ttsAudioSegments;
    n.fightLoseText = bags[2].text;
    n.fightLoseTts = bags[2].ttsText;
    n.fightLoseVoice = bags[2].ttsVoice;
    n.fightLoseTtsAudio = bags[2].ttsAudio.empty() ? defaultBagAudioPath(2) : bags[2].ttsAudio;
    n.fightLoseTtsAudioSegments = bags[2].ttsAudioSegments;
    n.fightActorAdvHealth = clampEdge(actorEdgeHealth);
    n.fightActorAdvEnergy = clampEdge(actorEdgeEnergy);
    n.fightActorAdvResolve = clampEdge(actorEdgeResolve);
    n.fightActorAdvLucidity = clampEdge(actorEdgeLucidity);
    n.fightActorAdvCharisma = clampEdge(actorEdgeCharisma);
    n.fightCannotLose = cannotLose;
    n.fightPlayerDeath = cannotLose ? false : playerDeath;
    n.playerDeathPossible = n.fightPlayerDeath;
    n.fightLossHealth = lossHealth;
    n.fightLossEnergy = lossEnergy;
    n.fightLossResolve = lossResolve;
    n.fightLossLucidity = lossLucidity;
    n.fightLossCharisma = lossCharisma;
    n.fightLossHealthPct = clampPct(lossHealthPct);
    n.fightLossEnergyPct = clampPct(lossEnergyPct);
    n.fightLossResolvePct = clampPct(lossResolvePct);
    n.fightLossLucidityPct = clampPct(lossLucidityPct);
    n.fightLossCharismaPct = clampPct(lossCharismaPct);
    n.fightReputationChange = reputationChange;
    n.fightReputationDelta = reputationDelta;
    n.fightRequiresMoney = requiresMoney;
    n.fightRequiresInsufficientMoney = requiresInsufficientMoney;
    n.fightRequiresPositiveActorTab = requiresPositiveActorTab;
    n.fightRequiresPayActorTabInFull = requiresPayActorTabInFull;
    n.fightRequiresInsufficientForActorTab = requiresInsufficientForActorTab;
    n.fightRequiresSaloonRoomAvailable = requiresSaloonRoomAvailable;
    n.fightTabActor = tabActor;
    n.fightRequiresLightSource = requiresLightSource;
    n.fightRequiresInventoryItem = requiresInventoryItem;
    n.fightRequiresStoryFlag = requiresStoryFlag;
    n.fightMinDay = minDay;
    n.fightMaxDay = maxDay;
    n.fightRequiresLucidityBelow = requiresLucidityBelow;
    n.fightRequiresLucidityAbove = requiresLucidityAbove;
    n.fightRequiresRecentCollapse = requiresRecentCollapse;
    n.fightRequiresRecentSleep = requiresRecentSleep;
    n.fightRequiresDaysSinceFlag = requiresDaysSinceFlag;
    n.fightRequiresDaysSinceMin = requiresDaysSinceMin;
    n.fightRequiresDaysSinceMax = requiresDaysSinceMax;
    n.fightOpinionActor = opinionActor;
    n.fightHasActorOpinionAtLeast = hasActorOpinionAtLeast;
    n.fightRequiresActorOpinionAtLeast = requiresActorOpinionAtLeast;
    n.fightHasActorOpinionAtMost = hasActorOpinionAtMost;
    n.fightRequiresActorOpinionAtMost = requiresActorOpinionAtMost;
}

void FightAuthoringDialog::loadFromNodeAndJson()
{
    resetWorkingState();
    if (node != nullptr)
        copyNodeToDialog(*node);

    std::string id = sanitizeEncounterId(effectiveEncounterId());
    if (id.empty() && node != nullptr)
        id = sanitizeEncounterId(node->combatantId);
    if (docs != nullptr && !sceneId.empty() && docs->conversationsLoaded
        && docs->conversationsRoot.contains(sceneId)
        && docs->conversationsRoot[sceneId].is_object())
    {
        const nlohmann::json& scene = docs->conversationsRoot[sceneId];
        if (scene.contains("attackEncounters") && scene["attackEncounters"].is_object())
        {
            const nlohmann::json& table = scene["attackEncounters"];
            nlohmann::json encounter = nlohmann::json::object();
            bool found = false;
            if (!id.empty() && table.contains(id) && table[id].is_object())
            {
                encounter = table[id];
                found = true;
            }
            else if (node != nullptr && !node->combatantId.empty()
                     && table.contains(node->combatantId)
                     && table[node->combatantId].is_object())
            {
                encounter = table[node->combatantId];
                found = true;
            }
            if (found)
            {
                if (node != nullptr)
                    fillFightNodeFromEncounterJson(*node, encounter);
                DialogFlowNode tmp;
                if (node != nullptr)
                    tmp = *node;
                fillFightNodeFromEncounterJson(tmp, encounter);
                copyNodeToDialog(tmp);
                loadedEncounterId = tmp.fightEncounterId.empty()
                    ? (encounter.value("id", id))
                    : tmp.fightEncounterId;
            }
        }
    }
    if (loadedEncounterId.empty())
        loadedEncounterId = sanitizeEncounterId(effectiveEncounterId());
    for (int i = 0; i < 3; ++i)
    {
        if (bags[i].ttsAudio.empty())
            bags[i].ttsAudio = defaultBagAudioPath(i);
        if (bags[i].ttsVoice.empty())
            bags[i].ttsVoice = "leo";
    }
}

void FightAuthoringDialog::openForFight(
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

void FightAuthoringDialog::closeDialog()
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

FightBagEdit& FightAuthoringDialog::activeBag()
{
    return bags[std::clamp(selectedBag, 0, 2)];
}

const FightBagEdit& FightAuthoringDialog::activeBag() const
{
    return bags[std::clamp(selectedBag, 0, 2)];
}

std::string FightAuthoringDialog::effectiveEncounterId() const
{
    const std::string fromEncounter = sanitizeEncounterId(encounterId);
    if (!fromEncounter.empty())
        return fromEncounter;
    const std::string fromCombatant = sanitizeEncounterId(combatantId);
    if (!fromCombatant.empty())
        return fromCombatant;
    return {};
}

std::string FightAuthoringDialog::defaultBagAudioPath(int bagIndex) const
{
    const std::string id = effectiveEncounterId().empty() ? std::string("fight")
                                                         : effectiveEncounterId();
    const std::string scene = sceneId.empty() ? std::string("scene") : sceneId;
    return "resources/audio/tts/" + scene + "/fight_" + id + "_" + bagSlotKey(bagIndex)
        + ".mp3";
}

std::string FightAuthoringDialog::bagAudioPath(int bagIndex) const
{
    const FightBagEdit& bag = bags[std::clamp(bagIndex, 0, 2)];
    return bag.ttsAudio.empty() ? defaultBagAudioPath(bagIndex) : bag.ttsAudio;
}

bool FightAuthoringDialog::bagAudioExists(int bagIndex) const
{
    return assetRelExists(docs, bagAudioPath(bagIndex));
}

std::string FightAuthoringDialog::effectiveApiKey() const
{
    return sessionKeys != nullptr ? sessionKeys->xaiKey : std::string{};
}

bool FightAuthoringDialog::applyChanges()
{
    commitNumericFocus();
    if (node == nullptr)
    {
        error = "No Fight node bound.";
        return false;
    }
    const std::string id = effectiveEncounterId();
    if (id.empty())
    {
        error = "Set an encounter id (or combatant id).";
        return false;
    }
    encounterId = id;
    if (combatantId.empty())
        combatantId = id;
    if (cannotLose)
        playerDeath = false;
    for (int i = 0; i < 3; ++i)
    {
        if (bags[i].ttsAudio.empty())
            bags[i].ttsAudio = defaultBagAudioPath(i);
    }

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
    if (!docs->conversationsRoot.contains(sceneId)
        || !docs->conversationsRoot[sceneId].is_object())
        docs->conversationsRoot[sceneId] = nlohmann::json::object();
    nlohmann::json& scene = docs->conversationsRoot[sceneId];
    if (!scene.contains("attackEncounters") || !scene["attackEncounters"].is_object())
        scene["attackEncounters"] = nlohmann::json::object();
    nlohmann::json& table = scene["attackEncounters"];

    nlohmann::json enc = nlohmann::json::object();
    enc["id"] = id;
    enc["combatantId"] = combatantId;
    enc["leadUp"] = bagToJson(bags[0], defaultBagAudioPath(0));
    enc["win"] = bagToJson(bags[1], defaultBagAudioPath(1));
    enc["lose"] = bagToJson(bags[2], defaultBagAudioPath(2));
    enc["actorEdge"] = nlohmann::json{
        {"health", clampEdge(actorEdgeHealth)},
        {"energy", clampEdge(actorEdgeEnergy)},
        {"resolve", clampEdge(actorEdgeResolve)},
        {"lucidity", clampEdge(actorEdgeLucidity)},
        {"charisma", clampEdge(actorEdgeCharisma)}};
    enc["cannotLose"] = cannotLose;
    enc["playerDeath"] = playerDeath;

    nlohmann::json loss = nlohmann::json::object();
    if (lossHealth)
        loss["health"] = lossToJson(true, clampPct(lossHealthPct));
    if (lossEnergy)
        loss["energy"] = lossToJson(true, clampPct(lossEnergyPct));
    if (lossResolve)
        loss["resolve"] = lossToJson(true, clampPct(lossResolvePct));
    if (lossLucidity)
        loss["lucidity"] = lossToJson(true, clampPct(lossLucidityPct));
    if (lossCharisma)
        loss["charisma"] = lossToJson(true, clampPct(lossCharismaPct));
    enc["loss"] = loss;

    nlohmann::json rep = nlohmann::json::object();
    rep["enabled"] = reputationChange;
    rep["delta"] = reputationDelta;
    enc["reputation"] = rep;

    nlohmann::json avail = nlohmann::json::object();
    if (requiresMoney > 0.0f)
        avail["requiresMoney"] = requiresMoney;
    if (requiresInsufficientMoney > 0.0f)
        avail["requiresInsufficientMoney"] = requiresInsufficientMoney;
    if (requiresPositiveActorTab)
        avail["requiresPositiveActorTab"] = true;
    if (requiresPayActorTabInFull)
        avail["requiresPayActorTabInFull"] = true;
    if (requiresInsufficientForActorTab)
        avail["requiresInsufficientForActorTab"] = true;
    if (requiresSaloonRoomAvailable)
        avail["requiresSaloonRoomAvailable"] = true;
    if (!tabActor.empty())
        avail["tabActor"] = tabActor;
    if (requiresLightSource)
        avail["requiresLightSource"] = true;
    if (!requiresInventoryItem.empty())
        avail["requiresInventoryItem"] = requiresInventoryItem;
    if (!requiresStoryFlag.empty())
        avail["requiresStoryFlag"] = requiresStoryFlag;
    enc["availability"] = avail;

    if (minDay > 0)
        enc["minDay"] = minDay;
    if (maxDay > 0)
        enc["maxDay"] = maxDay;
    if (requiresLucidityBelow > 0.0f)
        enc["requiresLucidityBelow"] = requiresLucidityBelow;
    if (requiresLucidityAbove > 0.0f)
        enc["requiresLucidityAbove"] = requiresLucidityAbove;
    if (requiresRecentCollapse)
        enc["requiresRecentCollapse"] = true;
    if (requiresRecentSleep)
        enc["requiresRecentSleep"] = true;
    if (!requiresDaysSinceFlag.empty())
    {
        nlohmann::json since = nlohmann::json::object();
        since["flag"] = requiresDaysSinceFlag;
        if (requiresDaysSinceMin > 0)
            since["min"] = requiresDaysSinceMin;
        if (requiresDaysSinceMax > 0)
            since["max"] = requiresDaysSinceMax;
        enc["requiresDaysSince"] = since;
        enc["requiresDaysSinceFlag"] = requiresDaysSinceFlag;
        if (requiresDaysSinceMin > 0)
            enc["requiresDaysSinceMin"] = requiresDaysSinceMin;
        if (requiresDaysSinceMax > 0)
            enc["requiresDaysSinceMax"] = requiresDaysSinceMax;
    }
    if (!opinionActor.empty())
        enc["opinionActor"] = opinionActor;
    if (hasActorOpinionAtLeast)
        enc["requiresActorOpinionAtLeast"] = requiresActorOpinionAtLeast;
    if (hasActorOpinionAtMost)
        enc["requiresActorOpinionAtMost"] = requiresActorOpinionAtMost;

    if (!loadedEncounterId.empty() && loadedEncounterId != id
        && table.contains(loadedEncounterId))
        table.erase(loadedEncounterId);
    table[id] = enc;
    loadedEncounterId = id;

    if (nlohmann::json* line = lineObjectAt(docs, node->jsonPointer))
    {
        (*line)["allowAttack"] = true;
        (*line)["attackEncounterId"] = id;
    }

    docs->markDirty();
    status = "Saved fight " + id;
    error.clear();
    if (onSaved)
        onSaved();
    return true;
}

bool FightAuthoringDialog::focusedIsNumeric() const
{
    return focusField >= kFocusEdgeHealth && focusField != kFocusDaysFlag
        && focusField != kFocusOpinionActor;
}

void FightAuthoringDialog::beginNumericFocus(int field, const std::string& initial)
{
    commitNumericFocus();
    focusField = field;
    focusDraft = initial;
    itemSuggestOpen = false;
    voiceMenuOpen = false;
}

void FightAuthoringDialog::commitNumericFocus()
{
    if (!focusedIsNumeric())
        return;
    const char* s = focusDraft.c_str();
    auto asInt = [&]() { return static_cast<int>(std::strtol(s, nullptr, 10)); };
    auto asFloat = [&]() { return std::strtof(s, nullptr); };
    switch (focusField)
    {
    case kFocusEdgeHealth:
        actorEdgeHealth = clampEdge(asInt());
        break;
    case kFocusEdgeEnergy:
        actorEdgeEnergy = clampEdge(asInt());
        break;
    case kFocusEdgeResolve:
        actorEdgeResolve = clampEdge(asInt());
        break;
    case kFocusEdgeLucidity:
        actorEdgeLucidity = clampEdge(asInt());
        break;
    case kFocusEdgeCharisma:
        actorEdgeCharisma = clampEdge(asInt());
        break;
    case kFocusLossHealth:
        lossHealthPct = clampPct(asFloat());
        break;
    case kFocusLossEnergy:
        lossEnergyPct = clampPct(asFloat());
        break;
    case kFocusLossResolve:
        lossResolvePct = clampPct(asFloat());
        break;
    case kFocusLossLucidity:
        lossLucidityPct = clampPct(asFloat());
        break;
    case kFocusLossCharisma:
        lossCharismaPct = clampPct(asFloat());
        break;
    case kFocusRepDelta:
        reputationDelta = asInt();
        break;
    case kFocusMoney:
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

std::string* FightAuthoringDialog::focusedString()
{
    switch (focusField)
    {
    case kFocusEncounter:
        return &encounterId;
    case kFocusCombatant:
        return &combatantId;
    case kFocusBagText:
        return &activeBag().text;
    case kFocusBagTts:
        return &activeBag().ttsText;
    case kFocusTabActor:
        return &tabActor;
    case kFocusInventory:
        return &requiresInventoryItem;
    case kFocusStoryFlag:
        return &requiresStoryFlag;
    case kFocusDaysFlag:
        return &requiresDaysSinceFlag;
    case kFocusOpinionActor:
        return &opinionActor;
    default:
        return nullptr;
    }
}

void FightAuthoringDialog::typeIntoFocusedField()
{
    if (focusedIsNumeric())
    {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_ESCAPE))
        {
            if (IsKeyPressed(KEY_ESCAPE))
            {
                focusDraft.clear();
                focusField = kFocusEncounter;
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
        const bool allowMinus = focusField == kFocusRepDelta
            || (focusField >= kFocusEdgeHealth && focusField <= kFocusEdgeCharisma)
            || focusField == kFocusOpinionAtLeast || focusField == kFocusOpinionAtMost;
        const bool allowDot = focusField == kFocusLossHealth
            || focusField == kFocusLossEnergy || focusField == kFocusLossResolve
            || focusField == kFocusLossLucidity || focusField == kFocusLossCharisma
            || focusField == kFocusMoney || focusField == kFocusInsuffMoney
            || focusField == kFocusLucBelow || focusField == kFocusLucAbove;
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

std::string FightAuthoringDialog::inventoryItemTokenPrefix() const
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

void FightAuthoringDialog::applyInventoryItemSuggestion(const std::string& itemId)
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

std::vector<std::string> FightAuthoringDialog::inventoryItemSuggestions(int maxCount) const
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

void FightAuthoringDialog::stopPreviewVoice()
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

void FightAuthoringDialog::updatePreviewVoice()
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

void FightAuthoringDialog::startPreviewVoice()
{
    if (docs == nullptr || generateBusy.load())
        return;
    if (!bagAudioExists(selectedBag))
    {
        error = "No fight TTS audio on disk yet — Generate voice first.";
        return;
    }
    stopPreviewVoice();
    if (!editorAudioDeviceReady())
    {
        error = "Audio device not ready.";
        return;
    }
    const std::string rel = bagAudioPath(selectedBag);
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
            "editor_fight_tts_preview.mp3");
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
        error = "Could not load fight TTS audio.";
        return;
    }
    music.looping = false;
    previewVoice = music;
    previewVoiceLoaded = true;
    previewVoicePlaying = true;
    previewVoiceTempFile = tempFile;
    SetMusicVolume(previewVoice, 1.0f);
    PlayMusicStream(previewVoice);
    status = std::string("Previewing ") + bagSlotLabel(selectedBag) + " TTS…";
    error.clear();
}

void FightAuthoringDialog::openParchmentForBag(bool tts)
{
    if (parchment == nullptr || docs == nullptr)
        return;
    FightBagEdit& bag = activeBag();
    if (tts && bag.ttsAudio.empty())
        bag.ttsAudio = defaultBagAudioPath(selectedBag);
    if (tts && bag.ttsText.empty() && !bag.text.empty())
        bag.ttsText = bag.text;
    const std::string label = std::string("Fight ") + bagSlotLabel(selectedBag)
        + (tts ? " TTS" : " text");
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

void FightAuthoringDialog::startTtsDialogGenerate()
{
    if (docs == nullptr || generateBusy.load())
        return;
    FightBagEdit& bag = activeBag();
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
        std::string("Prepare the following fight ") + bagSlotLabel(selectedBag)
        + " text as spoken Timberline TTS narration (lead-up to a scrape, or the "
          "win/lose aftermath). "
        + ttsEmbellishRules + styleBlock + "\nSOURCE:\n" + source;

    const std::string root = docs->assetRoot.empty() ? "." : docs->assetRoot;
    const std::string authoringDir =
        pathJoin(pathJoin(root, "resources"), ".authoring");
#if !defined(_WIN32)
    const std::string mkdirCmd = "mkdir -p '" + authoringDir + "'";
    std::system(mkdirCmd.c_str());
#endif
    const std::string id = effectiveEncounterId().empty() ? std::string("fight")
                                                         : effectiveEncounterId();
    const std::string stem =
        sceneId + "_fight_" + id + "_" + bagSlotKey(selectedBag);
    const std::string outRel = "resources/.authoring/" + stem + "_tts.txt";
    pendingTtsJobsPath = pathJoin(authoringDir, stem + "_tts_jobs.json");

    nlohmann::json jobsRoot;
    jobsRoot["sceneId"] = sceneId;
    jobsRoot["itemId"] = sceneId;
    jobsRoot["kind"] = "fight_tts";
    jobsRoot["jobs"] = nlohmann::json::array(
        {nlohmann::json{
            {"type", "generate_scene_description_tts_text"},
            {"prompt", prompt},
            {"outPath", outRel},
            {"action", "fight_tts"},
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
    generateBag = selectedBag;
    generateResultPending = false;
    status = std::string("Generating ") + bagSlotLabel(selectedBag) + " TTS dialog…";
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

void FightAuthoringDialog::startVoiceGenerate()
{
    if (docs == nullptr || generateBusy.load())
        return;
    FightBagEdit& bag = activeBag();
    if (!sceneTtsTextHasWord(bag.ttsText))
    {
        error = "Add fight TTS text before generating voice.";
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
        bag.ttsAudio = defaultBagAudioPath(selectedBag);
    if (!applyChanges())
        return;

    generateCancel.store(false);
    generateBusy = true;
    generateKind = 2;
    generateBag = selectedBag;
    generateResultPending = false;
    generateResultPath.clear();
    generateResultSegments.clear();
    status = std::string("Generating ") + bagSlotLabel(selectedBag) + " voice…";
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

void FightAuthoringDialog::pollGenerateResult()
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
                        if (!text.empty() && bagIndex >= 0 && bagIndex < 3)
                        {
                            bags[bagIndex].ttsText = text;
                            if (bags[bagIndex].ttsAudio.empty())
                                bags[bagIndex].ttsAudio = defaultBagAudioPath(bagIndex);
                            (void)applyChanges();
                            status = std::string(bagSlotLabel(bagIndex))
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

    if (kind == 2 && bagIndex >= 0 && bagIndex < 3)
    {
        if (!generateResultPath.empty())
        {
            bags[bagIndex].ttsAudio = generateResultPath;
            bags[bagIndex].ttsAudioSegments = generateResultSegments;
            (void)applyChanges();
            status = std::string(bagSlotLabel(bagIndex)) + " voice ready.";
        }
        else if (error.empty() && status.find("Voice") == std::string::npos)
            error = status;
        generateResultPath.clear();
        generateResultSegments.clear();
    }
}

void FightAuthoringDialog::handleInput(int screenW, int screenH)
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

void FightAuthoringDialog::draw(int screenW, int screenH)
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

    DrawTextEx(bold, "Edit Fight", {fieldX, y}, kFontHeading, 1.0f, kTextPrimary);
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

    stringField(kFocusEncounter, "Encounter id", encounterId, "(defaults to combatant id)");
    stringField(kFocusCombatant, "Combatant id", combatantId, "(actor / opponent id)");

    DrawTextEx(bold, "Dialog bags", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);

    const float tabW = (fieldW - 16.0f) / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        Rectangle tab = {fieldX + static_cast<float>(i) * (tabW + 8.0f), rowY(), tabW, 26.0f};
        const bool on = selectedBag == i;
        drawEditorButton(font, tab, bagSlotLabel(i), on, true);
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

    DrawTextEx(
        font,
        "On-screen text",
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
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
        ("Voice: " + (activeBag().ttsVoice.empty() ? "leo" : activeBag().ttsVoice)).c_str(),
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
    const bool canGen =
        !busy && sceneTtsTextHasWord(activeBag().ttsText) && xaiOk
        && !activeBag().ttsVoice.empty();
    const bool canPrev = !busy && bagAudioExists(selectedBag);
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
        bagAudioPath(selectedBag).c_str(),
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(18.0f);

    DrawTextEx(bold, "Actor edge", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);
    DrawTextEx(
        font,
        "Signed −100…+100. Positive = actor advantage (harder). 0 = even.",
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(18.0f);
    numericField(kFocusEdgeHealth, "Health", formatSignedInt(actorEdgeHealth));
    numericField(kFocusEdgeEnergy, "Energy", formatSignedInt(actorEdgeEnergy));
    numericField(kFocusEdgeResolve, "Resolve", formatSignedInt(actorEdgeResolve));
    numericField(kFocusEdgeLucidity, "Lucidity", formatSignedInt(actorEdgeLucidity));
    numericField(kFocusEdgeCharisma, "Charisma", formatSignedInt(actorEdgeCharisma));

    DrawTextEx(bold, "Outcome", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);
    DrawTextEx(
        font,
        "Player death and cannot-lose are mutually exclusive. Loss % is of current.",
        {fieldX, rowY()},
        kFontTiny,
        1.0f,
        kTextMuted);
    advance(18.0f);
    rowToggle("Player cannot lose", cannotLose);
    if (cannotLose && playerDeath)
        playerDeath = false;
    rowToggle("Player death on lose", playerDeath);
    if (playerDeath && cannotLose)
        cannotLose = false;
    const bool lossEnabled = !cannotLose;
    auto lossRow = [&](const char* label, bool& on, int pctField, float& pct) {
        const float rowH = 28.0f;
        const float sw = 44.0f;
        const float sh = 22.0f;
        const Rectangle track = {fieldX, rowY() + 3.0f, sw, sh};
        bool toggled = false;
        drawOnOffSwitch(font, track, on && lossEnabled, label, false, toggled);
        Rectangle pctRect = {fieldX + fieldW - 72.0f, rowY() + 2.0f, 52.0f, 24.0f};
        const bool focused = focusField == pctField;
        DrawRectangleRec(
            pctRect, focused ? Color{50, 48, 62, 255} : Color{24, 22, 32, 255});
        DrawRectangleLinesEx(
            pctRect, focused ? 2.0f : 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
        const std::string shown = focused
            ? (focusDraft.empty() ? "|" : focusDraft)
            : formatPct(pct);
        DrawTextEx(
            font,
            shown.c_str(),
            {pctRect.x + 4.0f, pctRect.y + 4.0f},
            kFontTiny,
            1.0f,
            lossEnabled ? kTextPrimary : kTextMuted);
        DrawTextEx(
            font,
            "%",
            {pctRect.x + pctRect.width + 4.0f, pctRect.y + 4.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
        if (canClick && lossEnabled && CheckCollisionPointRec(mouse, scrollClip))
        {
            const Rectangle hit = {fieldX, rowY(), fieldW - 80.0f, rowH};
            if (CheckCollisionPointRec(mouse, hit))
                on = !on;
            if (CheckCollisionPointRec(mouse, pctRect))
                beginNumericFocus(pctField, formatPct(pct));
        }
        advance(rowH + 4.0f);
    };
    lossRow("Health loss", lossHealth, kFocusLossHealth, lossHealthPct);
    lossRow("Energy loss", lossEnergy, kFocusLossEnergy, lossEnergyPct);
    lossRow("Resolve loss", lossResolve, kFocusLossResolve, lossResolvePct);
    lossRow("Lucidity loss", lossLucidity, kFocusLossLucidity, lossLucidityPct);
    lossRow("Charisma loss", lossCharisma, kFocusLossCharisma, lossCharismaPct);

    DrawTextEx(bold, "Actor reputation", {fieldX, rowY()}, kFontSmall, 1.0f, kTextPrimary);
    advance(20.0f);
    {
        const float rowH = 28.0f;
        const float sw = 44.0f;
        const float sh = 22.0f;
        const Rectangle track = {fieldX, rowY() + 3.0f, sw, sh};
        bool toggled = false;
        drawOnOffSwitch(font, track, reputationChange, "Reputation change", false, toggled);
        Rectangle deltaRect = {fieldX + fieldW - 80.0f, rowY() + 2.0f, 80.0f, 24.0f};
        const bool focused = focusField == kFocusRepDelta;
        DrawRectangleRec(
            deltaRect, focused ? Color{50, 48, 62, 255} : Color{24, 22, 32, 255});
        DrawRectangleLinesEx(
            deltaRect, focused ? 2.0f : 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
        const std::string shown = focused
            ? (focusDraft.empty() ? "|" : focusDraft)
            : formatSignedInt(reputationDelta);
        DrawTextEx(
            font,
            shown.c_str(),
            {deltaRect.x + 4.0f, deltaRect.y + 4.0f},
            kFontTiny,
            1.0f,
            kTextPrimary);
        if (canClick && CheckCollisionPointRec(mouse, scrollClip))
        {
            const Rectangle hit = {fieldX, rowY(), fieldW - 88.0f, rowH};
            if (CheckCollisionPointRec(mouse, hit))
                reputationChange = !reputationChange;
            if (CheckCollisionPointRec(mouse, deltaRect))
                beginNumericFocus(kFocusRepDelta, formatSignedInt(reputationDelta));
        }
        advance(rowH + 8.0f);
    }

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
    numericField(kFocusMoney, "Requires money ($)", formatFloat(requiresMoney));
    numericField(
        kFocusInsuffMoney, "Requires insufficient money ($)", formatFloat(requiresInsufficientMoney));
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
    stringField(
        kFocusStoryFlag, "Requires story flag", requiresStoryFlag, "(none)");
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
        const int visible = std::min(kVoiceMenuVisibleRows, voiceCount);
        const float maxVScroll =
            static_cast<float>(std::max(0, voiceCount - visible));
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
            if (i < 0 || i >= voiceCount)
                break;
            const std::string& v = voices[static_cast<size_t>(i)];
            Rectangle r = {
                voiceMenuRect.x + 2.0f, my, voiceMenuRect.width - 4.0f, rowH - 2.0f};
            if (CheckCollisionPointRec(mouse, r))
            {
                DrawRectangleRec(r, Color{60, 54, 72, 220});
                if (canClick)
                {
                    activeBag().ttsVoice = normalizeVoiceId(v);
                    voiceMenuOpen = false;
                }
            }
            DrawTextEx(font, v.c_str(), {r.x + 6.0f, r.y + 2.0f}, kFontSmall, 1.0f, kTextPrimary);
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
