/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneAuthoring.h"
#include "EditorPaths.h"
#include "ImageCompression.h"
#include "PlatformPath.h"

#include <raylib.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
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
    scene["actions"] = {
        {"examine", true},
        {"speak", payload.speakEnabled},
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
    }

    return scene;
}

std::vector<SceneAiJob> buildSceneAiJobs(
    const SceneAuthoringPayload& payload,
    int aiTargetFilter)
{
    std::vector<SceneAiJob> jobs;
    const std::string ctx = payload.description.empty()
        ? payload.id
        : payload.description;
    const std::string basePrompt =
        "Timberline 1890s high-altitude Colorado frontier detective game. "
        "Period-accurate, painterly realistic, no modern objects, no text, no UI. ";

    auto want = [aiTargetFilter](int target) {
        return aiTargetFilter == 0 || aiTargetFilter == target;
    };

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
        job.prompt = "Loopable ambient bed for scene: " + ctx;
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
        job.prompt = "Loopable period instrumental underscore for scene: " + ctx;
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
    return jobs;
}

SceneUpsertResult upsertScene(
    DocumentWorkspace& docs,
    SceneAuthoringPayload& payload,
    int aiTargetFilter,
    bool writeAiJobs)
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
    if (docs.scenes.hasScene(payload.id))
    {
        result.message = "Scene id already exists: " + payload.id;
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

    nlohmann::json scene = buildSceneJson(payload);
    if (!docs.scenes.createScene(payload.id, scene))
    {
        result.message = "Failed to create scene object in document.";
        return result;
    }

    if (writeAiJobs)
    {
        result.jobs = buildSceneAiJobs(payload, aiTargetFilter);
        if (!result.jobs.empty())
        {
            nlohmann::json jobsRoot;
            jobsRoot["sceneId"] = payload.id;
            jobsRoot["itemId"] = payload.id; // runner also accepts itemId key
            jobsRoot["kind"] = "scene";
            nlohmann::json arr = nlohmann::json::array();
            for (const SceneAiJob& job : result.jobs)
            {
                std::string type = "generate_image";
                if (job.type == SceneAiJobType::GenerateAmbient)
                    type = "generate_ambient_sound";
                else if (job.type == SceneAiJobType::GenerateMusic)
                    type = "generate_music";
                else if (job.type == SceneAiJobType::GenerateEnterSfx)
                    type = "generate_use_sound";
                else if (job.type == SceneAiJobType::GenerateExitSfx)
                    type = "generate_examine_sound";
                else if (job.type == SceneAiJobType::GenerateImage)
                    type = "generate_image";
                arr.push_back({
                    {"type", type},
                    {"prompt", job.prompt},
                    {"outPath", job.outPath},
                    {"action", job.action.empty() ? "examine" : job.action}});
            }
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
        result.message = "Created in memory but failed to write scenes.json";
        return result;
    }
    docs.dirty = false;
    result.ok = true;
    result.message = "Created scene \"" + payload.id + "\"";
    if (!result.jobsFilePath.empty())
        result.message += " | AI jobs: " + result.jobsFilePath;
    return result;
}

std::string runSceneAuthoringAiJobsFile(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& jobsFilePath,
    const std::string& sessionApiKey)
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

    int code = runWith("python3");
#if !defined(_WIN32)
    if (WIFEXITED(code) && WEXITSTATUS(code) != 0)
        code = runWith("python");
    else if (!WIFEXITED(code))
        code = runWith("python");
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
    const std::string& sessionApiKey)
{
    const std::string gameRoot = findGameRoot(assetRootHint, resourceDirHint);
    const std::string jobsPath = pathJoin(
        pathJoin(pathJoin(gameRoot, "resources"), ".authoring"),
        sceneId + "_ai_jobs.json");
    const std::string msg = runSceneAuthoringAiJobsFile(
        assetRootHint, resourceDirHint, jobsPath, sessionApiKey);
    if (msg == "AI generate finished OK")
        return "AI generate finished OK for " + sceneId;
    return msg;
}

std::string writeSceneAiJobsFile(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const SceneAuthoringPayload& payload,
    int aiTargetFilter)
{
    const std::vector<SceneAiJob> jobs = buildSceneAiJobs(payload, aiTargetFilter);
    if (jobs.empty())
        return "";

    nlohmann::json jobsRoot;
    jobsRoot["sceneId"] = payload.id;
    jobsRoot["itemId"] = payload.id;
    jobsRoot["kind"] = "scene";
    nlohmann::json arr = nlohmann::json::array();
    for (const SceneAiJob& job : jobs)
    {
        std::string type = "generate_image";
        if (job.type == SceneAiJobType::GenerateAmbient)
            type = "generate_ambient_sound";
        else if (job.type == SceneAiJobType::GenerateMusic)
            type = "generate_music";
        else if (job.type == SceneAiJobType::GenerateEnterSfx)
            type = "generate_use_sound";
        else if (job.type == SceneAiJobType::GenerateExitSfx)
            type = "generate_examine_sound";
        arr.push_back({
            {"type", type},
            {"prompt", job.prompt},
            {"outPath", job.outPath},
            {"action", job.action.empty() ? "examine" : job.action}});
    }
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
    out.speakEnabled = scene->value("actions", nlohmann::json::object())
                           .value("speak", false);
    out.ttsEnabled = scene->value("ttsEnabled", false);
    out.ttsDefaultVoice = scene->value("ttsDefaultVoice", "leo");
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
    int aiTargetFilter)
{
    // Reuse prompt construction, then override outPath to preview locations.
    std::vector<SceneAiJob> jobs = buildSceneAiJobs(payload, aiTargetFilter);
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
    const std::vector<SceneAiJob> jobs =
        buildSceneAiPreviewJobs(payload, aiTargetFilter);
    if (jobs.empty())
        return "";

    nlohmann::json jobsRoot;
    jobsRoot["sceneId"] = payload.id;
    jobsRoot["itemId"] = payload.id;
    jobsRoot["kind"] = "scene";
    jobsRoot["preview"] = true;
    nlohmann::json arr = nlohmann::json::array();
    for (const SceneAiJob& job : jobs)
    {
        std::string type = "generate_image";
        if (job.type == SceneAiJobType::GenerateAmbient)
            type = "generate_ambient_sound";
        else if (job.type == SceneAiJobType::GenerateMusic)
            type = "generate_music";
        else if (job.type == SceneAiJobType::GenerateEnterSfx)
            type = "generate_use_sound";
        else if (job.type == SceneAiJobType::GenerateExitSfx)
            type = "generate_examine_sound";
        arr.push_back({
            {"type", type},
            {"prompt", job.prompt},
            {"outPath", job.outPath},
            {"action", job.action.empty() ? "examine" : job.action}});
    }
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
    docs.markDirty();
    docs.scenes.save();
    docs.dirty = false;
}

} // namespace timberline_editor
