#include "SceneLoader.h"
#include "MaskEvaluator.h"
#include "SceneOverlayDef.h"
#include "ImageCompression.h"
#include <PlatformPath.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <raylib.h>
#include <vector>

namespace highline_ridge
{

namespace
{

Font loadGameFont(const std::string& assetRoot, const std::string& fontPath)
{
    std::vector<std::string> paths;
    auto tryAdd = [&](const std::string& candidate)
    {
        if (candidate.empty())
            return;

        if (std::find(paths.begin(), paths.end(), candidate) == paths.end())
            paths.push_back(candidate);
    };

    tryAdd(fontPath);
    tryAdd(resolveAssetPath(assetRoot, fontPath));
    tryAdd(resolveAssetPath(".", fontPath));
    tryAdd(resolveAssetPath("..", fontPath));

    const char* appDir = GetApplicationDirectory();
    if (appDir != nullptr && appDir[0] != '\0')
    {
        const std::string appDirectory(appDir);
        tryAdd(resolveAssetPath(appDirectory, fontPath));
        tryAdd(resolveAssetPath(appDirectory + "/..", fontPath));
    }

    for (const std::string& path : paths)
    {
        if (!FileExists(path.c_str()))
            continue;

        Font font = LoadFontEx(path.c_str(), 64, nullptr, 0);
        if (font.texture.id != 0)
        {
            TraceLog(LOG_INFO, "Loaded game font: %s", path.c_str());
            return font;
        }
    }

    return Font{};
}

bool parseMovement(const nlohmann::json& movement, MovementStruct& out)
{
    if (!movement.is_object())
        return false;

    out.up = movement.value("up", false);
    out.down = movement.value("down", false);
    out.forward = movement.value("forward", false);
    out.backward = movement.value("backward", false);
    out.left = movement.value("left", false);
    out.right = movement.value("right", false);
    return true;
}

bool parseActions(const nlohmann::json& actions, ActionStruct& out)
{
    if (!actions.is_object())
        return false;

    out.examine = actions.value("examine", false);
    out.speak = actions.value("speak", false);
    out.hit = actions.value("hit", false);
    out.use = actions.value("use", false);
    out.take = actions.value("take", false);
    return true;
}

std::string formatActorDisplayName(const std::string& actorId)
{
    if (actorId.empty())
        return "";

    std::string formatted;
    formatted.reserve(actorId.size());
    bool capitalizeNext = true;

    for (char character : actorId)
    {
        if (character == '_')
        {
            formatted.push_back(' ');
            capitalizeNext = true;
            continue;
        }

        if (capitalizeNext)
        {
            formatted.push_back((char)std::toupper((unsigned char)character));
            capitalizeNext = false;
        }
        else
            formatted.push_back(character);
    }

    return formatted;
}

std::string normalizeActorId(const std::string& rawId)
{
    if (rawId.empty())
        return "";

    static const char* kSuffixes[] = { "_path", "_fedora" };
    for (const char* suffix : kSuffixes)
    {
        const size_t suffixLength = std::strlen(suffix);
        if (rawId.size() > suffixLength
            && rawId.compare(rawId.size() - suffixLength, suffixLength, suffix) == 0)
            return rawId.substr(0, rawId.size() - suffixLength);
    }

    return rawId;
}

bool parseActorFields(const nlohmann::json& node, std::string& actorId, std::string& actorName)
{
    actorId.clear();
    actorName.clear();

    if (!node.is_object())
        return true;

    const nlohmann::json& actorNode = node.value("actor", nlohmann::json());
    if (actorNode.is_string())
        actorId = actorNode.get<std::string>();
    else if (actorNode.is_object())
    {
        actorId = actorNode.value("id", "");
        actorName = actorNode.value("name", "");
    }

    if (actorName.empty())
        actorName = node.value("actorName", "");

    return true;
}

void registerSceneActor(
    std::map<std::string, SceneActorDef>& actors,
    const std::string& rawId,
    const std::string& explicitName)
{
    const std::string actorId = normalizeActorId(rawId);
    if (actorId.empty())
        return;

    const std::string displayName = !explicitName.empty()
        ? explicitName
        : formatActorDisplayName(actorId);

    std::map<std::string, SceneActorDef>::iterator existing = actors.find(actorId);
    if (existing == actors.end())
        actors[actorId] = { actorId, displayName };
    else if (!explicitName.empty())
        existing->second.name = explicitName;
}

void collectSceneActorsFromConfig(
    const SceneSpeakConfig& config,
    std::vector<SceneActorDef>& out)
{
    std::map<std::string, SceneActorDef> actors;

    for (const ConversationPhase& phase : config.phases)
    {
        if (phase.type == ConversationPhaseType::Random)
        {
            for (const RandomConversationLine& line : phase.lines)
            {
                const std::string rawId = !line.actorId.empty() ? line.actorId : line.id;
                registerSceneActor(actors, rawId, line.actorName);
            }
            continue;
        }

        const std::string rawId = !phase.actorId.empty() ? phase.actorId : phase.id;
        registerSceneActor(actors, rawId, phase.actorName);
    }

    out.clear();
    out.reserve(actors.size());
    for (std::map<std::string, SceneActorDef>::const_iterator it = actors.begin();
         it != actors.end();
         ++it)
        out.push_back(it->second);

    std::sort(
        out.begin(),
        out.end(),
        [](const SceneActorDef& a, const SceneActorDef& b)
        {
            return a.name < b.name;
        });
}

bool parseStatusEffect(const nlohmann::json& status, StatusEffect& out)
{
    if (!status.is_object())
        return true;

    out.key = status.value("key", "");
    out.health = status.value("health", 0.0f);
    out.energy = status.value("energy", 0.0f);
    out.resolve = status.value("resolve", status.value("tenacity", 0.0f));
    out.lucidity = status.value("lucidity", 0.0f);
    out.charisma = status.value("charisma", 0.0f);
    out.money = status.value("money", 0.0f);
    out.actorOpinion = status.value("actorOpinion", 0);
    out.actorTab = status.value("actorTab", 0.0f);
    out.repeat = status.value("repeat", false);
    out.onZeroLucidity = status.value("onZeroLucidity", "");

    const nlohmann::json& actorNode = status.value("actor", nlohmann::json());
    if (actorNode.is_string())
        out.actor = actorNode.get<std::string>();
    else if (actorNode.is_object())
        out.actor = actorNode.value("id", "");

    return true;
}

ConversationPhaseType parsePhaseType(const std::string& type)
{
    if (type == "scripted")
        return ConversationPhaseType::Scripted;
    if (type == "random")
        return ConversationPhaseType::Random;
    return ConversationPhaseType::Once;
}

std::string formatChoiceLabel(const std::string& label)
{
    if (label.size() >= 2 && label.compare(0, 2, "> ") == 0)
        return label;
    return "> " + label;
}

std::string parseOptionalAudioField(const nlohmann::json& object, const char* key)
{
    if (!object.contains(key) || !object[key].is_string())
        return "";

    return object.value(key, "");
}

std::vector<std::string> parseStringArrayField(const nlohmann::json& object, const char* key)
{
    std::vector<std::string> values;
    if (!object.contains(key) || !object[key].is_array())
        return values;

    for (const nlohmann::json& entry : object[key])
    {
        if (entry.is_string() && !entry.get<std::string>().empty())
            values.push_back(entry.get<std::string>());
    }

    return values;
}

bool parseGrantedInventoryItem(const nlohmann::json& item, GrantedInventoryItemDef& out)
{
    if (!item.is_object())
        return true;

    out.id = item.value("id", "");
    out.name = item.value("name", "");
    out.iconPath = item.value("icon", "");
    out.examineImagePath = item.value("examineImage", "");
    out.examineText = item.value("examineText", "");
    return true;
}

void parseDialogRequirementFields(const nlohmann::json& node, DialogRequirementFields& out)
{
    out.minDay = node.value("minDay", 0);
    out.maxDay = node.value("maxDay", 0);
    out.requiresLucidityBelow = node.value("requiresLucidityBelow", 0.0f);
    out.requiresLucidityAbove = node.value("requiresLucidityAbove", 0.0f);
    out.requiresRecentCollapse = node.value("requiresRecentCollapse", false);
    out.requiresRecentSleep = node.value("requiresRecentSleep", false);
    out.requiresDaysSinceFlag = node.value("requiresDaysSinceFlag", "");
    out.requiresDaysSinceMin = node.value("requiresDaysSinceMin", 0);
    out.requiresDaysSinceMax = node.value("requiresDaysSinceMax", 0);

    if (node.contains("requiresDaysSince") && node["requiresDaysSince"].is_object())
    {
        const nlohmann::json& since = node["requiresDaysSince"];
        out.requiresDaysSinceFlag = since.value("flag", out.requiresDaysSinceFlag);
        out.requiresDaysSinceMin = since.value("min", out.requiresDaysSinceMin);
        out.requiresDaysSinceMax = since.value("max", out.requiresDaysSinceMax);
    }

    out.opinionActor = node.value("opinionActor", "");
    if (node.contains("requiresActorOpinionAtLeast"))
    {
        out.hasActorOpinionAtLeast = true;
        out.requiresActorOpinionAtLeast = node.value("requiresActorOpinionAtLeast", 0);
    }
    if (node.contains("requiresActorOpinionAtMost"))
    {
        out.hasActorOpinionAtMost = true;
        out.requiresActorOpinionAtMost = node.value("requiresActorOpinionAtMost", 0);
    }
}

bool parseConversationChoice(const nlohmann::json& choice, ConversationChoiceDef& out)
{
    if (!choice.is_object())
        return false;

    out.id = choice.value("id", "");
    out.label = formatChoiceLabel(choice.value("label", ""));
    out.response = choice.value("response", "");
    out.responseAudio = parseOptionalAudioField(choice, "responseAudio");
    out.sketchPath = choice.value("sketch", "");
    out.tts = choice.value("tts", false);
    out.ttsVoice = choice.value("ttsVoice", "");
    out.ttsText = choice.value("ttsText", "");
    out.ttsAudio = choice.value("ttsAudio", "");
    out.ttsAudioSegments = parseStringArrayField(choice, "ttsAudioSegments");
    out.ttsAfter = choice.value("ttsAfter", false);
    out.ttsAfterVoice = choice.value("ttsAfterVoice", "");
    out.ttsAfterText = choice.value("ttsAfterText", "");
    out.ttsAfterAudio = choice.value("ttsAfterAudio", "");
    out.ttsAfterAudioSegments = parseStringArrayField(choice, "ttsAfterAudioSegments");
    if (!parseStatusEffect(choice.value("status", nlohmann::json::object()), out.status))
        return false;

    if (!parseGrantedInventoryItem(choice.value("grantItem", nlohmann::json::object()), out.grantItem))
        return false;

    out.requiresMoney = choice.value("requiresMoney", 0.0f);
    out.requiresInsufficientMoney = choice.value("requiresInsufficientMoney", 0.0f);
    out.tabActor = choice.value("tabActor", "");
    out.requiresPositiveActorTab = choice.value("requiresPositiveActorTab", false);
    out.requiresPayActorTabInFull = choice.value("requiresPayActorTabInFull", false);
    out.requiresInsufficientForActorTab = choice.value("requiresInsufficientForActorTab", false);
    out.payActorTabInFull = choice.value("payActorTabInFull", false);
    out.requiresSaloonRoomAvailable = choice.value("requiresSaloonRoomAvailable", false);
    out.closePhase = choice.value("closePhase", true);
    out.consumeOnSelect = choice.value("consumeOnSelect", false);
    out.persistConsumed = choice.value("persistConsumed", false);
    out.resumeTopLevel = choice.value("resumeTopLevel", false);
    out.resumeChoiceId = choice.value("resumeChoiceId", "");
    out.grantStoryFlag = choice.value("grantStoryFlag", "");
    out.startPhase = choice.value("startPhase", "");
    out.skipIntroOnStartPhase = choice.value("skipIntroOnStartPhase", false);

    if (!parseOverlaySequence(choice.value("overlaySequence", nlohmann::json::array()), out.overlaySequence))
        return false;

    out.followUpChoices.clear();
    const nlohmann::json& followUps = choice.value("choices", nlohmann::json::array());
    if (!followUps.is_array())
        return false;

    for (const nlohmann::json& followUp : followUps)
    {
        ConversationChoiceDef parsed;
        if (!parseConversationChoice(followUp, parsed))
            return false;
        out.followUpChoices.push_back(parsed);
    }

    return !out.id.empty() && !out.label.empty()
        && (!out.response.empty() || out.closePhase);
}

bool parseRandomLine(const nlohmann::json& line, RandomConversationLine& out)
{
    if (!line.is_object())
        return false;

    out.id = line.value("id", "");
    if (!parseActorFields(line, out.actorId, out.actorName))
        return false;
    parseDialogRequirementFields(line, out.requirements);
    out.text = line.value("text", "");
    out.sketchPath = line.value("sketch", "");
    out.audio = parseOptionalAudioField(line, "audio");
    out.tts = line.value("tts", false);
    out.ttsVoice = line.value("ttsVoice", "");
    out.ttsText = line.value("ttsText", "");
    out.ttsAudio = line.value("ttsAudio", "");
    out.ttsAudioSegments = parseStringArrayField(line, "ttsAudioSegments");
    out.ttsAfter = line.value("ttsAfter", false);
    out.ttsAfterVoice = line.value("ttsAfterVoice", "");
    out.ttsAfterText = line.value("ttsAfterText", "");
    out.ttsAfterAudio = line.value("ttsAfterAudio", "");
    out.ttsAfterAudioSegments = parseStringArrayField(line, "ttsAfterAudioSegments");
    out.weight = line.value("weight", 1);
    out.once = line.value("once", false);
    out.grantStoryFlag = line.value("grantStoryFlag", "");
    out.allowAttack = line.value("allowAttack", false);
    out.attackEncounterId = line.value("attackEncounterId", "");
    if (!parseStatusEffect(line.value("status", nlohmann::json::object()), out.status))
        return false;

    out.choices.clear();
    const nlohmann::json& choices = line.value("choices", nlohmann::json::array());
    if (!choices.is_array())
        return false;

    for (const nlohmann::json& choice : choices)
    {
        ConversationChoiceDef parsed;
        if (!parseConversationChoice(choice, parsed))
            return false;
        out.choices.push_back(parsed);
    }

    return !out.text.empty();
}

bool parseConversationPhase(const nlohmann::json& phase, ConversationPhase& out)
{
    if (!phase.is_object())
        return false;

    out.id = phase.value("id", "");
    if (!parseActorFields(phase, out.actorId, out.actorName))
        return false;
    out.type = parsePhaseType(phase.value("type", "once"));
    out.requiresPhaseId = phase.value("requiresPhase", "");
    out.requiresFlag = phase.value("requiresFlag", "");
    parseDialogRequirementFields(phase, out.requirements);
    out.resetOnSceneEnter = phase.value("resetOnSceneEnter", true);
    out.repeatable = phase.value("repeatable", false);
    out.text = phase.value("text", "");
    out.audio = parseOptionalAudioField(phase, "audio");
    out.intro = phase.value("intro", "");
    out.introAudio = parseOptionalAudioField(phase, "introAudio");
    out.sketchPath = phase.value("sketch", "");
    out.tts = phase.value("tts", false);
    out.ttsVoice = phase.value("ttsVoice", "");
    out.ttsText = phase.value("ttsText", "");
    out.ttsAudio = phase.value("ttsAudio", "");
    out.ttsAudioSegments = parseStringArrayField(phase, "ttsAudioSegments");
    out.ttsAfter = phase.value("ttsAfter", false);
    out.ttsAfterVoice = phase.value("ttsAfterVoice", "");
    out.ttsAfterText = phase.value("ttsAfterText", "");
    out.ttsAfterAudio = phase.value("ttsAfterAudio", "");
    out.ttsAfterAudioSegments = parseStringArrayField(phase, "ttsAfterAudioSegments");
    out.resumeTts = phase.value("resumeTts", false);
    out.resumeTtsVoice = phase.value("resumeTtsVoice", "");
    out.resumeTtsText = phase.value("resumeTtsText", "");
    out.resumeTtsAudio = phase.value("resumeTtsAudio", "");
    out.resumeIntro = phase.value("resumeIntro", "");
    out.resumeIntroAudio = parseOptionalAudioField(phase, "resumeIntroAudio");
    out.poolId = phase.value("poolId", "");
    out.avoidRepeat = phase.value("avoidRepeat", true);

    if (!parseStatusEffect(phase.value("status", nlohmann::json::object()), out.status))
        return false;

    if (!parseGrantedInventoryItem(phase.value("grantItem", nlohmann::json::object()), out.grantItem))
        return false;

    out.choices.clear();
    const nlohmann::json& choices = phase.value("choices", nlohmann::json::array());
    if (!choices.is_array())
        return false;

    for (const nlohmann::json& choice : choices)
    {
        ConversationChoiceDef parsed;
        if (!parseConversationChoice(choice, parsed))
            return false;
        out.choices.push_back(parsed);
    }

    out.lines.clear();
    const nlohmann::json& lines = phase.value("lines", nlohmann::json::array());
    if (!lines.is_array())
        return false;

    for (const nlohmann::json& line : lines)
    {
        RandomConversationLine parsed;
        if (!parseRandomLine(line, parsed))
            return false;
        out.lines.push_back(parsed);
    }

    if (out.id.empty())
        return false;

    if (out.type == ConversationPhaseType::Once && out.text.empty())
        return false;

    if (out.type == ConversationPhaseType::Scripted &&
        (out.intro.empty() || out.choices.empty()))
        return false;

    if (out.type == ConversationPhaseType::Random && out.lines.empty())
        return false;

    return true;
}

bool parseSpeakPhasesArray(const nlohmann::json& phases, SceneSpeakConfig& out)
{
    if (!phases.is_array())
        return false;

    out.phases.clear();
    for (const nlohmann::json& phase : phases)
    {
        ConversationPhase parsed;
        if (!parseConversationPhase(phase, parsed))
            return false;
        out.phases.push_back(parsed);
    }

    return true;
}

bool parseSpeakConfig(const nlohmann::json& sceneJson, SceneSpeakConfig& out)
{
    out.phases.clear();

    if (sceneJson.contains("conversations") &&
        sceneJson["conversations"].is_object() &&
        sceneJson["conversations"].contains("speakPhases"))
    {
        return parseSpeakPhasesArray(sceneJson["conversations"]["speakPhases"], out);
    }

    if (sceneJson.contains("speakPhases"))
        return parseSpeakPhasesArray(sceneJson["speakPhases"], out);

    const std::string speakDetails = sceneJson.value("speakDetails", "");
    if (speakDetails.empty())
        return true;

    ConversationPhase legacy;
    legacy.id = "speak";
    legacy.type = ConversationPhaseType::Once;
    legacy.text = speakDetails;
    legacy.resetOnSceneEnter = true;
    if (!parseStatusEffect(sceneJson.value("speakStatus", nlohmann::json::object()), legacy.status))
        return false;
    out.phases.push_back(legacy);
    return true;
}

bool applyConversationOverlays(
    const nlohmann::json& overlays,
    std::map<std::string, SceneData>& scenes)
{
    if (!overlays.is_object())
        return true;

    for (auto it = overlays.begin(); it != overlays.end(); ++it)
    {
        std::map<std::string, SceneData>::iterator sceneIt = scenes.find(it.key());
        if (sceneIt == scenes.end())
        {
            TraceLog(LOG_ERROR, "Conversation overlay references unknown scene '%s'", it.key().c_str());
            return false;
        }

        if (!parseSpeakConfig(it.value(), sceneIt->second.speakConfig))
        {
            TraceLog(LOG_ERROR, "Failed to parse speak config for scene '%s'", it.key().c_str());
            return false;
        }
    }

    return true;
}

std::string siblingConfigPath(const std::string& configPath, const std::string& filename)
{
    const size_t slash = configPath.find_last_of('/');
    if (slash == std::string::npos)
        return filename;
    return configPath.substr(0, slash + 1) + filename;
}

std::string resolveConversationsPath(const std::string& configPath, const std::string& assetRoot)
{
    std::vector<std::string> candidates;
    auto addCandidate = [&](const std::string& path)
    {
        if (path.empty())
            return;

        if (std::find(candidates.begin(), candidates.end(), path) == candidates.end())
            candidates.push_back(path);
    };

    addCandidate(siblingConfigPath(configPath, "conversations.json"));
    addCandidate(pathJoin(assetRoot, "resources/conversations.json"));
    addCandidate(pathJoin(assetRoot, "../resources/conversations.json"));

    std::string newestPath;
    std::filesystem::file_time_type newestTime{};
    bool found = false;

    for (const std::string& candidate : candidates)
    {
        if (!FileExists(candidate.c_str()))
            continue;

        std::error_code error;
        const std::filesystem::file_time_type writeTime =
            std::filesystem::last_write_time(candidate, error);
        if (error)
            continue;

        if (!found || writeTime > newestTime)
        {
            found = true;
            newestTime = writeTime;
            newestPath = candidate;
        }
    }

    return newestPath;
}

bool parseAudioClipDef(
    const nlohmann::json& clip,
    AudioClipDef& out,
    bool defaultLoop,
    const std::string& defaultTrigger = "")
{
    if (!clip.is_object())
        return false;

    out.path = clip.value("path", clip.value("file", ""));
    if (out.path.empty())
        return false;

    out.volume = clip.value("volume", 1.0f);
    out.fadeIn = clip.value("fade_in", clip.value("fadeIn", 0.0f));
    out.fadeOut = clip.value("fade_out", clip.value("fadeOut", 0.0f));
    out.loop = clip.value("loop", defaultLoop);
    out.trigger = clip.value("trigger", defaultTrigger);

    out.numericAttributes.clear();
    out.boolAttributes.clear();
    out.stringAttributes.clear();

    for (auto it = clip.begin(); it != clip.end(); ++it)
    {
        const std::string& key = it.key();
        if (key == "path" || key == "file" || key == "volume" ||
            key == "fade_in" || key == "fadeIn" || key == "fade_out" || key == "fadeOut" ||
            key == "loop" || key == "trigger")
        {
            continue;
        }

        if (it.value().is_number_float() || it.value().is_number_integer())
            out.numericAttributes[key] = it.value().get<float>();
        else if (it.value().is_boolean())
            out.boolAttributes[key] = it.value().get<bool>();
        else if (it.value().is_string())
            out.stringAttributes[key] = it.value().get<std::string>();
    }

    return true;
}

bool parseRoomAudio(const nlohmann::json& audio, RoomAudioConfig& out)
{
    out = RoomAudioConfig{};
    if (!audio.is_object())
        return true;

    if (audio.contains("music"))
    {
        AudioClipDef musicClip;
        if (!parseAudioClipDef(audio["music"], musicClip, true))
            return false;
        out.music = musicClip;
        out.hasMusic = true;
    }

    const nlohmann::json& ambient = audio.value("ambient", nlohmann::json::array());
    if (!ambient.is_array())
        return false;

    for (const nlohmann::json& ambientClip : ambient)
    {
        AudioClipDef parsed;
        if (!parseAudioClipDef(ambientClip, parsed, true))
            return false;
        out.ambient.push_back(parsed);
    }

    const nlohmann::json& sfx = audio.value("sfx", nlohmann::json::array());
    if (!sfx.is_array())
        return false;

    for (const nlohmann::json& sfxClip : sfx)
    {
        AudioClipDef parsed;
        if (!parseAudioClipDef(sfxClip, parsed, false, "on_enter"))
            return false;
        out.sfx.push_back(parsed);
    }

    return true;
}

bool parseExits(const nlohmann::json& exits, std::map<std::string, std::string>& out)
{
    out.clear();
    if (!exits.is_object())
        return true;

    for (auto it = exits.begin(); it != exits.end(); ++it)
    {
        if (it.value().is_string())
            out[it.key()] = it.value().get<std::string>();
    }

    return true;
}

bool parseExitRequirement(const nlohmann::json& requirement, ExitRequirementDef& out)
{
    if (!requirement.is_object())
        return false;

    out.requiresLightSource = requirement.value(
        "requiresLightSource",
        requirement.value("requires_light_source", false));
    out.requiresRoomPurchasedToday = requirement.value(
        "requiresRoomPurchasedToday",
        requirement.value("requires_room_purchased_today", false));
    out.requiresInventoryItem = requirement.value(
        "requiresInventoryItem",
        requirement.value("requires_inventory_item", ""));
    out.requiresInventoryItems.clear();
    const nlohmann::json inventoryItems = requirement.value(
        "requiresInventoryItems",
        requirement.value("requires_inventory_items", nlohmann::json::array()));
    if (inventoryItems.is_array())
    {
        for (const nlohmann::json& itemId : inventoryItems)
        {
            if (itemId.is_string())
                out.requiresInventoryItems.push_back(itemId.get<std::string>());
        }
    }
    out.requiresStoryFlag = requirement.value(
        "requiresStoryFlag",
        requirement.value("requires_story_flag", ""));
    out.blockedDetails = requirement.value(
        "blockedDetails",
        requirement.value("blocked_details", ""));
    return out.requiresLightSource
        || out.requiresRoomPurchasedToday
        || !out.requiresInventoryItem.empty()
        || !out.requiresInventoryItems.empty()
        || !out.requiresStoryFlag.empty()
        || !out.blockedDetails.empty();
}

bool parseExitRequirements(
    const nlohmann::json& requirements,
    std::map<std::string, ExitRequirementDef>& out)
{
    out.clear();
    if (!requirements.is_object())
        return true;

    for (auto it = requirements.begin(); it != requirements.end(); ++it)
    {
        ExitRequirementDef parsed;
        if (!parseExitRequirement(it.value(), parsed))
            return false;
        out[it.key()] = parsed;
    }

    return true;
}

bool parseTakeableItem(const nlohmann::json& item, TakeableItemDef& out)
{
    if (!item.is_object())
        return false;

    out.id = item.value("id", "");
    out.name = item.value("name", "");
    out.iconPath = item.value("icon", "");
    out.examineImagePath = item.value("examineImage", "");
    out.examineText = item.value("examineText", "");
    out.requiresExamine = item.value("requiresExamine", true);
    out.requiresStoryFlag = item.value("requiresStoryFlag", "");

    return !out.id.empty();
}

bool parseTakeables(const nlohmann::json& takeables, std::vector<TakeableItemDef>& out)
{
    out.clear();
    if (!takeables.is_array())
        return true;

    for (const nlohmann::json& item : takeables)
    {
        TakeableItemDef parsed;
        if (!parseTakeableItem(item, parsed))
            return false;
        out.push_back(parsed);
    }

    return true;
}

bool parseInteraction(const nlohmann::json& interaction, SceneInteractionDef& out)
{
    if (!interaction.is_object())
        return false;

    out.id = interaction.value("id", "");
    out.label = interaction.value("label", "");
    out.useDetails = interaction.value("useDetails", "");
    out.sketchPath = interaction.value("sketch", "");
    out.exitSceneId = interaction.value("exitSceneId", "");
    out.useFlag = interaction.value("useFlag", "");
    out.hideWhenStoryFlag = interaction.value("hideWhenStoryFlag", "");
    out.useHealthDelta = interaction.value("useHealthDelta", 0.0f);
    out.useEnergyDelta = interaction.value("useEnergyDelta", 0.0f);
    out.useResolveDelta = interaction.value("useResolveDelta", 0.0f);
    out.useLucidityDelta = interaction.value("useLucidityDelta", 0.0f);
    out.useCharismaDelta = interaction.value("useCharismaDelta", 0.0f);
    out.repeat = interaction.value("repeat", false);
    out.oncePerDay = interaction.value("oncePerDay", false);
    out.requiresExamine = interaction.value("requiresExamine", true);
    out.advancesDay = interaction.value("advancesDay", false);
    out.requiresAnyInventoryItems.clear();
    const nlohmann::json anyInventoryItems = interaction.value(
        "requiresAnyInventoryItems",
        nlohmann::json::array());
    if (anyInventoryItems.is_array())
    {
        for (const nlohmann::json& itemId : anyInventoryItems)
        {
            if (itemId.is_string())
                out.requiresAnyInventoryItems.push_back(itemId.get<std::string>());
        }
    }

    if (!parseGrantedInventoryItem(interaction.value("grantItem", nlohmann::json::object()), out.grantItem))
        return false;

    if (!parseOverlaySequence(interaction.value("overlaySequence", nlohmann::json::array()), out.overlaySequence))
        return false;

    out.tts = interaction.value("tts", false);
    out.ttsVoice = interaction.value("ttsVoice", "");
    out.ttsText = interaction.value("ttsText", "");
    out.ttsAudio = interaction.value("ttsAudio", "");
    out.ttsAfter = interaction.value("ttsAfter", false);
    out.ttsAfterVoice = interaction.value("ttsAfterVoice", "");
    out.ttsAfterText = interaction.value("ttsAfterText", "");
    out.ttsAfterAudio = interaction.value("ttsAfterAudio", "");
    out.ttsVariantFlag = interaction.value("ttsVariantFlag", "");
    out.ttsVariantText = interaction.value("ttsVariantText", "");
    out.ttsVariantAudio = interaction.value("ttsVariantAudio", "");

    return !out.id.empty() && !out.label.empty();
}

bool parseAlternateImage(const nlohmann::json& alternate, AlternateImageDef& out)
{
    if (!alternate.is_object())
        return false;

    out.imagePath = alternate.value("image", "");
    out.flag = alternate.value("flag", "");
    out.untilPhase = alternate.value("untilPhase", alternate.value("until_phase", ""));
    return !out.imagePath.empty();
}

bool parseAlternateImages(const nlohmann::json& alternates, std::vector<AlternateImageDef>& out)
{
    out.clear();
    if (!alternates.is_array())
        return true;

    for (const nlohmann::json& alternate : alternates)
    {
        AlternateImageDef parsed;
        if (!parseAlternateImage(alternate, parsed))
            return false;
        out.push_back(parsed);
    }

    return true;
}

bool parseInteractions(const nlohmann::json& interactions, std::vector<SceneInteractionDef>& out)
{
    out.clear();
    if (!interactions.is_array())
        return true;

    for (const nlohmann::json& interaction : interactions)
    {
        SceneInteractionDef parsed;
        if (!parseInteraction(interaction, parsed))
            return false;
        out.push_back(parsed);
    }

    return true;
}

bool movementStructIsEmpty(const MovementStruct& movement)
{
    return !movement.up && !movement.down && !movement.forward
        && !movement.backward && !movement.left && !movement.right;
}

bool actionStructIsEmpty(const ActionStruct& actions)
{
    return !actions.examine && !actions.speak && !actions.hit && !actions.use && !actions.take;
}

MaskCondition maskConditionFromFlag(const std::string& flag)
{
    MaskCondition condition;
    condition.type = MaskConditionType::Flag;
    condition.value = flag;
    return condition;
}

bool parseMaskConditionNode(const nlohmann::json& node, MaskCondition& out)
{
    if (!node.is_object())
        return false;

    if (node.contains("all") && node["all"].is_array())
    {
        out.type = MaskConditionType::All;
        for (const nlohmann::json& child : node["all"])
        {
            MaskCondition parsed;
            if (!parseMaskConditionNode(child, parsed))
                return false;
            out.children.push_back(parsed);
        }
        return true;
    }

    if (node.contains("any") && node["any"].is_array())
    {
        out.type = MaskConditionType::Any;
        for (const nlohmann::json& child : node["any"])
        {
            MaskCondition parsed;
            if (!parseMaskConditionNode(child, parsed))
                return false;
            out.children.push_back(parsed);
        }
        return true;
    }

    if (node.contains("flag"))
    {
        out.type = MaskConditionType::Flag;
        out.value = node.value("flag", "");
        return !out.value.empty();
    }

    if (node.contains("playerHasItem"))
    {
        out.type = MaskConditionType::PlayerHasItem;
        out.value = node.value("playerHasItem", "");
        return !out.value.empty();
    }

    if (node.contains("playerHasItemFlag"))
    {
        out.type = MaskConditionType::PlayerHasItemFlag;
        out.value = node.value("playerHasItemFlag", "");
        return !out.value.empty();
    }

    if (node.contains("milestoneStarted"))
    {
        out.type = MaskConditionType::MilestoneStarted;
        out.value = node.value("milestoneStarted", "");
        return !out.value.empty();
    }

    if (node.contains("milestoneComplete"))
    {
        out.type = MaskConditionType::MilestoneComplete;
        out.value = node.value("milestoneComplete", "");
        return !out.value.empty();
    }

    if (node.contains("milestoneFailed"))
    {
        out.type = MaskConditionType::MilestoneFailed;
        out.value = node.value("milestoneFailed", "");
        return !out.value.empty();
    }

    if (node.contains("subScene"))
    {
        out.type = MaskConditionType::SubSceneActive;
        out.value = node.value("subScene", "");
        return !out.value.empty();
    }

    return false;
}

bool parseMaskConditionField(const nlohmann::json& node, const char* field, MaskCondition& out)
{
    if (!node.contains(field))
        return true;

    return parseMaskConditionNode(node[field], out);
}

MilestoneStatus parseMilestoneWhen(const std::string& when)
{
    if (when == "started")
        return MilestoneStatus::Started;
    if (when == "failed")
        return MilestoneStatus::Failed;
    return MilestoneStatus::Complete;
}

bool parseMilestoneMaskHook(const nlohmann::json& node, MilestoneMaskHook& out)
{
    if (!node.is_object())
        return false;

    out.milestoneId = node.value("milestone", "");
    out.when = parseMilestoneWhen(node.value("when", "complete"));
    return !out.milestoneId.empty();
}

bool parseMovementMapping(const nlohmann::json& node, MovementMappingDef& out)
{
    if (!node.is_object())
        return false;

    out.id = node.value("id", "");
    const std::string target = node.value("target", "");
    out.target = parseMovementTarget(target);
    out.defaultMasked = node.value("masked", node.value("defaultMasked", false));
    out.exclusiveGroup = node.value("exclusiveGroup", "");

    if (!parseMaskConditionField(node, "unmaskWhen", out.unmaskWhen))
        return false;

    out.onMilestoneEnable.clear();
    const nlohmann::json enableNode = node.value(
        "onMilestoneEnable",
        node.value("on_milestone_enable", nlohmann::json()));
    if (enableNode.is_object())
    {
        MilestoneMaskHook hook;
        if (parseMilestoneMaskHook(enableNode, hook))
            out.onMilestoneEnable.push_back(hook);
    }
    else if (enableNode.is_array())
    {
        for (const nlohmann::json& entry : enableNode)
        {
            MilestoneMaskHook hook;
            if (!parseMilestoneMaskHook(entry, hook))
                return false;
            out.onMilestoneEnable.push_back(hook);
        }
    }

    out.onMilestoneDisable.clear();
    const nlohmann::json disableNode = node.value(
        "onMilestoneDisable",
        node.value("on_milestone_disable", nlohmann::json()));
    if (disableNode.is_object())
    {
        MilestoneMaskHook hook;
        if (parseMilestoneMaskHook(disableNode, hook))
            out.onMilestoneDisable.push_back(hook);
    }
    else if (disableNode.is_array())
    {
        for (const nlohmann::json& entry : disableNode)
        {
            MilestoneMaskHook hook;
            if (!parseMilestoneMaskHook(entry, hook))
                return false;
            out.onMilestoneDisable.push_back(hook);
        }
    }

    if (out.id.empty())
        out.id = target;

    return !out.id.empty() && !out.target.empty();
}

bool parseMovementExits(
    const nlohmann::json& movementExits,
    std::map<std::string, std::vector<MovementMappingDef>>& out)
{
    out.clear();
    if (!movementExits.is_object())
        return true;

    for (auto it = movementExits.begin(); it != movementExits.end(); ++it)
    {
        std::vector<MovementMappingDef> mappings;
        if (it.value().is_array())
        {
            for (const nlohmann::json& entry : it.value())
            {
                MovementMappingDef parsed;
                if (!parseMovementMapping(entry, parsed))
                    return false;
                mappings.push_back(parsed);
            }
        }
        else if (it.value().is_object())
        {
            MovementMappingDef parsed;
            if (!parseMovementMapping(it.value(), parsed))
                return false;
            mappings.push_back(parsed);
        }

        if (!mappings.empty())
            out[it.key()] = mappings;
    }

    return true;
}

bool parseNarrativeTts(const nlohmann::json& node, ItemTtsDef& out)
{
    if (!node.is_object())
        return true;

    out.enabled = node.value("tts", node.value("enabled", false));
    out.voice = node.value("ttsVoice", node.value("voice", ""));
    out.text = node.value("ttsText", node.value("text", ""));
    out.audio = node.value("ttsAudio", node.value("audio", ""));
    return true;
}

bool subSceneOverridesTts(const ItemTtsDef& tts)
{
    return tts.enabled || !tts.audio.empty() || !tts.text.empty();
}

ItemTtsDef pickSceneTts(const ItemTtsDef& sceneTts, const ItemTtsDef& subSceneTts)
{
    if (subSceneOverridesTts(subSceneTts))
        return subSceneTts;
    return sceneTts;
}

bool parseSubScene(
    const std::string& id,
    const nlohmann::json& node,
    SubSceneDef& out)
{
    if (!node.is_object())
        return false;

    out.id = id;
    out.imagePath = node.value("image", "");
    out.description = node.value("description", "");
    parseNarrativeTts(node.value("descriptionTts", nlohmann::json::object()), out.descriptionTts);
    out.examineDetails = node.value("examineDetails", "");
    parseNarrativeTts(node.value("examineTts", nlohmann::json::object()), out.examineTts);
    out.examineFlag = node.value("examineFlag", "");
    out.examineLucidityDelta = node.value("examineLucidityDelta", 0.0f);
    out.examineLucidityOncePerDay = node.value("examineLucidityOncePerDay", false);
    out.speakDetails = node.value("speakDetails", "");
    out.useDetails = node.value("useDetails", "");
    out.useHealthDelta = node.value("useHealthDelta", 0.0f);
    out.useEnergyDelta = node.value("useEnergyDelta", 0.0f);
    out.useRepeatStatus = node.value("useRepeatStatus", false);
    out.useRequiresExamine = node.value("useRequiresExamine", true);
    out.useAdvancesDay = node.value("advancesDay", false);
    out.useExit = node.value("useExit", "");
    out.focus = node.value("focus", false);

    if (!parseMovement(node.value("movement", nlohmann::json::object()), out.movement))
        return false;

    if (!parseActions(node.value("actions", nlohmann::json::object()), out.actions))
        return false;

    const nlohmann::json blanketNode = node.value(
        "movementMask",
        node.value("movementBlanket", nlohmann::json::object()));
    if (!parseMovement(blanketNode, out.movementBlanket))
        return false;

    if (!parseRoomAudio(node.value("audio", nlohmann::json::object()), out.audio))
        return false;

    out.flags.clear();
    const nlohmann::json flagsNode = node.value("flags", nlohmann::json::array());
    if (flagsNode.is_array())
    {
        for (const nlohmann::json& flag : flagsNode)
        {
            if (flag.is_string())
                out.flags.push_back(flag.get<std::string>());
        }
    }

    out.actorsPresent.clear();
    const nlohmann::json actorsNode = node.value(
        "actorsPresent",
        node.value("actors_present", nlohmann::json::array()));
    if (actorsNode.is_array())
    {
        for (const nlohmann::json& actor : actorsNode)
        {
            if (actor.is_string())
                out.actorsPresent.push_back(actor.get<std::string>());
        }
    }

    if (!parseInteractions(node.value("interactions", nlohmann::json::array()), out.interactions))
        return false;

    return true;
}

bool parseSubScenes(const nlohmann::json& subScenes, std::map<std::string, SubSceneDef>& out)
{
    out.clear();
    if (!subScenes.is_object())
        return true;

    for (auto it = subScenes.begin(); it != subScenes.end(); ++it)
    {
        SubSceneDef parsed;
        if (!parseSubScene(it.key(), it.value(), parsed))
            return false;
        out[it.key()] = parsed;
    }

    return true;
}

bool parseSubSceneRules(const nlohmann::json& rules, std::vector<SubSceneRuleDef>& out)
{
    out.clear();
    if (!rules.is_array())
        return true;

    for (const nlohmann::json& rule : rules)
    {
        if (!rule.is_object())
            return false;

        SubSceneRuleDef parsed;
        parsed.subSceneId = rule.value("subScene", "");
        parsed.isDefault = rule.value("default", false);
        parsed.untilPhase = rule.value("untilPhase", rule.value("until_phase", ""));
        parsed.applyOnEnter = rule.value("applyOnEnter", rule.value("apply_on_enter", true));
        if (!parseMaskConditionField(rule, "when", parsed.when))
            return false;

        if (parsed.subSceneId.empty())
            return false;

        out.push_back(parsed);
    }

    return true;
}

bool parseSceneInventory(
    const nlohmann::json& inventory,
    std::vector<SceneInventoryEntryDef>& out)
{
    out.clear();
    if (!inventory.is_array())
        return true;

    for (const nlohmann::json& entry : inventory)
    {
        if (!entry.is_object())
            return false;

        SceneInventoryEntryDef parsed;
        parsed.instance.defId = entry.value("defId", entry.value("id", ""));
        parsed.instance.instanceId = entry.value("instanceId", parsed.instance.defId);
        parsed.instance.quantity = entry.value("quantity", 1);
        parsed.instance.usedFraction = entry.value("usedFraction", 0.0f);
        parsed.infiniteQuantity = entry.value("infiniteQuantity", false);

        if (parsed.instance.defId.empty())
            return false;

        if (!parseMaskConditionField(entry, "unmaskWhen", parsed.unmaskWhen))
            return false;

        out.push_back(parsed);
    }

    return true;
}

void copyMovementBlanketDefaults(SubSceneDef& subScene)
{
    if (movementStructIsEmpty(subScene.movementBlanket))
        subScene.movementBlanket = subScene.movement;
}

void buildDefaultSubScene(SceneData& scene)
{
    SubSceneDef subScene;
    subScene.id = "default";
    subScene.imagePath = scene.imagePath;
    subScene.description = scene.description;
    subScene.descriptionTts = scene.descriptionTts;
    subScene.examineDetails = scene.examineDetails;
    subScene.examineTts = scene.examineTts;
    subScene.examineFlag = scene.examineFlag;
    subScene.examineLucidityDelta = scene.examineLucidityDelta;
    subScene.examineLucidityOncePerDay = scene.examineLucidityOncePerDay;
    subScene.speakDetails = scene.speakDetails;
    subScene.useDetails = scene.useDetails;
    subScene.useHealthDelta = scene.useHealthDelta;
    subScene.useEnergyDelta = scene.useEnergyDelta;
    subScene.useRepeatStatus = scene.useRepeatStatus;
    subScene.useRequiresExamine = scene.useRequiresExamine;
    subScene.useAdvancesDay = scene.useAdvancesDay;
    subScene.useExit = scene.useExit;
    subScene.movement = scene.movement;
    subScene.actions = scene.actions;
    subScene.movementBlanket = scene.movement;
    subScene.audio = scene.audio;
    subScene.interactions = scene.interactions;

    scene.subScenes[subScene.id] = subScene;
    if (scene.defaultSubSceneId.empty())
        scene.defaultSubSceneId = subScene.id;
}

bool sameMovementTarget(const MovementTarget& left, const MovementTarget& right)
{
    return left.sceneId == right.sceneId && left.subSceneId == right.subSceneId;
}

bool hasUnmaskedMappingToTarget(
    const std::vector<MovementMappingDef>& mappings,
    const MovementTarget& target)
{
    for (const MovementMappingDef& mapping : mappings)
    {
        if (!mapping.defaultMasked && sameMovementTarget(mapping.target, target))
            return true;
    }

    return false;
}

void buildMovementExitsFromLegacyExits(SceneData& scene)
{
    for (std::map<std::string, std::string>::const_iterator it = scene.exits.begin();
         it != scene.exits.end();
         ++it)
    {
        const MovementTarget legacyTarget = parseMovementTarget(it->second);
        const std::vector<MovementMappingDef>& existing = scene.movementExits[it->first];
        if (hasUnmaskedMappingToTarget(existing, legacyTarget))
            continue;

        MovementMappingDef mapping;
        mapping.id = it->first + "_to_" + it->second;
        mapping.target = legacyTarget;
        mapping.defaultMasked = false;

        std::map<std::string, ExitRequirementDef>::const_iterator requirementIt =
            scene.exitRequirements.find(it->first);
        if (requirementIt != scene.exitRequirements.end())
        {
            const ExitRequirementDef& requirement = requirementIt->second;
            if (!requirement.requiresStoryFlag.empty())
            {
                mapping.defaultMasked = true;
                mapping.unmaskWhen = maskConditionFromFlag(requirement.requiresStoryFlag);
            }
            else if (requirement.requiresLightSource)
            {
                mapping.defaultMasked = true;
                MaskCondition lightCondition;
                lightCondition.type = MaskConditionType::PlayerHasItemFlag;
                lightCondition.value = "light_source";
                mapping.unmaskWhen = lightCondition;
            }
            else if (!requirement.requiresInventoryItem.empty())
            {
                mapping.defaultMasked = true;
                MaskCondition itemCondition;
                itemCondition.type = MaskConditionType::PlayerHasItem;
                itemCondition.value = requirement.requiresInventoryItem;
                mapping.unmaskWhen = itemCondition;
            }
        }

        scene.movementExits[it->first].push_back(mapping);
    }
}

void buildSubScenesFromAlternates(SceneData& scene)
{
    if (scene.alternateImages.empty() || scene.subScenes.size() > 1)
        return;

    bool hasDefaultRule = false;
    for (const SubSceneRuleDef& rule : scene.subSceneRules)
    {
        if (rule.isDefault)
            hasDefaultRule = true;
    }

    if (!hasDefaultRule)
    {
        SubSceneRuleDef defaultRule;
        defaultRule.subSceneId = scene.defaultSubSceneId;
        defaultRule.isDefault = true;
        scene.subSceneRules.push_back(defaultRule);
    }

    for (const AlternateImageDef& alternate : scene.alternateImages)
    {
        if (alternate.flag.empty() || alternate.imagePath.empty())
            continue;

        const std::string subSceneId = "alt_" + alternate.flag;
        if (scene.subScenes.count(subSceneId) > 0)
            continue;

        SubSceneDef alternateSubScene = scene.subScenes[scene.defaultSubSceneId];
        alternateSubScene.id = subSceneId;
        alternateSubScene.imagePath = alternate.imagePath;
        scene.subScenes[subSceneId] = alternateSubScene;

        SubSceneRuleDef rule;
        rule.subSceneId = subSceneId;
        rule.when = maskConditionFromFlag(alternate.flag);
        scene.subSceneRules.push_back(rule);
    }
}

void migrateTakeablesToSceneInventory(SceneData& scene)
{
    if (!scene.sceneInventory.empty() || scene.takeables.empty())
        return;

    for (const TakeableItemDef& takeable : scene.takeables)
    {
        SceneInventoryEntryDef entry;
        entry.instance.defId = takeable.id;
        entry.instance.instanceId = takeable.id;
        if (!takeable.requiresStoryFlag.empty())
            entry.unmaskWhen = maskConditionFromFlag(takeable.requiresStoryFlag);
        scene.sceneInventory.push_back(entry);
    }
}

void compileSceneData(SceneData& scene)
{
    if (scene.subScenes.empty())
        buildDefaultSubScene(scene);

    if (scene.defaultSubSceneId.empty())
        scene.defaultSubSceneId = "default";

    for (std::map<std::string, SubSceneDef>::iterator it = scene.subScenes.begin();
         it != scene.subScenes.end();
         ++it)
    {
        if (movementStructIsEmpty(it->second.movement))
            it->second.movement = scene.movement;

        copyMovementBlanketDefaults(it->second);

        if (movementStructIsEmpty(it->second.movementBlanket))
            it->second.movementBlanket = it->second.movement;
    }

    buildMovementExitsFromLegacyExits(scene);
    buildSubScenesFromAlternates(scene);
    migrateTakeablesToSceneInventory(scene);
}

bool parseScene(const std::string& id, const nlohmann::json& sceneJson, SceneData& out)
{
    if (!sceneJson.is_object())
        return false;

    out.id = id;
    out.isStart = sceneJson.value("start", false);
    out.highAltitude = sceneJson.value("highAltitude", sceneJson.value("high_altitude", false));
    out.imagePath = sceneJson.value("image", "");
    out.alternateImagePath = sceneJson.value("alternateImage", "");
    out.alternateImageFlag = sceneJson.value("alternateImageFlag", "");
    out.alternateImageUntilPhase = sceneJson.value("alternateImageUntilPhase", "");
    if (!parseAlternateImages(sceneJson.value("alternateImages", nlohmann::json::array()), out.alternateImages))
        return false;

    if (!out.alternateImagePath.empty() && !out.alternateImageFlag.empty())
    {
        AlternateImageDef legacyAlternate;
        legacyAlternate.imagePath = out.alternateImagePath;
        legacyAlternate.flag = out.alternateImageFlag;
        legacyAlternate.untilPhase = out.alternateImageUntilPhase;
        out.alternateImages.push_back(legacyAlternate);
    }

    out.description = sceneJson.value("description", "");
    parseNarrativeTts(sceneJson.value("descriptionTts", nlohmann::json::object()), out.descriptionTts);
    out.wakeNarrative = sceneJson.value("wakeNarrative", "");
    parseNarrativeTts(sceneJson.value("wakeTts", nlohmann::json::object()), out.wakeTts);
    out.examineDetails = sceneJson.value("examineDetails", "");
    parseNarrativeTts(sceneJson.value("examineTts", nlohmann::json::object()), out.examineTts);
    out.examineFlag = sceneJson.value("examineFlag", "");
    out.examineLucidityDelta = sceneJson.value("examineLucidityDelta", 0.0f);
    out.examineLucidityOncePerDay = sceneJson.value("examineLucidityOncePerDay", false);
    out.speakDetails = sceneJson.value("speakDetails", "");
    out.useDetails = sceneJson.value("useDetails", "");
    out.useHealthDelta = sceneJson.value("useHealthDelta", 0.0f);
    out.useEnergyDelta = sceneJson.value("useEnergyDelta", 0.0f);
    out.useRepeatStatus = sceneJson.value("useRepeatStatus", false);
    out.useRequiresExamine = sceneJson.value("useRequiresExamine", true);
    out.useAdvancesDay = sceneJson.value("advancesDay", false);
    out.useExit = sceneJson.value("useExit", "");

    if (out.description.empty())
        return false;

    if (!parseMovement(sceneJson.value("movement", nlohmann::json::object()), out.movement))
        return false;

    if (!parseActions(sceneJson.value("actions", nlohmann::json::object()), out.actions))
        return false;

    if (!parseExits(sceneJson.value("exits", nlohmann::json::object()), out.exits))
        return false;

    if (!parseExitRequirements(
            sceneJson.value("exitRequirements", sceneJson.value("exit_requirements", nlohmann::json::object())),
            out.exitRequirements))
    {
        return false;
    }

    if (!parseSpeakConfig(sceneJson, out.speakConfig))
        return false;

    if (!parseRoomAudio(sceneJson.value("audio", nlohmann::json::object()), out.audio))
        return false;

    if (!parseTakeables(sceneJson.value("takeables", nlohmann::json::array()), out.takeables))
        return false;

    if (!parseInteractions(sceneJson.value("interactions", nlohmann::json::array()), out.interactions))
        return false;

    if (!parseSceneOverlays(sceneJson.value("overlays", nlohmann::json::array()), out.overlays))
        return false;

    out.defaultSubSceneId = sceneJson.value("defaultSubScene", "");

    if (!parseSubScenes(sceneJson.value("subScenes", nlohmann::json::object()), out.subScenes))
        return false;

    if (!parseSubSceneRules(sceneJson.value("subSceneRules", nlohmann::json::array()), out.subSceneRules))
        return false;

    if (!parseMovementExits(sceneJson.value("movementExits", nlohmann::json::object()), out.movementExits))
        return false;

    if (!parseSceneInventory(sceneJson.value("inventory", nlohmann::json::array()), out.sceneInventory))
        return false;

    compileSceneData(out);
    return true;
}

}

bool loadResourceTexture(
    const std::string& assetRoot,
    const std::string& relativePath,
    Texture2D& outTexture)
{
    const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, relativePath);

    for (const std::string& path : paths)
    {
        const std::string compressedPath = compressedAssetPath(path);
        if (FileExists(compressedPath.c_str()) &&
            loadTextureFromAssetFile(compressedPath, outTexture))
        {
            TraceLog(LOG_INFO, "Loaded compressed resource texture: %s", compressedPath.c_str());
            return true;
        }

        if (FileExists(path.c_str()))
        {
            Texture2D texture = LoadTexture(path.c_str());
            if (texture.id != 0)
            {
                outTexture = texture;
                TraceLog(LOG_INFO, "Loaded resource texture: %s", path.c_str());
                return true;
            }

            if (loadTextureFromAssetFile(path, outTexture))
            {
                TraceLog(LOG_INFO, "Loaded resource texture: %s", path.c_str());
                return true;
            }
        }
    }

    TraceLog(LOG_ERROR, "Failed to load resource texture: %s", relativePath.c_str());
    return false;
}

SceneDatabase::SceneDatabase()
    : descriptionFont{},
      boldFont{},
      uiFont{},
      fontsLoaded(false)
{
}

SceneDatabase::~SceneDatabase()
{
    if (underConstructionImageReady && underConstructionImage.data != nullptr)
        UnloadImage(underConstructionImage);

    if (fontsLoaded)
    {
        UnloadFont(descriptionFont);
        if (boldFont.texture.id != descriptionFont.texture.id)
            UnloadFont(boldFont);
        if (uiFont.texture.id != descriptionFont.texture.id &&
            uiFont.texture.id != boldFont.texture.id)
        {
            UnloadFont(uiFont);
        }
    }
}

bool SceneDatabase::load(const std::string& configPath, const std::string& assetRoot)
{
    this->assetRoot = assetRoot;
    scenes.clear();

    std::ifstream file(configPath.c_str());
    if (!file.is_open())
    {
        TraceLog(LOG_ERROR, "Could not open scene config: %s", configPath.c_str());
        return false;
    }

    nlohmann::json config;
    try
    {
        file >> config;
    }
    catch (const nlohmann::json::exception&)
    {
        TraceLog(LOG_ERROR, "Invalid JSON in scene config: %s", configPath.c_str());
        return false;
    }

    if (!config.is_object() || !config.contains("scenes") || !config["scenes"].is_object())
    {
        TraceLog(LOG_ERROR, "Scene config is missing a top-level 'scenes' object: %s", configPath.c_str());
        return false;
    }

    const std::string fontPath = config.value("font", "");
    const std::string boldFontPath = config.value("boldFont", fontPath);
    const std::string uiFontPath = config.value("uiFont", "");
    if (fontPath.empty())
    {
        TraceLog(LOG_ERROR, "Scene config is missing required 'font' entry: %s", configPath.c_str());
        return false;
    }

    if (!fontsLoaded)
    {
        descriptionFont = loadGameFont(assetRoot, fontPath);
        if (descriptionFont.texture.id == 0)
        {
            TraceLog(LOG_ERROR, "Failed to load game font: %s", fontPath.c_str());
            return false;
        }

        boldFont = loadGameFont(assetRoot, boldFontPath);
        if (boldFont.texture.id == 0)
            boldFont = descriptionFont;

        if (!uiFontPath.empty())
            uiFont = loadGameFont(assetRoot, uiFontPath);

        if (uiFont.texture.id == 0)
        {
            uiFont = loadGameFont(assetRoot, "/System/Library/Fonts/Supplemental/Courier New.ttf");
            if (uiFont.texture.id == 0)
                uiFont = descriptionFont;
        }

        fontsLoaded = true;
    }

    const nlohmann::json& scenesJson = config["scenes"];
    for (auto it = scenesJson.begin(); it != scenesJson.end(); ++it)
    {
        SceneData scene;
        if (!parseScene(it.key(), it.value(), scene))
        {
            TraceLog(LOG_ERROR, "Failed to parse scene '%s' in %s", it.key().c_str(), configPath.c_str());
            return false;
        }

        scenes[scene.id] = scene;
    }

    for (std::map<std::string, SceneData>::iterator it = scenes.begin(); it != scenes.end(); ++it)
        compileSceneData(it->second);

    if (config.contains("conversations"))
    {
        if (!applyConversationOverlays(config["conversations"], scenes))
        {
            TraceLog(LOG_ERROR, "Failed to apply inline conversation overlays in %s", configPath.c_str());
            return false;
        }
    }
    else
    {
        const std::string conversationsPath = resolveConversationsPath(configPath, assetRoot);
        std::ifstream conversationsFile(conversationsPath.c_str());
        if (!conversationsPath.empty() && conversationsFile.is_open())
        {
            nlohmann::json conversationsConfig;
            try
            {
                conversationsFile >> conversationsConfig;
            }
            catch (const nlohmann::json::exception&)
            {
                TraceLog(LOG_ERROR, "Failed to parse conversations file: %s", conversationsPath.c_str());
                return false;
            }

            if (!applyConversationOverlays(conversationsConfig, scenes))
            {
                TraceLog(LOG_ERROR, "Failed to apply conversation overlays from %s", conversationsPath.c_str());
                return false;
            }
        }
    }

    return !scenes.empty();
}

bool SceneDatabase::tryLoadSceneImage(const std::string& imagePath, Texture2D& outTexture) const
{
    if (imagePath.empty())
        return false;

    Texture2D texture{};
    if (loadResourceTexture(assetRoot, imagePath, texture))
    {
        outTexture = texture;
        return true;
    }

    return false;
}

void SceneDatabase::ensureUnderConstructionImage() const
{
    if (underConstructionImageReady && underConstructionImage.data != nullptr)
        return;

    Texture2D assetTexture{};
    if (loadResourceTexture(assetRoot, "resources/images/scene_under_construction.png", assetTexture))
    {
        underConstructionImage = LoadImageFromTexture(assetTexture);
        UnloadTexture(assetTexture);
        if (underConstructionImage.data != nullptr)
        {
            underConstructionImageReady = true;
            TraceLog(LOG_INFO, "Loaded under-construction image from resources/images/scene_under_construction.png");
            return;
        }
    }

    const int width = 1024;
    const int height = 768;
    RenderTexture2D renderTarget = LoadRenderTexture(width, height);

    BeginTextureMode(renderTarget);
    ClearBackground(Color{34, 32, 40, 255});

    const Color accent = {186, 150, 72, 255};
    const Color stripe = {168, 132, 48, 255};
    DrawRectangle(0, 0, width, 10, accent);
    DrawRectangle(0, height - 10, width, 10, accent);

    for (int x = -height; x < width; x += 48)
        DrawRectangle(x, height / 2 - 36, 24, 72, stripe);

    const char* title = "UNDER CONSTRUCTION";
    const char* subtitle = "This scene is not yet complete.";
    const float titleSize = 52.0f;
    const float subtitleSize = 24.0f;
    const Vector2 titleMeasure = MeasureTextEx(descriptionFont, title, titleSize, 2.0f);
    const Vector2 subtitleMeasure = MeasureTextEx(descriptionFont, subtitle, subtitleSize, 1.0f);

    DrawTextEx(
        descriptionFont,
        title,
        { (width - titleMeasure.x) * 0.5f, height * 0.36f },
        titleSize,
        2.0f,
        accent);

    DrawTextEx(
        descriptionFont,
        subtitle,
        { (width - subtitleMeasure.x) * 0.5f, height * 0.36f + titleMeasure.y + 18.0f },
        subtitleSize,
        1.0f,
        Color{176, 168, 152, 255});

    EndTextureMode();

    Image generated = LoadImageFromTexture(renderTarget.texture);
    ImageFlipVertical(&generated);
    UnloadRenderTexture(renderTarget);

    underConstructionImage = generated;
    underConstructionImageReady = true;
    TraceLog(LOG_INFO, "Generated under-construction placeholder image");
}

Texture2D SceneDatabase::createOwnedPlaceholderTexture() const
{
    ensureUnderConstructionImage();

    Texture2D texture = LoadTextureFromImage(underConstructionImage);
    if (texture.id != 0)
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);

    return texture;
}

bool SceneDatabase::buildLocationStruct(
    const SceneData& scene,
    const std::string& subSceneId,
    LocationStruct& outLocation) const
{
    const SubSceneDef* subScene = getSubScene(scene.id, subSceneId);
    if (subScene == nullptr)
        return false;

    outLocation.locationDescription = !subScene->description.empty()
        ? subScene->description
        : scene.description;
    outLocation.descriptionTts = pickSceneTts(scene.descriptionTts, subScene->descriptionTts);
    outLocation.examineDetails = !subScene->examineDetails.empty()
        ? subScene->examineDetails
        : scene.examineDetails;
    outLocation.examineTts = pickSceneTts(scene.examineTts, subScene->examineTts);
    outLocation.examineFlag = !subScene->examineFlag.empty()
        ? subScene->examineFlag
        : scene.examineFlag;
    outLocation.examineLucidityDelta = subScene->examineLucidityDelta;
    outLocation.examineLucidityOncePerDay = subScene->examineLucidityOncePerDay;
    outLocation.speakDetails = !subScene->speakDetails.empty()
        ? subScene->speakDetails
        : scene.speakDetails;
    outLocation.useDetails = !subScene->useDetails.empty()
        ? subScene->useDetails
        : scene.useDetails;
    outLocation.useHealthDelta = subScene->useHealthDelta;
    outLocation.useEnergyDelta = subScene->useEnergyDelta;
    outLocation.useRepeatStatus = subScene->useRepeatStatus;
    outLocation.useRequiresExamine = subScene->useRequiresExamine;
    outLocation.useAdvancesDay = subScene->useAdvancesDay;
    outLocation.useExit = !subScene->useExit.empty() ? subScene->useExit : scene.useExit;
    outLocation.descriptionFont = descriptionFont;
    outLocation.boldFont = boldFont;
    outLocation.uiFont = uiFont;
    outLocation.movementFilter = movementStructIsEmpty(subScene->movement)
        ? scene.movement
        : subScene->movement;
    outLocation.actionFilter = actionStructIsEmpty(subScene->actions)
        ? scene.actions
        : subScene->actions;
    outLocation.ownsLocationImage = true;
    outLocation.isUnderConstruction = false;

    const std::string imagePath = !subScene->imagePath.empty()
        ? subScene->imagePath
        : scene.imagePath;

    Texture2D sceneTexture{};
    if (tryLoadSceneImage(imagePath, sceneTexture))
    {
        outLocation.locationImage = sceneTexture;
        return true;
    }

    if (!imagePath.empty())
        TraceLog(LOG_WARNING, "Scene '%s' image unavailable (%s); using under-construction placeholder", scene.id.c_str(), imagePath.c_str());
    else
        TraceLog(LOG_INFO, "Scene '%s' has no image; using under-construction placeholder", scene.id.c_str());

    outLocation.isUnderConstruction = true;
    outLocation.locationImage = createOwnedPlaceholderTexture();
    outLocation.ownsLocationImage = (outLocation.locationImage.id != 0);
    return true;
}

bool SceneDatabase::loadStartScene(LocationStruct& outLocation, std::string& outSceneId) const
{
    for (std::map<std::string, SceneData>::const_iterator it = scenes.begin(); it != scenes.end(); ++it)
    {
        if (it->second.isStart)
        {
            outSceneId = it->first;
            return loadScene(outSceneId, outLocation);
        }
    }

    return false;
}

bool SceneDatabase::loadScene(const std::string& sceneId, LocationStruct& outLocation) const
{
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return false;

    return buildLocationStruct(it->second, it->second.defaultSubSceneId, outLocation);
}

bool SceneDatabase::loadScene(
    const std::string& sceneId,
    const std::string& subSceneId,
    LocationStruct& outLocation) const
{
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return false;

    return buildLocationStruct(it->second, subSceneId, outLocation);
}

const SceneSpeakConfig& SceneDatabase::getSpeakConfig(const std::string& sceneId) const
{
    static const SceneSpeakConfig kEmptyConfig;
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return kEmptyConfig;

    return it->second.speakConfig;
}

std::vector<SceneActorDef> SceneDatabase::getSceneActors(const std::string& sceneId) const
{
    return getSceneActors(sceneId, "");
}

std::vector<SceneActorDef> SceneDatabase::getSceneActors(
    const std::string& sceneId,
    const std::string& subSceneId) const
{
    std::vector<SceneActorDef> actors;
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return actors;

    collectSceneActorsFromConfig(it->second.speakConfig, actors);

    const SubSceneDef* subScene = getSubScene(sceneId, subSceneId);
    if (subScene == nullptr || subScene->actorsPresent.empty())
        return actors;

    std::vector<SceneActorDef> filtered;
    for (const SceneActorDef& actor : actors)
    {
        if (std::find(
                subScene->actorsPresent.begin(),
                subScene->actorsPresent.end(),
                actor.id) != subScene->actorsPresent.end())
        {
            filtered.push_back(actor);
        }
    }

    return filtered;
}

const RoomAudioConfig& SceneDatabase::getSceneAudio(const std::string& sceneId) const
{
    static const RoomAudioConfig kEmptyConfig;
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return kEmptyConfig;

    return it->second.audio;
}

const RoomAudioConfig& SceneDatabase::getSceneAudio(
    const std::string& sceneId,
    const std::string& subSceneId) const
{
    const SubSceneDef* subScene = getSubScene(sceneId, subSceneId);
    if (subScene != nullptr && subScene->audio.hasMusic)
        return subScene->audio;

    return getSceneAudio(sceneId);
}

const std::vector<TakeableItemDef>& SceneDatabase::getTakeables(const std::string& sceneId) const
{
    static const std::vector<TakeableItemDef> kEmptyTakeables;
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return kEmptyTakeables;

    return it->second.takeables;
}

const std::vector<SceneInteractionDef>& SceneDatabase::getInteractions(const std::string& sceneId) const
{
    static const std::vector<SceneInteractionDef> kEmptyInteractions;
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return kEmptyInteractions;

    return it->second.interactions;
}

const std::vector<SceneInteractionDef>& SceneDatabase::getInteractions(
    const std::string& sceneId,
    const std::string& subSceneId) const
{
    const SubSceneDef* subScene = getSubScene(sceneId, subSceneId);
    if (subScene != nullptr && !subScene->interactions.empty())
        return subScene->interactions;

    return getInteractions(sceneId);
}

const SubSceneDef* SceneDatabase::getSubScene(
    const std::string& sceneId,
    const std::string& subSceneId) const
{
    std::map<std::string, SceneData>::const_iterator sceneIt = scenes.find(sceneId);
    if (sceneIt == scenes.end())
        return nullptr;

    std::map<std::string, SubSceneDef>::const_iterator subSceneIt =
        sceneIt->second.subScenes.find(subSceneId);
    if (subSceneIt == sceneIt->second.subScenes.end())
        return nullptr;

    return &subSceneIt->second;
}

std::string SceneDatabase::resolveActiveSubSceneId(
    const SceneData& scene,
    const std::set<std::string>& storyFlags,
    const std::string& requestedSubSceneId,
    SubSceneResolveMode mode,
    const std::function<bool(const std::string& phaseId)>& isPhaseComplete) const
{
    if (!requestedSubSceneId.empty()
        && scene.subScenes.count(requestedSubSceneId) > 0)
    {
        return requestedSubSceneId;
    }

    MaskEvalContext context;
    context.storyFlags = &storyFlags;
    context.activeSubSceneId = requestedSubSceneId;

    for (const SubSceneRuleDef& rule : scene.subSceneRules)
    {
        if (rule.isDefault)
            continue;

        if (mode == SubSceneResolveMode::OnEnter && !rule.applyOnEnter)
            continue;

        if (!isMaskConditionMet(rule.when, context))
            continue;

        if (!rule.untilPhase.empty()
            && isPhaseComplete
            && isPhaseComplete(rule.untilPhase))
        {
            continue;
        }

        return rule.subSceneId;
    }

    for (const SubSceneRuleDef& rule : scene.subSceneRules)
    {
        if (!rule.isDefault)
            continue;

        return rule.subSceneId;
    }

    if (!scene.defaultSubSceneId.empty()
        && scene.subScenes.count(scene.defaultSubSceneId) > 0)
        return scene.defaultSubSceneId;

    if (!scene.subScenes.empty())
        return scene.subScenes.begin()->first;

    return "default";
}

const std::vector<SceneOverlayDef>& SceneDatabase::getOverlays(const std::string& sceneId) const
{
    static const std::vector<SceneOverlayDef> kEmptyOverlays;
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return kEmptyOverlays;

    return it->second.overlays;
}

std::string SceneDatabase::getExitSceneId(const std::string& sceneId, const std::string& direction) const
{
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return "";

    std::map<std::string, std::string>::const_iterator exitIt = it->second.exits.find(direction);
    if (exitIt == it->second.exits.end())
        return "";

    return exitIt->second;
}

bool SceneDatabase::getExitRequirement(
    const std::string& sceneId,
    const std::string& direction,
    ExitRequirementDef& outRequirement) const
{
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return false;

    std::map<std::string, ExitRequirementDef>::const_iterator requirementIt =
        it->second.exitRequirements.find(direction);
    if (requirementIt == it->second.exitRequirements.end())
        return false;

    outRequirement = requirementIt->second;
    return true;
}

const SceneData* SceneDatabase::getScene(const std::string& sceneId) const
{
    std::map<std::string, SceneData>::const_iterator it = scenes.find(sceneId);
    if (it == scenes.end())
        return nullptr;

    return &it->second;
}

bool SceneDatabase::isHighAltitudeScene(const std::string& sceneId) const
{
    const SceneData* scene = getScene(sceneId);
    return scene != nullptr && scene->highAltitude;
}

std::string SceneDatabase::resolveSceneImagePath(
    const SceneData& scene,
    const std::string& subSceneId,
    const std::set<std::string>& storyFlags,
    const std::function<bool(const std::string& phaseId)>& isPhaseComplete) const
{
    const std::string activeSubSceneId = resolveActiveSubSceneId(
        scene,
        storyFlags,
        subSceneId,
        SubSceneResolveMode::InScene,
        isPhaseComplete);
    const SubSceneDef* subScene = getSubScene(scene.id, activeSubSceneId);
    if (subScene != nullptr && !subScene->imagePath.empty())
        return subScene->imagePath;

    for (const AlternateImageDef& alternate : scene.alternateImages)
    {
        if (alternate.flag.empty())
            continue;

        if (storyFlags.count(alternate.flag) == 0)
            continue;

        if (!alternate.untilPhase.empty() && isPhaseComplete(alternate.untilPhase))
            continue;

        return alternate.imagePath;
    }

    return scene.imagePath;
}

std::vector<std::string> SceneDatabase::collectSceneImagePaths(const SceneData& scene) const
{
    std::vector<std::string> paths;

    for (std::map<std::string, SubSceneDef>::const_iterator it = scene.subScenes.begin();
         it != scene.subScenes.end();
         ++it)
    {
        if (it->second.imagePath.empty())
            continue;

        if (std::find(paths.begin(), paths.end(), it->second.imagePath) == paths.end())
            paths.push_back(it->second.imagePath);
    }

    if (!scene.imagePath.empty()
        && std::find(paths.begin(), paths.end(), scene.imagePath) == paths.end())
    {
        paths.push_back(scene.imagePath);
    }

    for (const AlternateImageDef& alternate : scene.alternateImages)
    {
        if (alternate.imagePath.empty())
            continue;

        if (std::find(paths.begin(), paths.end(), alternate.imagePath) == paths.end())
            paths.push_back(alternate.imagePath);
    }

    return paths;
}

bool SceneDatabase::loadSceneTexture(const std::string& imagePath, Texture2D& outTexture) const
{
    return tryLoadSceneImage(imagePath, outTexture);
}

bool loadStartLocation(
    const std::string& configPath,
    const std::string& assetRoot,
    LocationStruct& outLocation)
{
    SceneDatabase database;
    std::string sceneId;
    if (!database.load(configPath, assetRoot))
        return false;

    return database.loadStartScene(outLocation, sceneId);
}

}
