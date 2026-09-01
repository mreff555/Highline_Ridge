/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneAuthoring.h"
#include "EditorPaths.h"
#include "EditorPrefs.h"
#include "ImageCompression.h"
#include "PlatformPath.h"

#include <raylib.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <vector>

#include <atomic>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

using timberline_engine::compressedAssetPath;
using timberline_engine::ensureParentDirectoryExists;
using timberline_engine::pathJoin;

namespace timberline_editor
{

namespace
{

std::string trimCopy(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

bool isPlausibleResourcePath(const std::string& path)
{
    if (path.empty())
        return false;
    if (path.rfind("resources/", 0) != 0)
        return false;
    if (path.find("..") != std::string::npos)
        return false;
    return true;
}

std::string shellQuote(const std::string& value)
{
    std::string out = "'";
    for (char ch : value)
    {
        if (ch == '\'')
            out += "'\\''";
        else
            out += ch;
    }
    out += "'";
    return out;
}

std::string findGameRoot(const std::string& assetRootHint, const std::string& resourceDirHint)
{
    std::vector<std::string> seeds = {assetRootHint, resourceDirHint, "."};
    for (const std::string& seed : seeds)
    {
        std::string cursor = seed.empty() ? "." : seed;
        for (int i = 0; i < 8; ++i)
        {
            const std::string runner =
                pathJoin(cursor, "tools/run_item_authoring_ai.py");
            if (FileExists(runner.c_str()))
                return cursor;
            cursor = pathJoin(cursor, "..");
        }
    }
    return ".";
}

std::string sceneAiJobTypeString(SceneAiJobType type)
{
    switch (type)
    {
    case SceneAiJobType::GenerateAmbient:
        return "generate_ambient_sound";
    case SceneAiJobType::GenerateMusic:
        return "generate_music";
    case SceneAiJobType::GenerateEnterSfx:
        return "generate_use_sound";
    case SceneAiJobType::GenerateExitSfx:
        return "generate_examine_sound";
    case SceneAiJobType::GenerateDescriptionTtsText:
        return "generate_scene_description_tts_text";
    case SceneAiJobType::GenerateExamineTtsText:
        return "generate_scene_examine_tts_text";
    case SceneAiJobType::GenerateImage:
    default:
        return "generate_image";
    }
}

nlohmann::json sceneAiJobToJson(const SceneAiJob& job)
{
    nlohmann::json j = {
        {"type", sceneAiJobTypeString(job.type)},
        {"prompt", job.prompt},
        {"outPath", job.outPath},
        {"action", job.action.empty() ? "examine" : job.action}};
    if (!job.sourceText.empty())
        j["sourceText"] = job.sourceText;
    if (!job.defaultVoice.empty())
        j["defaultVoice"] = job.defaultVoice;
    return j;
}

std::string ttsBagText(const nlohmann::json& scene, const char* key)
{
    if (!scene.contains(key) || !scene[key].is_object())
        return "";
    const auto& bag = scene[key];
    if (bag.contains("ttsText") && bag["ttsText"].is_string())
        return bag["ttsText"].get<std::string>();
    if (bag.contains("text") && bag["text"].is_string())
        return bag["text"].get<std::string>();
    return "";
}

} // namespace

std::string sanitizeSceneId(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (char ch : raw)
    {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isalnum(u))
            out.push_back(static_cast<char>(std::tolower(u)));
        else if (ch == '_' || ch == '-' || ch == ' ')
            out.push_back('_');
    }
    // Collapse repeats.
    std::string compact;
    for (char ch : out)
    {
        if (ch == '_' && !compact.empty() && compact.back() == '_')
            continue;
        compact.push_back(ch);
    }
    while (!compact.empty() && compact.front() == '_')
        compact.erase(compact.begin());
    while (!compact.empty() && compact.back() == '_')
        compact.pop_back();
    return compact;
}

bool isValidSceneId(const std::string& id)
{
    if (id.empty() || id.size() > 64)
        return false;
    if (!std::isalpha(static_cast<unsigned char>(id[0])))
        return false;
    for (char ch : id)
    {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (!(std::isalnum(u) || ch == '_'))
            return false;
    }
    return true;
}

nlohmann::json buildSceneJson(const SceneAuthoringPayload& payload)
{
    nlohmann::json scene = nlohmann::json::object();
    scene["start"] = false;
    scene["image"] = payload.imagePath.empty()
        ? ("resources/images/" + payload.id + ".png")
        : payload.imagePath;
    scene["description"] = payload.description;
    scene["examineDetails"] = payload.examineDetails;
    scene["speakDetails"] = "";
    scene["useDetails"] = "";
    scene["movement"] = {
        {"forward", false},
        {"backward", true},
        {"left", false},
        {"right", false}};
    // New Scene couples Speak action with TTS enable.
    const bool speakOn = payload.ttsEnabled || payload.speakEnabled;
    scene["actions"] = {
        {"examine", true},
        {"speak", speakOn},
        {"hit", false},
        {"use", false}};
    scene["exits"] = nlohmann::json::object();
    // Intentionally omit layout — new scenes stay list-only until dragged onto the map.

    nlohmann::json audio = nlohmann::json::object();
    if (!payload.musicPath.empty() && isPlausibleResourcePath(payload.musicPath))
    {
        audio["music"] = {
            {"path", payload.musicPath},
            {"volume", 0.2},
            {"fade_in", 3.0},
            {"fade_out", 2.5},
            {"loop", true}};
    }
    nlohmann::json ambient = nlohmann::json::array();
    if (!payload.ambientPath.empty() && isPlausibleResourcePath(payload.ambientPath))
    {
        ambient.push_back({
            {"path", payload.ambientPath},
            {"volume", 0.45},
            {"fade_in", 1.5},
            {"fade_out", 1.5},
            {"loop", true}});
    }
    if (!ambient.empty())
        audio["ambient"] = ambient;

    nlohmann::json sfx = nlohmann::json::array();
    if (!payload.enterSfxPath.empty() && isPlausibleResourcePath(payload.enterSfxPath))
    {
        sfx.push_back({
            {"path", payload.enterSfxPath},
            {"trigger", "on_enter"},
            {"volume", 0.85}});
    }
    if (!payload.exitSfxPath.empty() && isPlausibleResourcePath(payload.exitSfxPath))
    {
        sfx.push_back({
            {"path", payload.exitSfxPath},
            {"trigger", "on_exit"},
            {"volume", 0.85}});
    }
    if (!sfx.empty())
        audio["sfx"] = sfx;
    if (!audio.empty())
        scene["audio"] = audio;

    if (payload.ttsEnabled)
    {
        scene["ttsEnabled"] = true;
        scene["ttsDefaultVoice"] = payload.ttsDefaultVoice.empty()
            ? "leo"
            : payload.ttsDefaultVoice;

        auto writeTtsBag = [&](const char* key,
                                const std::string& text,
                                const std::string& audioLeaf) {
            if (text.empty())
                return;
            scene[key] = {
                {"tts", true},
                {"ttsText", text},
                {"ttsVoice", ""},
                {"ttsAudio",
                 "resources/audio/tts/" + payload.id + "/" + audioLeaf + ".mp3"}};
        };
        writeTtsBag("descriptionTts", payload.ttsDescription, "descriptionTts");
        writeTtsBag("examineTts", payload.ttsExamineDetails, "examineTts");
    }

    return scene;
}

std::vector<SceneAiJob> buildSceneAiJobs(
    const SceneAuthoringPayload& payload,
    int aiTargetFilter,
    const std::string& styleBlock)
{
    std::vector<SceneAiJob> jobs;
    std::string overview = payload.description.empty()
        ? payload.id
        : payload.description;
    std::string ctx = "Scene overview: " + overview;
    if (!payload.examineDetails.empty())
        ctx += " Examine / detail notes: " + payload.examineDetails;

    const std::string periodRules =
        "Timberline 1890s high-altitude Colorado frontier detective game. "
        "Period-accurate, painterly realistic, no modern objects, no text, no UI. ";
    const std::string voice = payload.ttsDefaultVoice.empty()
        ? "leo"
        : payload.ttsDefaultVoice;

    auto want = [aiTargetFilter](int target) {
        return aiTargetFilter == 0 || aiTargetFilter == target;
    };

    const std::string basePrompt = periodRules + styleBlock;

    if (want(1))
    {
        SceneAiJob job;
        job.type = SceneAiJobType::GenerateImage;
        job.outPath = payload.imagePath.empty()
            ? ("resources/images/" + payload.id + ".png")
            : payload.imagePath;
        job.prompt = basePrompt
            + "Wide establishing scene image of: " + ctx
            + " Full-screen adventure game background, dimmable for UI.";
        jobs.push_back(job);
    }
    if (want(2))
    {
        SceneAiJob job;
        job.type = SceneAiJobType::GenerateAmbient;
        job.action = "ambient";
        job.outPath = payload.ambientPath.empty()
            ? ("resources/audio/ambient/" + payload.id + ".mp3")
            : payload.ambientPath;
        // Prompt carries scene + style for chat layer-planning in the runner.
        job.prompt =
            std::string("Plan a seamless loopable ambient SOUND BED (not music, not speech).\n")
            + styleBlock + "Scene context:\n" + ctx
            + "\nPrefer layers matching the setting (e.g. distant birds, faint streams, "
              "soft wind for mountain trails).";
        jobs.push_back(job);
    }
    if (want(3))
    {
        SceneAiJob job;
        job.type = SceneAiJobType::GenerateMusic;
        job.action = "music";
        job.outPath = payload.musicPath.empty()
            ? ("resources/audio/music/" + payload.id + "_theme.mp3")
            : payload.musicPath;
        job.prompt = std::string("Loopable period instrumental underscore.\n")
            + styleBlock + "Scene context:\n" + ctx;
        jobs.push_back(job);
    }
    if (want(4))
    {
        SceneAiJob job;
        job.type = SceneAiJobType::GenerateEnterSfx;
        job.action = "enter";
        job.outPath = payload.enterSfxPath.empty()
            ? ("resources/audio/sfx/" + payload.id + "_enter.mp3")
            : payload.enterSfxPath;
        job.prompt = "Door enter SFX for: " + ctx;
        jobs.push_back(job);
    }
    if (want(5))
    {
        SceneAiJob job;
        job.type = SceneAiJobType::GenerateExitSfx;
        job.action = "exit";
        job.outPath = payload.exitSfxPath.empty()
            ? ("resources/audio/sfx/" + payload.id + "_exit.mp3")
            : payload.exitSfxPath;
        job.prompt = "Door exit SFX for: " + ctx;
        jobs.push_back(job);
    }

    // TTS markup text (chat). Generate-all (0) only when TTS enabled.
    const bool wantTtsJobs = payload.ttsEnabled
        && (aiTargetFilter == 0 || aiTargetFilter == 6 || aiTargetFilter == 7);
    if (wantTtsJobs && (want(6) || aiTargetFilter == 0) && !payload.description.empty())
    {
        // Fill-if-empty on Generate all; dedicated target 6 always regenerates.
        if (aiTargetFilter == 6 || payload.ttsDescription.empty())
        {
            SceneAiJob job;
            job.type = SceneAiJobType::GenerateDescriptionTtsText;
            job.action = "description_tts";
            job.outPath = "resources/.authoring/" + payload.id + "_description_tts.txt";
            job.sourceText = payload.description;
            job.defaultVoice = voice;
            job.prompt =
                "Rewrite the following scene description as spoken Timberline TTS "
                "narration markup. Return ONLY the speakable text (no markdown fences, "
                "no commentary). Insert natural [pause] / [long-pause] and allowlisted "
                "command tags where appropriate. Add style/tone wrappings such as "
                "<whisper>, <soft>, <emphasis>, <slow>, <fast> when speech context "
                "warrants. Quoted spoken dialog must be wrapped with "
                "{{voice:"
                + voice
                + "}}...{{/voice}} using that default voice id (author may change later). "
                  "Narrator prose stays unwrapped (owner default voice). Preserve meaning; "
                  "1890s Highline Ridge tone.\n"
                + styleBlock + "\nSOURCE:\n"
                + payload.description;
            jobs.push_back(job);
        }
    }
    if (wantTtsJobs && (want(7) || aiTargetFilter == 0)
        && !payload.examineDetails.empty())
    {
        if (aiTargetFilter == 7 || payload.ttsExamineDetails.empty())
        {
            SceneAiJob job;
            job.type = SceneAiJobType::GenerateExamineTtsText;
            job.action = "examine_tts";
            job.outPath = "resources/.authoring/" + payload.id + "_examine_tts.txt";
            job.sourceText = payload.examineDetails;
            job.defaultVoice = voice;
            job.prompt =
                "Rewrite the following scene examine details as spoken Timberline TTS "
                "narration markup. Return ONLY the speakable text (no markdown fences, "
                "no commentary). Insert natural [pause] / [long-pause] and allowlisted "
                "command tags where appropriate. Add style/tone wrappings such as "
                "<whisper>, <soft>, <emphasis>, <slow>, <fast> when speech context "
                "warrants. Quoted spoken dialog must be wrapped with "
                "{{voice:"
                + voice
                + "}}...{{/voice}} using that default voice id (author may change later). "
                  "Narrator prose stays unwrapped (owner default voice). Preserve meaning; "
                  "1890s Highline Ridge tone.\n"
                + styleBlock + "\nSOURCE:\n"
                + payload.examineDetails;
            jobs.push_back(job);
        }
    }
    return jobs;
}

SceneUpsertResult upsertScene(
    DocumentWorkspace& docs,
    SceneAuthoringPayload& payload,
    int aiTargetFilter,
    bool writeAiJobs,
    bool backupRotate)
{
    SceneUpsertResult result;
    payload.id = sanitizeSceneId(payload.id);
    payload.description = trimCopy(payload.description);
    payload.examineDetails = trimCopy(payload.examineDetails);

    if (!isValidSceneId(payload.id))
    {
        result.message =
            "Invalid scene id (start with a letter; use a-z, 0-9, underscore).";
        return result;
    }
    if (payload.description.empty())
    {
        result.message = "Description is required (AI uses it as context).";
        return result;
    }

    // Default paths if empty.
    if (payload.imagePath.empty())
        payload.imagePath = "resources/images/" + payload.id + ".png";
    if (payload.ambientPath.empty())
        payload.ambientPath = "resources/audio/ambient/" + payload.id + ".mp3";
    if (payload.musicPath.empty())
        payload.musicPath = "resources/audio/music/" + payload.id + "_theme.mp3";

    const bool alreadyExists = docs.scenes.hasScene(payload.id);
    if (alreadyExists)
    {
        // Merge authoring fields; preserve layout / exits / movement / inventory.
        nlohmann::json* existing = docs.scenes.sceneJson(payload.id);
        if (existing == nullptr || !existing->is_object())
        {
            result.message = "Failed to open existing scene \"" + payload.id + "\"";
            return result;
        }
        // Keep authored transition SFX (incl. from_room / to_room constraints).
        // The New Scene UI does not edit enter/exit one-shots; rewriting them from
        // payload defaults was stamping door_open/close onto outdoor scenes and
        // stripping constrained interior door cues.
        nlohmann::json preservedSfx = nlohmann::json{};
        bool hadPreservedSfx = false;
        if (existing->contains("audio") && (*existing)["audio"].is_object()
            && (*existing)["audio"].contains("sfx"))
        {
            preservedSfx = (*existing)["audio"]["sfx"];
            hadPreservedSfx = true;
        }

        nlohmann::json fresh = buildSceneJson(payload);
        for (auto it = fresh.begin(); it != fresh.end(); ++it)
        {
            const std::string& key = it.key();
            if (key == "layout" || key == "exits" || key == "movement"
                || key == "inventory" || key == "start")
                continue;
            (*existing)[key] = it.value();
        }
        if (hadPreservedSfx)
        {
            if (!existing->contains("audio") || !(*existing)["audio"].is_object())
                (*existing)["audio"] = nlohmann::json::object();
            (*existing)["audio"]["sfx"] = preservedSfx;
        }
        else if (existing->contains("audio") && (*existing)["audio"].is_object())
        {
            (*existing)["audio"].erase("sfx");
        }
        // Drop TTS bags when disabled so refresh does not synthesize stale text.
        if (!payload.ttsEnabled)
        {
            existing->erase("ttsEnabled");
            existing->erase("ttsDefaultVoice");
            existing->erase("descriptionTts");
            existing->erase("examineTts");
        }
    }
    else
    {
        nlohmann::json scene = buildSceneJson(payload);
        if (!docs.scenes.createScene(payload.id, scene))
        {
            result.message = "Failed to create scene object in document.";
            return result;
        }
    }

    if (writeAiJobs)
    {
        const std::string styleBlock =
            formatGenerationStyleBlock(loadGenerationStyleFilter(docs.resourceDir));
        result.jobs = buildSceneAiJobs(payload, aiTargetFilter, styleBlock);
        if (!result.jobs.empty())
        {
            nlohmann::json jobsRoot;
            jobsRoot["sceneId"] = payload.id;
            jobsRoot["itemId"] = payload.id; // runner also accepts itemId key
            jobsRoot["kind"] = "scene";
            if (backupRotate)
                jobsRoot["backupRotate"] = true;
            nlohmann::json arr = nlohmann::json::array();
            for (const SceneAiJob& job : result.jobs)
                arr.push_back(sceneAiJobToJson(job));
            jobsRoot["jobs"] = arr;

            const std::string gameRoot =
                findGameRoot(docs.assetRoot, docs.resourceDir);
            const std::string authoringDir =
                pathJoin(pathJoin(gameRoot, "resources"), ".authoring");
#if !defined(_WIN32)
            // best-effort mkdir
            std::string mkdirCmd = "mkdir -p " + shellQuote(authoringDir);
            std::system(mkdirCmd.c_str());
#endif
            const std::string jobsPath =
                pathJoin(authoringDir, payload.id + "_ai_jobs.json");
            std::ofstream out(jobsPath.c_str());
            if (out)
            {
                out << jobsRoot.dump(2);
                result.jobsFilePath = jobsPath;
            }
        }
    }

    docs.markDirty();
    if (!docs.scenes.save())
    {
        result.message = alreadyExists
            ? "Updated in memory but failed to write scenes.json"
            : "Created in memory but failed to write scenes.json";
        return result;
    }
    docs.dirty = false;
    result.ok = true;
    result.message = alreadyExists
        ? ("Updated scene \"" + payload.id + "\"")
        : ("Created scene \"" + payload.id + "\"");
    if (!result.jobsFilePath.empty())
        result.message += " | AI jobs: " + result.jobsFilePath;
    return result;
}

std::string runSceneAuthoringAiJobsFile(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& jobsFilePath,
    const std::string& sessionApiKey,
    std::atomic<bool>* cancelFlag)
{
    const std::string gameRoot = findGameRoot(assetRootHint, resourceDirHint);
    const std::string runner =
        pathJoin(gameRoot, "tools/run_item_authoring_ai.py");
    if (!FileExists(runner.c_str()))
        return "AI runner missing: tools/run_item_authoring_ai.py";
    if (!FileExists(jobsFilePath.c_str()))
        return "Jobs file missing: " + jobsFilePath;

    // Derive a stable log next to the jobs file.
    std::string logFile = jobsFilePath;
    const std::string jobsSuffix = "_ai_preview_jobs.json";
    const std::string jobsSuffix2 = "_ai_jobs.json";
    if (logFile.size() >= jobsSuffix.size()
        && logFile.compare(logFile.size() - jobsSuffix.size(), jobsSuffix.size(), jobsSuffix)
            == 0)
    {
        logFile = logFile.substr(0, logFile.size() - jobsSuffix.size())
            + "_ai_preview_run.log";
    }
    else if (logFile.size() >= jobsSuffix2.size()
             && logFile.compare(
                    logFile.size() - jobsSuffix2.size(), jobsSuffix2.size(), jobsSuffix2)
                 == 0)
    {
        logFile = logFile.substr(0, logFile.size() - jobsSuffix2.size())
            + "_ai_run.log";
    }
    else
        logFile += ".run.log";

    {
        std::ofstream logPre(logFile.c_str(), std::ios::trunc);
        if (logPre)
        {
            logPre << "=== Timberline scene authoring AI runner ===\n"
                   << "gameRoot: " << gameRoot << "\n"
                   << "jobsFile: " << jobsFilePath << "\n"
                   << "hasSessionKey: " << (sessionApiKey.empty() ? "no" : "yes")
                   << "\n--- python output follows ---\n";
        }
    }

#if !defined(_WIN32)
    auto runWith = [&](const char* pythonBin) -> int {
        const pid_t pid = fork();
        if (pid < 0)
            return -1;
        if (pid == 0)
        {
            // Own process group so Cancel can signal the whole tree.
            setpgid(0, 0);
            std::ostringstream cmd;
            cmd << "exec " << pythonBin << " " << shellQuote(runner)
                << " --asset-root " << shellQuote(gameRoot)
                << " --jobs-file " << shellQuote(jobsFilePath);
            if (!sessionApiKey.empty())
                cmd << " --key " << shellQuote(sessionApiKey);
            cmd << " >> " << shellQuote(logFile) << " 2>&1";
            execl("/bin/sh", "sh", "-c", cmd.str().c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        setpgid(pid, pid);
        while (true)
        {
            int st = 0;
            const pid_t waited = waitpid(pid, &st, WNOHANG);
            if (waited == pid)
                return st;
            if (waited < 0)
                return -1;
            if (cancelFlag != nullptr && cancelFlag->load())
            {
                kill(-pid, SIGTERM);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                kill(-pid, SIGKILL);
                waitpid(pid, &st, 0);
                return -999; // cancelled sentinel
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    };
#else
    auto runWith = [&](const char* pythonBin) -> int {
        std::ostringstream cmd;
        cmd << pythonBin << " " << shellQuote(runner)
            << " --asset-root " << shellQuote(gameRoot)
            << " --jobs-file " << shellQuote(jobsFilePath);
        if (!sessionApiKey.empty())
            cmd << " --key " << shellQuote(sessionApiKey);
        cmd << " >> " << shellQuote(logFile) << " 2>&1";
        return std::system(cmd.str().c_str());
    };
#endif

    if (cancelFlag != nullptr && cancelFlag->load())
        return "Cancelled.";

    int code = runWith("python3");
#if !defined(_WIN32)
    if (code == -999)
        return "Cancelled.";
    if (WIFEXITED(code) && WEXITSTATUS(code) != 0)
        code = runWith("python");
    else if (!WIFEXITED(code) && code != -999)
        code = runWith("python");
    if (code == -999)
        return "Cancelled.";
#else
    if (code != 0)
        code = runWith("python");
#endif

    int exitStatus = code;
#if !defined(_WIN32)
    if (WIFEXITED(code))
        exitStatus = WEXITSTATUS(code);
#endif

    std::string structuredErrors;
    std::string producedSummary;
    try
    {
        std::ifstream jobsIn(jobsFilePath.c_str());
        if (jobsIn)
        {
            nlohmann::json root;
            jobsIn >> root;
            if (root.contains("lastRun") && root["lastRun"].is_object())
            {
                const auto& last = root["lastRun"];
                if (last.contains("errors") && last["errors"].is_array())
                {
                    for (const auto& err : last["errors"])
                    {
                        if (!err.is_string())
                            continue;
                        if (!structuredErrors.empty())
                            structuredErrors += " | ";
                        structuredErrors += err.get<std::string>();
                    }
                }
                if (last.contains("produced") && last["produced"].is_array())
                {
                    for (const auto& p : last["produced"])
                    {
                        if (!p.is_string())
                            continue;
                        if (!producedSummary.empty())
                            producedSummary += ", ";
                        producedSummary += p.get<std::string>();
                    }
                }
            }
        }
    }
    catch (const nlohmann::json::exception&)
    {
    }

    if (exitStatus == 0 && structuredErrors.empty())
    {
        if (!producedSummary.empty())
            return "AI generate finished OK (" + producedSummary + ")";
        return "AI generate finished OK";
    }

    std::string msg = "AI generate failed";
    if (!structuredErrors.empty())
        msg += ": " + structuredErrors;
    else
        msg += " (exit " + std::to_string(exitStatus)
            + "). Paste an xAI API key for images (Cmd/Ctrl+V).";
    return msg;
}

std::string runSceneAuthoringAiJobs(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& sceneId,
    const std::string& sessionApiKey,
    std::atomic<bool>* cancelFlag)
{
    const std::string gameRoot = findGameRoot(assetRootHint, resourceDirHint);
    const std::string jobsPath = pathJoin(
        pathJoin(pathJoin(gameRoot, "resources"), ".authoring"),
        sceneId + "_ai_jobs.json");
    const std::string msg = runSceneAuthoringAiJobsFile(
        assetRootHint, resourceDirHint, jobsPath, sessionApiKey, cancelFlag);
    if (msg == "AI generate finished OK")
        return "AI generate finished OK for " + sceneId;
    return msg;
}

std::string writeSceneAiJobsFile(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const SceneAuthoringPayload& payload,
    int aiTargetFilter,
    bool backupRotate)
{
    const std::string styleBlock =
        formatGenerationStyleBlock(loadGenerationStyleFilter(resourceDirHint));
    const std::vector<SceneAiJob> jobs =
        buildSceneAiJobs(payload, aiTargetFilter, styleBlock);
    if (jobs.empty())
        return "";

    nlohmann::json jobsRoot;
    jobsRoot["sceneId"] = payload.id;
    jobsRoot["itemId"] = payload.id;
    jobsRoot["kind"] = "scene";
    if (backupRotate)
        jobsRoot["backupRotate"] = true;
    nlohmann::json arr = nlohmann::json::array();
    for (const SceneAiJob& job : jobs)
        arr.push_back(sceneAiJobToJson(job));
    jobsRoot["jobs"] = arr;

    const std::string gameRoot = findGameRoot(assetRootHint, resourceDirHint);
    const std::string authoringDir =
        pathJoin(pathJoin(gameRoot, "resources"), ".authoring");
#if !defined(_WIN32)
    std::string mkdirCmd = "mkdir -p " + shellQuote(authoringDir);
    std::system(mkdirCmd.c_str());
#endif
    const std::string jobsPath =
        pathJoin(authoringDir, payload.id + "_ai_jobs.json");
    std::ofstream out(jobsPath.c_str());
    if (!out)
        return "";
    out << jobsRoot.dump(2);
    return jobsPath;
}

bool fillPayloadFromScene(
    const DocumentWorkspace& docs,
    const std::string& sceneId,
    SceneAuthoringPayload& out)
{
    const nlohmann::json* scene = docs.scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
        return false;

    out = SceneAuthoringPayload{};
    out.id = sceneId;
    out.description = scene->value("description", "");
    out.examineDetails = scene->value("examineDetails", "");
    out.imagePath = scene->value("image", "");
    out.ambientPath = docs.scenes.getSceneAmbientPath(sceneId);
    out.musicPath = docs.scenes.getSceneMusicPath(sceneId);
    out.enterSfxPath.clear();
    out.exitSfxPath.clear();
    {
        const nlohmann::json audio = scene->value("audio", nlohmann::json::object());
        if (audio.is_object() && audio.contains("sfx") && audio["sfx"].is_array())
        {
            for (const nlohmann::json& entry : audio["sfx"])
            {
                if (!entry.is_object())
                    continue;
                const std::string path = entry.value("path", "");
                if (path.empty())
                    continue;
                const std::string trigger = entry.value("trigger", "on_enter");
                if (trigger == "on_enter" && out.enterSfxPath.empty())
                    out.enterSfxPath = path;
                else if (trigger == "on_exit" && out.exitSfxPath.empty())
                    out.exitSfxPath = path;
            }
        }
    }
    out.speakEnabled = scene->value("actions", nlohmann::json::object())
                           .value("speak", false);
    out.ttsEnabled = scene->value("ttsEnabled", false);
    out.ttsDefaultVoice = scene->value("ttsDefaultVoice", "leo");
    out.ttsDescription = ttsBagText(*scene, "descriptionTts");
    out.ttsExamineDetails = ttsBagText(*scene, "examineTts");
    const auto layout = docs.scenes.getLayout(sceneId);
    out.layoutX = layout.x;
    out.layoutY = layout.y;
    out.layoutLevel = layout.level;

    if (out.imagePath.empty())
        out.imagePath = "resources/images/" + sceneId + ".png";
    if (out.ambientPath.empty())
        out.ambientPath = "resources/audio/ambient/" + sceneId + ".mp3";
    if (out.musicPath.empty())
        out.musicPath = "resources/audio/music/" + sceneId + "_theme.mp3";
    return true;
}

bool sceneImageReferenceExists(
    const std::string& assetRoot,
    const std::string& relPath)
{
    if (!isPlausibleResourcePath(relPath))
        return false;
    const std::string root = assetRoot.empty() ? "." : assetRoot;
    const std::string abs = pathJoin(root, relPath);
    if (FileExists(abs.c_str()))
        return true;
    const std::string xz = compressedAssetPath(abs);
    return FileExists(xz.c_str());
}

namespace
{

/** Replace oldId with newId in the filename stem only (last path segment). */
std::string rewritePathStemId(const std::string& relPath, const std::string& oldId, const std::string& newId)
{
    if (relPath.empty() || oldId.empty() || newId.empty() || oldId == newId)
        return relPath;
    const size_t slash = relPath.find_last_of("/\\");
    const std::string dir =
        (slash == std::string::npos) ? std::string{} : relPath.substr(0, slash + 1);
    std::string base =
        (slash == std::string::npos) ? relPath : relPath.substr(slash + 1);
    const size_t pos = base.find(oldId);
    if (pos == std::string::npos)
        return relPath;
    base.replace(pos, oldId.size(), newId);
    return dir + base;
}

bool renameAssetFilePair(
    const std::string& assetRoot,
    const std::string& oldRel,
    const std::string& newRel)
{
    if (oldRel.empty() || newRel.empty() || oldRel == newRel)
        return true;
    if (!isPlausibleResourcePath(oldRel) || !isPlausibleResourcePath(newRel))
        return false;
    const std::string oldAbs = pathJoin(assetRoot, oldRel);
    const std::string newAbs = pathJoin(assetRoot, newRel);
    const std::string oldXz = compressedAssetPath(oldAbs);
    const std::string newXz = compressedAssetPath(newAbs);
    ensureParentDirectoryExists(newAbs);
    bool ok = true;
    if (FileExists(oldAbs.c_str()))
    {
        if (FileExists(newAbs.c_str()))
            std::remove(newAbs.c_str());
        if (std::rename(oldAbs.c_str(), newAbs.c_str()) != 0)
            ok = false;
    }
    if (FileExists(oldXz.c_str()))
    {
        if (FileExists(newXz.c_str()))
            std::remove(newXz.c_str());
        if (std::rename(oldXz.c_str(), newXz.c_str()) != 0)
            ok = false;
    }
    return ok;
}

} // namespace

bool renameSceneAuthoring(
    DocumentWorkspace& docs,
    const std::string& oldId,
    const std::string& newIdRaw,
    SceneAuthoringPayload* payloadToSync,
    std::string& errorOut)
{
    errorOut.clear();
    const std::string newId = sanitizeSceneId(newIdRaw);
    if (!isValidSceneId(newId))
    {
        errorOut = "New scene id is invalid.";
        return false;
    }
    if (oldId.empty() || oldId == newId)
    {
        errorOut = "Enter a different valid scene id to rename.";
        return false;
    }
    if (!docs.scenes.hasScene(oldId))
    {
        errorOut = "Scene not found: " + oldId;
        return false;
    }
    if (docs.scenes.hasScene(newId))
    {
        errorOut = "Scene id already exists: " + newId;
        return false;
    }

    // Snapshot unique asset paths before the id moves.
    const std::vector<std::string> uniquePaths =
        collectUniqueSceneAssetPaths(docs, oldId);

    if (!docs.scenes.renameScene(oldId, newId))
    {
        errorOut = "Failed to rename scene in document.";
        return false;
    }

    nlohmann::json* scene = docs.scenes.sceneJson(newId);
    if (scene == nullptr || !scene->is_object())
    {
        errorOut = "Scene missing after rename.";
        return false;
    }

    const std::string root = docs.assetRoot.empty() ? "." : docs.assetRoot;
    auto rewriteAndMaybeRenameFile = [&](std::string& pathField) {
        if (pathField.empty())
            return;
        const std::string rewritten = rewritePathStemId(pathField, oldId, newId);
        if (rewritten == pathField)
            return;
        // Only rename on disk when this path was unique to the scene.
        auto stripXz = [](std::string p) {
            if (p.size() > 3 && p.substr(p.size() - 3) == ".xz")
                p = p.substr(0, p.size() - 3);
            return p;
        };
        const std::string fieldKey = stripXz(pathField);
        bool wasUnique = false;
        for (const std::string& u : uniquePaths)
        {
            if (stripXz(u) == fieldKey)
            {
                wasUnique = true;
                break;
            }
        }
        if (wasUnique)
            renameAssetFilePair(root, pathField, rewritten);
        pathField = rewritten;
    };

    // Update common path fields on the scene JSON.
    if (scene->contains("image") && (*scene)["image"].is_string())
    {
        std::string img = (*scene)["image"].get<std::string>();
        rewriteAndMaybeRenameFile(img);
        (*scene)["image"] = img;
    }
    if (scene->contains("audio") && (*scene)["audio"].is_object())
    {
        nlohmann::json& audio = (*scene)["audio"];
        if (audio.contains("music") && audio["music"].is_object()
            && audio["music"].contains("path") && audio["music"]["path"].is_string())
        {
            std::string p = audio["music"]["path"].get<std::string>();
            rewriteAndMaybeRenameFile(p);
            audio["music"]["path"] = p;
        }
        if (audio.contains("ambient") && audio["ambient"].is_array())
        {
            for (auto& row : audio["ambient"])
            {
                if (!row.is_object() || !row.contains("path") || !row["path"].is_string())
                    continue;
                std::string p = row["path"].get<std::string>();
                rewriteAndMaybeRenameFile(p);
                row["path"] = p;
            }
        }
        if (audio.contains("sfx") && audio["sfx"].is_array())
        {
            for (auto& row : audio["sfx"])
            {
                if (!row.is_object() || !row.contains("path") || !row["path"].is_string())
                    continue;
                std::string p = row["path"].get<std::string>();
                rewriteAndMaybeRenameFile(p);
                row["path"] = p;
            }
        }
    }
    auto rewriteTtsBag = [&](const char* key) {
        if (!scene->contains(key) || !(*scene)[key].is_object())
            return;
        nlohmann::json& bag = (*scene)[key];
        if (bag.contains("ttsAudio") && bag["ttsAudio"].is_string())
        {
            std::string p = bag["ttsAudio"].get<std::string>();
            rewriteAndMaybeRenameFile(p);
            bag["ttsAudio"] = p;
        }
        // Folder resources/audio/tts/<oldId>/… — rename directory if present.
        const std::string oldDirRel = "resources/audio/tts/" + oldId;
        const std::string newDirRel = "resources/audio/tts/" + newId;
        const std::string oldDirAbs = pathJoin(root, oldDirRel);
        const std::string newDirAbs = pathJoin(root, newDirRel);
        if (DirectoryExists(oldDirAbs.c_str()) && !DirectoryExists(newDirAbs.c_str()))
        {
            ensureParentDirectoryExists(newDirAbs);
            std::rename(oldDirAbs.c_str(), newDirAbs.c_str());
        }
    };
    rewriteTtsBag("descriptionTts");
    rewriteTtsBag("examineTts");

    if (payloadToSync != nullptr)
    {
        payloadToSync->id = newId;
        rewriteAndMaybeRenameFile(payloadToSync->imagePath);
        rewriteAndMaybeRenameFile(payloadToSync->ambientPath);
        rewriteAndMaybeRenameFile(payloadToSync->musicPath);
        // Prefer JSON paths after rewrite.
        if (scene->contains("image") && (*scene)["image"].is_string())
            payloadToSync->imagePath = (*scene)["image"].get<std::string>();
        payloadToSync->ambientPath = docs.scenes.getSceneAmbientPath(newId);
        payloadToSync->musicPath = docs.scenes.getSceneMusicPath(newId);
        if (payloadToSync->ambientPath.empty())
            payloadToSync->ambientPath = "resources/audio/ambient/" + newId + ".mp3";
        if (payloadToSync->musicPath.empty())
            payloadToSync->musicPath = "resources/audio/music/" + newId + "_theme.mp3";
    }

    docs.markDirty();
    if (!docs.scenes.save())
    {
        errorOut = "Renamed in memory but failed to write scenes.json";
        return false;
    }
    docs.dirty = false;
    return true;
}

namespace
{

std::string normalizeSceneAssetPath(std::string path)
{
    path = trimCopy(path);
    if (path.size() > 3 && path.substr(path.size() - 3) == ".xz")
        path = path.substr(0, path.size() - 3);
    return path;
}

bool endsWithPathSuffix(const std::string& path, const char* suffix)
{
    const std::string s = suffix;
    return path.size() >= s.size()
        && path.compare(path.size() - s.size(), s.size(), s) == 0;
}

bool isSharedDefaultSceneSfx(const std::string& path)
{
    return endsWithPathSuffix(path, "/door_open.mp3")
        || endsWithPathSuffix(path, "/door_close.mp3");
}

void addSceneAssetPath(std::vector<std::string>& out, const std::string& raw)
{
    const std::string path = normalizeSceneAssetPath(raw);
    if (path.empty() || !isPlausibleResourcePath(path))
        return;
    if (isSharedDefaultSceneSfx(path))
        return;
    out.push_back(path);
}

void collectPathsFromTtsBag(const nlohmann::json& scene, const char* key, std::vector<std::string>& out)
{
    if (!scene.contains(key) || !scene[key].is_object())
        return;
    const nlohmann::json& bag = scene[key];
    if (bag.contains("ttsAudio") && bag["ttsAudio"].is_string())
        addSceneAssetPath(out, bag["ttsAudio"].get<std::string>());
    if (bag.contains("ttsAudioSegments") && bag["ttsAudioSegments"].is_array())
    {
        for (const nlohmann::json& seg : bag["ttsAudioSegments"])
        {
            if (seg.is_string())
                addSceneAssetPath(out, seg.get<std::string>());
        }
    }
}

void collectPathsFromSceneJson(const nlohmann::json& scene, std::vector<std::string>& out)
{
    if (!scene.is_object())
        return;

    if (scene.contains("image") && scene["image"].is_string())
        addSceneAssetPath(out, scene["image"].get<std::string>());

    const nlohmann::json audio = scene.value("audio", nlohmann::json::object());
    if (audio.is_object())
    {
        if (audio.contains("music"))
        {
            if (audio["music"].is_object())
                addSceneAssetPath(out, audio["music"].value("path", ""));
            else if (audio["music"].is_string())
                addSceneAssetPath(out, audio["music"].get<std::string>());
        }

        if (audio.contains("ambient"))
        {
            const nlohmann::json& ambient = audio["ambient"];
            if (ambient.is_array())
            {
                for (const nlohmann::json& entry : ambient)
                {
                    if (entry.is_object())
                        addSceneAssetPath(out, entry.value("path", ""));
                    else if (entry.is_string())
                        addSceneAssetPath(out, entry.get<std::string>());
                }
            }
            else if (ambient.is_object())
                addSceneAssetPath(out, ambient.value("path", ""));
            else if (ambient.is_string())
                addSceneAssetPath(out, ambient.get<std::string>());
        }

        if (audio.contains("sfx") && audio["sfx"].is_array())
        {
            for (const nlohmann::json& entry : audio["sfx"])
            {
                if (entry.is_object())
                    addSceneAssetPath(out, entry.value("path", ""));
                else if (entry.is_string())
                    addSceneAssetPath(out, entry.get<std::string>());
            }
        }
    }

    collectPathsFromTtsBag(scene, "descriptionTts", out);
    collectPathsFromTtsBag(scene, "examineTts", out);
}

} // namespace

std::vector<std::string> collectUniqueSceneAssetPaths(
    const DocumentWorkspace& docs,
    const std::string& sceneId)
{
    std::vector<std::string> unique;
    if (!docs.scenes.isLoaded() || sceneId.empty() || !docs.scenes.hasScene(sceneId))
        return unique;

    const nlohmann::json* target = docs.scenes.sceneJson(sceneId);
    if (target == nullptr)
        return unique;

    std::vector<std::string> targetPaths;
    collectPathsFromSceneJson(*target, targetPaths);
    if (targetPaths.empty())
        return unique;

    std::map<std::string, int> refCounts;
    for (const std::string& id : docs.scenes.sceneIds())
    {
        const nlohmann::json* scene = docs.scenes.sceneJson(id);
        if (scene == nullptr)
            continue;
        std::vector<std::string> paths;
        collectPathsFromSceneJson(*scene, paths);
        std::map<std::string, bool> seenInScene;
        for (const std::string& path : paths)
        {
            if (seenInScene[path])
                continue;
            seenInScene[path] = true;
            ++refCounts[path];
        }
    }

    std::map<std::string, bool> emitted;
    for (const std::string& path : targetPaths)
    {
        if (emitted[path])
            continue;
        emitted[path] = true;
        if (refCounts[path] == 1)
            unique.push_back(path);
    }
    return unique;
}

bool purgeSceneAssetFiles(
    const std::string& assetRoot,
    const std::vector<std::string>& relPaths)
{
    if (assetRoot.empty())
        return false;
    bool allOk = true;
    for (const std::string& rel : relPaths)
    {
        const std::string path = normalizeSceneAssetPath(rel);
        if (path.empty() || !isPlausibleResourcePath(path))
        {
            allOk = false;
            continue;
        }
        const std::string abs = pathJoin(assetRoot, path);
        const std::string xz = compressedAssetPath(abs);
        bool removedSomething = false;
        if (FileExists(abs.c_str()))
        {
            if (std::remove(abs.c_str()) != 0)
                allOk = false;
            else
                removedSomething = true;
        }
        if (FileExists(xz.c_str()))
        {
            if (std::remove(xz.c_str()) != 0)
                allOk = false;
            else
                removedSomething = true;
        }
        (void)removedSomething;
    }
    return allOk;
}

std::string sceneAiPreviewRelPath(const std::string& sceneId, int target)
{
    if (target == 1)
        return "resources/.authoring/preview_" + sceneId + "_image.png";
    if (target == 2)
        return "resources/.authoring/preview_" + sceneId + "_ambient.mp3";
    if (target == 3)
        return "resources/.authoring/preview_" + sceneId + "_music.mp3";
    return "";
}

std::string sceneAiLiveRelPath(const SceneAuthoringPayload& payload, int target)
{
    if (target == 1)
    {
        return payload.imagePath.empty()
            ? ("resources/images/" + payload.id + ".png")
            : payload.imagePath;
    }
    if (target == 2)
    {
        return payload.ambientPath.empty()
            ? ("resources/audio/ambient/" + payload.id + ".mp3")
            : payload.ambientPath;
    }
    if (target == 3)
    {
        return payload.musicPath.empty()
            ? ("resources/audio/music/" + payload.id + "_theme.mp3")
            : payload.musicPath;
    }
    return "";
}

std::vector<SceneAiJob> buildSceneAiPreviewJobs(
    const SceneAuthoringPayload& payload,
    int aiTargetFilter,
    const std::string& styleBlock)
{
    // Reuse prompt construction, then override outPath to preview locations.
    std::vector<SceneAiJob> jobs =
        buildSceneAiJobs(payload, aiTargetFilter, styleBlock);
    for (SceneAiJob& job : jobs)
    {
        int target = 1;
        if (job.type == SceneAiJobType::GenerateAmbient)
            target = 2;
        else if (job.type == SceneAiJobType::GenerateMusic)
            target = 3;
        else if (job.type == SceneAiJobType::GenerateEnterSfx)
            target = 4;
        else if (job.type == SceneAiJobType::GenerateExitSfx)
            target = 5;
        const std::string preview = sceneAiPreviewRelPath(payload.id, target);
        if (!preview.empty())
            job.outPath = preview;
    }
    return jobs;
}

std::string writeSceneAiPreviewJobsFile(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const SceneAuthoringPayload& payload,
    int aiTargetFilter)
{
    const std::string styleBlock =
        formatGenerationStyleBlock(loadGenerationStyleFilter(resourceDirHint));
    const std::vector<SceneAiJob> jobs =
        buildSceneAiPreviewJobs(payload, aiTargetFilter, styleBlock);
    if (jobs.empty())
        return "";

    nlohmann::json jobsRoot;
    jobsRoot["sceneId"] = payload.id;
    jobsRoot["itemId"] = payload.id;
    jobsRoot["kind"] = "scene";
    jobsRoot["preview"] = true;
    nlohmann::json arr = nlohmann::json::array();
    for (const SceneAiJob& job : jobs)
        arr.push_back(sceneAiJobToJson(job));
    jobsRoot["jobs"] = arr;

    const std::string gameRoot = findGameRoot(assetRootHint, resourceDirHint);
    const std::string authoringDir =
        pathJoin(pathJoin(gameRoot, "resources"), ".authoring");
#if !defined(_WIN32)
    std::string mkdirCmd = "mkdir -p " + shellQuote(authoringDir);
    std::system(mkdirCmd.c_str());
#endif
    const std::string jobsPath =
        pathJoin(authoringDir, payload.id + "_ai_preview_jobs.json");
    std::ofstream out(jobsPath.c_str());
    if (!out)
        return "";
    out << jobsRoot.dump(2);
    return jobsPath;
}

namespace
{

bool copyFileBytes(const std::string& from, const std::string& to)
{
    std::ifstream in(from.c_str(), std::ios::binary);
    if (!in)
        return false;
    std::vector<char> buf(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    if (!ensureParentDirectoryExists(to))
        return false;
    std::ofstream out(to.c_str(), std::ios::binary);
    if (!out)
        return false;
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    return static_cast<bool>(out);
}

} // namespace

bool rotateLiveAssetBackup(
    const std::string& assetRoot,
    const std::string& relPath)
{
    if (assetRoot.empty() || relPath.empty())
        return false;
    if (relPath.find("..") != std::string::npos)
        return false;
    if (relPath.rfind("resources/", 0) != 0)
        return false;
    // Never rotate authoring scratch files.
    if (relPath.find("/.authoring/") != std::string::npos
        || relPath.rfind("resources/.authoring/", 0) == 0)
        return false;

    const std::string liveAbs = pathJoin(assetRoot, relPath);
    // Split directory / filename / extension (last dot in basename).
    const std::string slash = "/\\";
    const size_t slashPos = liveAbs.find_last_of(slash);
    const std::string dir =
        (slashPos == std::string::npos) ? std::string{} : liveAbs.substr(0, slashPos + 1);
    const std::string base =
        (slashPos == std::string::npos) ? liveAbs : liveAbs.substr(slashPos + 1);
    const size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
        return false;
    const std::string stem = base.substr(0, dot);
    const std::string ext = base.substr(dot);
    const std::string p1 = dir + stem + "_1" + ext;
    const std::string p2 = dir + stem + "_2" + ext;
    const std::string liveXz = compressedAssetPath(liveAbs);
    const std::string p1Xz = compressedAssetPath(p1);
    const std::string p2Xz = compressedAssetPath(p2);

    auto renameReplace = [](const std::string& src, const std::string& dst) {
        if (!FileExists(src.c_str()))
            return;
        if (FileExists(dst.c_str()))
            std::remove(dst.c_str());
        std::rename(src.c_str(), dst.c_str());
    };

    renameReplace(p1, p2);
    renameReplace(p1Xz, p2Xz);
    renameReplace(liveAbs, p1);
    renameReplace(liveXz, p1Xz);
    return true;
}

bool acceptSceneAiPreview(
    DocumentWorkspace& docs,
    const std::string& sceneId,
    int target,
    const std::string& previewRelPath,
    const std::string& liveRelPath,
    std::string& errorOut)
{
    errorOut.clear();
    if (previewRelPath.empty() || liveRelPath.empty())
    {
        errorOut = "Missing preview or live path.";
        return false;
    }

    const std::string gameRoot =
        findGameRoot(docs.assetRoot, docs.resourceDir);
    const std::string previewAbs = pathJoin(gameRoot, previewRelPath);
    const std::string previewXz = compressedAssetPath(previewAbs);
    const std::string liveAbs = pathJoin(gameRoot, liveRelPath);
    const std::string liveXz = compressedAssetPath(liveAbs);

    // Preserve prior live content as _1 / _2 before accepting the preview.
    rotateLiveAssetBackup(gameRoot, liveRelPath);

    bool copied = false;
    if (FileExists(previewAbs.c_str()))
    {
        if (!copyFileBytes(previewAbs, liveAbs))
        {
            errorOut = "Failed to copy preview to " + liveRelPath;
            return false;
        }
        copied = true;
    }
    if (FileExists(previewXz.c_str()))
    {
        if (!copyFileBytes(previewXz, liveXz))
        {
            errorOut = "Failed to copy compressed preview to live .xz";
            return false;
        }
        copied = true;
    }
    if (!copied)
    {
        errorOut = "Preview file not found: " + previewRelPath;
        return false;
    }

    nlohmann::json* scene = docs.scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
    {
        errorOut = "Scene missing after accept.";
        return false;
    }

    std::string jsonPath = liveRelPath;
    if (jsonPath.size() > 3 && jsonPath.substr(jsonPath.size() - 3) == ".xz")
        jsonPath = jsonPath.substr(0, jsonPath.size() - 3);

    if (target == 1)
    {
        (*scene)["image"] = jsonPath;
    }
    else if (target == 2 || target == 3)
    {
        nlohmann::json audio = scene->value("audio", nlohmann::json::object());
        if (!audio.is_object())
            audio = nlohmann::json::object();
        if (target == 3)
        {
            audio["music"] = {
                {"path", jsonPath},
                {"volume", 0.2},
                {"fade_in", 3.0},
                {"fade_out", 2.5},
                {"loop", true}};
        }
        else
        {
            audio["ambient"] = nlohmann::json::array({
                {{"path", jsonPath},
                 {"volume", 0.45},
                 {"fade_in", 1.5},
                 {"fade_out", 1.5},
                 {"loop", true}}});
        }
        (*scene)["audio"] = audio;
    }

    docs.markDirty();
    if (!docs.scenes.save())
    {
        errorOut = "Accepted file but failed to save scenes.json";
        return false;
    }
    docs.dirty = false;
    discardSceneAiPreviewFiles(docs.assetRoot, docs.resourceDir, previewRelPath);
    return true;
}

void discardSceneAiPreviewFiles(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& previewRelPath)
{
    if (previewRelPath.empty())
        return;
    const std::string gameRoot = findGameRoot(assetRootHint, resourceDirHint);
    const std::string abs = pathJoin(gameRoot, previewRelPath);
    const std::string xz = compressedAssetPath(abs);
    if (FileExists(abs.c_str()))
        std::remove(abs.c_str());
    if (FileExists(xz.c_str()))
        std::remove(xz.c_str());
}

void applySceneAiOutputsToPayload(
    SceneAuthoringPayload& payload,
    DocumentWorkspace& docs,
    const std::string& sceneId)
{
    nlohmann::json* scene = docs.scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
        return;

    // Prefer known default paths if files exist under resources.
    auto tryPath = [&](const std::string& rel) -> std::string {
        if (rel.empty())
            return "";
        // strip .xz for JSON path convention
        std::string p = rel;
        if (p.size() > 3 && p.substr(p.size() - 3) == ".xz")
            p = p.substr(0, p.size() - 3);
        return p;
    };

    if (payload.imagePath.empty() || !isPlausibleResourcePath(payload.imagePath))
        payload.imagePath = "resources/images/" + sceneId + ".png";
    if (payload.ambientPath.empty())
        payload.ambientPath = "resources/audio/ambient/" + sceneId + ".mp3";
    if (payload.musicPath.empty())
        payload.musicPath = "resources/audio/music/" + sceneId + "_theme.mp3";

    (*scene)["image"] = tryPath(payload.imagePath);
    nlohmann::json audio = scene->value("audio", nlohmann::json::object());
    if (!payload.musicPath.empty())
    {
        audio["music"] = {
            {"path", tryPath(payload.musicPath)},
            {"volume", 0.2},
            {"fade_in", 3.0},
            {"fade_out", 2.5},
            {"loop", true}};
    }
    if (!payload.ambientPath.empty())
    {
        audio["ambient"] = nlohmann::json::array({
            {{"path", tryPath(payload.ambientPath)},
             {"volume", 0.45},
             {"fade_in", 1.5},
             {"fade_out", 1.5},
             {"loop", true}}});
    }
    (*scene)["audio"] = audio;

    // Pull TTS markup results written by the Python chat jobs.
    const std::string gameRoot = findGameRoot(docs.assetRoot, docs.resourceDir);
    const std::string jobsPath = pathJoin(
        pathJoin(pathJoin(gameRoot, "resources"), ".authoring"),
        sceneId + "_ai_jobs.json");
    try
    {
        std::ifstream jobsIn(jobsPath.c_str());
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
                    const std::string type = job.value("type", "");
                    std::string text = job.value("resultText", "");
                    if (text.empty())
                    {
                        const std::string outRel = job.value("outPath", "");
                        if (!outRel.empty())
                        {
                            const std::string abs = pathJoin(gameRoot, outRel);
                            std::ifstream tf(abs.c_str());
                            if (tf)
                                text.assign(
                                    (std::istreambuf_iterator<char>(tf)),
                                    std::istreambuf_iterator<char>());
                        }
                    }
                    text = trimCopy(text);
                    if (text.empty())
                        continue;
                    if (type == "generate_scene_description_tts_text")
                    {
                        payload.ttsDescription = text;
                        (*scene)["descriptionTts"] = {
                            {"tts", true},
                            {"ttsText", text},
                            {"ttsVoice", ""},
                            {"ttsAudio",
                             "resources/audio/tts/" + sceneId
                                 + "/descriptionTts.mp3"}};
                    }
                    else if (type == "generate_scene_examine_tts_text")
                    {
                        payload.ttsExamineDetails = text;
                        (*scene)["examineTts"] = {
                            {"tts", true},
                            {"ttsText", text},
                            {"ttsVoice", ""},
                            {"ttsAudio",
                             "resources/audio/tts/" + sceneId + "/examineTts.mp3"}};
                    }
                }
            }
        }
    }
    catch (const nlohmann::json::exception&)
    {
    }

    if (payload.ttsEnabled)
    {
        (*scene)["ttsEnabled"] = true;
        (*scene)["ttsDefaultVoice"] = payload.ttsDefaultVoice.empty()
            ? "leo"
            : payload.ttsDefaultVoice;
        (*scene)["actions"]["speak"] = true;
    }

    docs.markDirty();
    docs.scenes.save();
    docs.dirty = false;
}

bool sceneTtsTextHasWord(const std::string& text)
{
    for (char ch : text)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)))
            return true;
    }
    return false;
}

std::string runSceneVoiceRefresh(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& sceneId,
    const std::string& sessionApiKey,
    std::atomic<bool>* cancelFlag)
{
    if (sceneId.empty())
        return "Voice refresh failed: empty scene id.";
    if (cancelFlag != nullptr && cancelFlag->load())
        return "Cancelled.";
    const std::string gameRoot = findGameRoot(assetRootHint, resourceDirHint);
    std::string bin;
    if (const char* env = std::getenv("HIGHLINE_GAME_BIN");
        env != nullptr && env[0] != '\0' && FileExists(env))
        bin = env;
    if (bin.empty())
    {
        const char* candidates[] = {
            "Highline Ridge", "highline_ridge", "timberline", "HighlineRidge"};
        const char* buildDirs[] = {".", "build", "build-release", "cmake-build-debug"};
        for (const char* dir : buildDirs)
        {
            for (const char* name : candidates)
            {
                const std::string p = (std::string(dir) == ".")
                    ? pathJoin(gameRoot, name)
                    : pathJoin(pathJoin(gameRoot, dir), name);
                if (FileExists(p.c_str()))
                {
                    bin = p;
                    break;
                }
            }
            if (!bin.empty())
                break;
        }
    }
    if (bin.empty())
    {
        return "Voice refresh failed: game binary not found. Build Highline Ridge "
               "(dev/authoring) or set HIGHLINE_GAME_BIN.";
    }

    const std::string logFile = pathJoin(
        pathJoin(pathJoin(gameRoot, "resources"), ".authoring"),
        sceneId + "_voice_refresh.log");
#if !defined(_WIN32)
    {
        std::string mkdirCmd = "mkdir -p "
            + shellQuote(pathJoin(pathJoin(gameRoot, "resources"), ".authoring"));
        std::system(mkdirCmd.c_str());
    }
#endif

    std::ostringstream cmd;
    cmd << shellQuote(bin);
    if (!sessionApiKey.empty())
        cmd << " --key=" << shellQuote(sessionApiKey);
    cmd << " --refresh=" << shellQuote(sceneId);
    cmd << " > " << shellQuote(logFile) << " 2>&1";

#if !defined(_WIN32)
    const pid_t pid = fork();
    if (pid < 0)
        return "Voice refresh failed: fork error.";
    if (pid == 0)
    {
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", cmd.str().c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    setpgid(pid, pid);
    int code = 0;
    while (true)
    {
        int st = 0;
        const pid_t waited = waitpid(pid, &st, WNOHANG);
        if (waited == pid)
        {
            code = st;
            break;
        }
        if (waited < 0)
            return "Voice refresh failed: waitpid error.";
        if (cancelFlag != nullptr && cancelFlag->load())
        {
            kill(-pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            kill(-pid, SIGKILL);
            waitpid(pid, &st, 0);
            return "Cancelled.";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    int exitStatus = code;
    if (WIFEXITED(code))
        exitStatus = WEXITSTATUS(code);
#else
    const int code = std::system(cmd.str().c_str());
    int exitStatus = code;
#endif
    if (exitStatus == 0)
        return "Voice data generated for \"" + sceneId + "\"";
    return "Voice refresh failed (exit " + std::to_string(exitStatus)
        + "). See " + logFile;
}

} // namespace timberline_editor
