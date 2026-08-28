/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Scene Examine / Use / interaction stat-delta editor.
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_EFFECTS_DIALOG_H
#define TIMBERLINE_SCENE_EFFECTS_DIALOG_H

#include "DocumentWorkspace.h"

#include <functional>
#include <string>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct StatDeltaSet
{
    float health = 0.0f;
    float energy = 0.0f;
    float resolve = 0.0f;
    float lucidity = 0.0f;
    float charisma = 0.0f;

    bool anyNonZero() const
    {
        return health != 0.0f || energy != 0.0f || resolve != 0.0f || lucidity != 0.0f
            || charisma != 0.0f;
    }
};

struct SceneInteractionEffectsEdit
{
    std::string id;
    std::string label;
    StatDeltaSet deltas;
};

struct SceneEffectsDialog
{
    DocumentWorkspace* docs = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;
    float scrollY = 0.0f;

    std::string sceneId;
    float examineLucidityDelta = 0.0f;
    bool examineLucidityOncePerDay = false;
    StatDeltaSet useDeltas;
    bool useRepeatStatus = false;
    std::vector<SceneInteractionEffectsEdit> interactions;

    std::string status;
    std::string error;
    int focusField = -1; // encoded field id for typing numbers

    std::function<void()> onSaved;

    void openForScene(const std::string& id);
    void closeDialog();
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    void loadFromScene();
    bool saveToScene();
    void typeIntoFocusedField();
    float* focusFloat();
};

/** Compact one-line summary of non-zero scene effects for the preview pane. */
std::string summarizeSceneEffects(const nlohmann::json& scene);

} // namespace timberline_editor

#endif
