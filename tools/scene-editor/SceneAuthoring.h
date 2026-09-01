/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * New-scene authoring payload, JSON upsert, and AI job runner hooks.
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_AUTHORING_H
#define TIMBERLINE_SCENE_AUTHORING_H

#include "DocumentWorkspace.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <string>
#include <vector>

namespace timberline_editor
{

struct SceneAuthoringPayload
{
    std::string id;
    std::string description;
    std::string examineDetails;
    std::string imagePath;
    std::string ambientPath;
    std::string musicPath;
    /** Enter/exit one-shots. Empty by default — do not stamp shared door SFX
     *  onto outdoor/trail scenes. Interiors can set these explicitly. */
    std::string enterSfxPath;
    std::string exitSfxPath;
    bool speakEnabled = false; // Coupled with ttsEnabled in New Scene UI.
    bool ttsEnabled = false;
    std::string ttsDefaultVoice = "leo";
    std::string ttsDescription;     // → descriptionTts.ttsText
    std::string ttsExamineDetails;  // → examineTts.ttsText
    float layoutX = 0.0f;
    float layoutY = 0.0f;
    int layoutLevel = 0;
};

enum class SceneAiJobType
{
    GenerateImage,
    GenerateAmbient,
    GenerateMusic,
    GenerateEnterSfx,
    GenerateExitSfx,
    GenerateDescriptionTtsText,
    GenerateExamineTtsText
};

struct SceneAiJob
{
    SceneAiJobType type = SceneAiJobType::GenerateImage;
    std::string prompt;
    std::string outPath;
    std::string action; // ambient | music | enter | exit | description_tts | examine_tts
    std::string sourceText;
    std::string defaultVoice;
    std::string resultText; // Filled by chat jobs (TTS markup).
};

std::string sanitizeSceneId(const std::string& raw);
bool isValidSceneId(const std::string& id);

/** Build a minimal but playable scene object for scenes.json. */
nlohmann::json buildSceneJson(const SceneAuthoringPayload& payload);

/**
 * Validate + write scene into docs->scenes, optional AI job file, save.
 * targetFilter: 0=all selected, 1=image, 2=ambient, 3=music, 4=enter sfx, 5=exit sfx,
 *               6=description TTS text, 7=examine TTS text
 */
struct SceneUpsertResult
{
    bool ok = false;
    std::string message;
    std::string jobsFilePath;
    std::vector<SceneAiJob> jobs;
};

SceneUpsertResult upsertScene(
    DocumentWorkspace& docs,
    SceneAuthoringPayload& payload,
    int aiTargetFilter,
    bool writeAiJobs,
    bool backupRotate = false);

/** Run tools/run_item_authoring_ai.py against a scene jobs file. */
std::string runSceneAuthoringAiJobs(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& sceneId,
    const std::string& sessionApiKey,
    std::atomic<bool>* cancelFlag = nullptr);

/** Run the AI runner against an explicit jobs JSON path. */
std::string runSceneAuthoringAiJobsFile(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& jobsFilePath,
    const std::string& sessionApiKey,
    std::atomic<bool>* cancelFlag = nullptr);

/** Merge generated asset paths (+ TTS markup results) back into payload / scene JSON. */
void applySceneAiOutputsToPayload(
    SceneAuthoringPayload& payload,
    DocumentWorkspace& docs,
    const std::string& sceneId);

/** True if text contains at least one alphanumeric word. */
bool sceneTtsTextHasWord(const std::string& text);

/**
 * Shell the game binary: --key=... --refresh=<sceneId>
 * Returns a human-readable status message.
 */
std::string runSceneVoiceRefresh(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& sceneId,
    const std::string& sessionApiKey,
    std::atomic<bool>* cancelFlag = nullptr);

std::vector<SceneAiJob> buildSceneAiJobs(
    const SceneAuthoringPayload& payload,
    int aiTargetFilter,
    const std::string& styleBlock = {});

/** Write resources/.authoring/<id>_ai_jobs.json without creating a scene. */
std::string writeSceneAiJobsFile(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const SceneAuthoringPayload& payload,
    int aiTargetFilter,
    bool backupRotate = false);

/**
 * Before overwriting a live asset while editing an existing scene:
 * name_1.ext (prior) → name_2.ext; name.ext → name_1.ext (+ sibling .xz).
 * Rel path is under assetRoot (e.g. resources/images/foo.png).
 */
bool rotateLiveAssetBackup(
    const std::string& assetRoot,
    const std::string& relPath);

/** Load authoring fields from an existing scene (description, paths, etc.). */
bool fillPayloadFromScene(
    const DocumentWorkspace& docs,
    const std::string& sceneId,
    SceneAuthoringPayload& out);

/**
 * Rename scene id in scenes.json (+ inbound exits). When a resource path's
 * filename contains oldId, rename that unique file (and .xz) to use newId and
 * update the path on the scene. Shared paths are left alone.
 */
bool renameSceneAuthoring(
    DocumentWorkspace& docs,
    const std::string& oldId,
    const std::string& newId,
    SceneAuthoringPayload* payloadToSync,
    std::string& errorOut);

/** True if relPath is under resources/ and exists (or sibling .xz exists). */
bool sceneImageReferenceExists(
    const std::string& assetRoot,
    const std::string& relPath);

/**
 * Asset paths referenced only by sceneId (count == 1 across all scenes).
 * Scans image, audio.music/ambient/sfx paths, and descriptionTts/examineTts
 * ttsAudio (+ ttsAudioSegments). Paths are normalized (trailing .xz stripped).
 * Shared defaults ending in /door_open.mp3 or /door_close.mp3 are skipped.
 */
std::vector<std::string> collectUniqueSceneAssetPaths(
    const DocumentWorkspace& docs,
    const std::string& sceneId);

/** Delete each relative path under assetRoot and sibling .xz if present. */
bool purgeSceneAssetFiles(
    const std::string& assetRoot,
    const std::vector<std::string>& relPaths);

/**
 * Preview out-path under resources/.authoring/ for regenerate-with-Accept/Revert.
 * target: 1=image, 2=ambient, 3=music
 */
std::string sceneAiPreviewRelPath(const std::string& sceneId, int target);

/** Live destination path for a regenerate target (from payload / defaults). */
std::string sceneAiLiveRelPath(const SceneAuthoringPayload& payload, int target);

/**
 * Build jobs that write to preview paths (not live scene assets).
 * targetFilter: 1=image, 2=ambient, 3=music (0 not used for assist).
 */
std::vector<SceneAiJob> buildSceneAiPreviewJobs(
    const SceneAuthoringPayload& payload,
    int aiTargetFilter,
    const std::string& styleBlock = {});

std::string writeSceneAiPreviewJobsFile(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const SceneAuthoringPayload& payload,
    int aiTargetFilter);

/**
 * Copy preview asset (+ optional .xz) over the live path and update scene JSON.
 * target: 1=image, 2=ambient, 3=music
 */
bool acceptSceneAiPreview(
    DocumentWorkspace& docs,
    const std::string& sceneId,
    int target,
    const std::string& previewRelPath,
    const std::string& liveRelPath,
    std::string& errorOut);

/** Delete preview file and sibling .xz if present. */
void discardSceneAiPreviewFiles(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& previewRelPath);

} // namespace timberline_editor

#endif /* TIMBERLINE_SCENE_AUTHORING_H */
