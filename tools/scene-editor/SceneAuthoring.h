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
    std::string enterSfxPath = "resources/audio/sfx/door_open.mp3";
    std::string exitSfxPath = "resources/audio/sfx/door_close.mp3";
    bool speakEnabled = false;
    bool ttsEnabled = false;
    std::string ttsDefaultVoice = "leo";
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
    GenerateExitSfx
};

struct SceneAiJob
{
    SceneAiJobType type = SceneAiJobType::GenerateImage;
    std::string prompt;
    std::string outPath;
    std::string action; // ambient | music | enter | exit
};

std::string sanitizeSceneId(const std::string& raw);
bool isValidSceneId(const std::string& id);

/** Build a minimal but playable scene object for scenes.json. */
nlohmann::json buildSceneJson(const SceneAuthoringPayload& payload);

/**
 * Validate + write scene into docs->scenes, optional AI job file, save.
 * targetFilter: 0=all selected, 1=image, 2=ambient, 3=music, 4=enter sfx, 5=exit sfx
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
    bool writeAiJobs);

/** Run tools/run_item_authoring_ai.py against a scene jobs file. */
std::string runSceneAuthoringAiJobs(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& sceneId,
    const std::string& sessionApiKey);

/** Run the AI runner against an explicit jobs JSON path. */
std::string runSceneAuthoringAiJobsFile(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const std::string& jobsFilePath,
    const std::string& sessionApiKey);

/** Merge generated asset paths back into payload / scene JSON after AI run. */
void applySceneAiOutputsToPayload(
    SceneAuthoringPayload& payload,
    DocumentWorkspace& docs,
    const std::string& sceneId);

std::vector<SceneAiJob> buildSceneAiJobs(
    const SceneAuthoringPayload& payload,
    int aiTargetFilter);

/** Write resources/.authoring/<id>_ai_jobs.json without creating a scene. */
std::string writeSceneAiJobsFile(
    const std::string& assetRootHint,
    const std::string& resourceDirHint,
    const SceneAuthoringPayload& payload,
    int aiTargetFilter);

/** Load authoring fields from an existing scene (description, paths, etc.). */
bool fillPayloadFromScene(
    const DocumentWorkspace& docs,
    const std::string& sceneId,
    SceneAuthoringPayload& out);

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
    int aiTargetFilter);

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
