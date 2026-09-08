/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Manage Use-action map edges: bind useExit or interaction exitSceneId.
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_USE_TRANSITION_DIALOG_H
#define TIMBERLINE_SCENE_USE_TRANSITION_DIALOG_H

#include "DocumentWorkspace.h"
#include "SceneGraphModel.h"

#include <functional>
#include <string>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct SceneUseTransitionDialog
{
    DocumentWorkspace* docs = nullptr;
    SceneGraphModel* graph = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;

    std::string fromId;
    std::string toId;
    std::string preferredBinding; // preselect if set
    std::string selectedBinding;

    std::vector<SceneGraphModel::UseBinding> rows;
    float listScroll = 0.0f;

    std::string status;
    std::string error;

    std::function<void()> onSaved;

    void openForLink(
        const std::string& fromMapNode,
        const std::string& toMapNode,
        const std::string& bindingHint = {});
    void closeDialog();
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

    /** Create useExit (if free) or a stub interaction → toId. */
    bool createNewBinding();

private:
    void refreshRows();
    bool applySelected();
    bool clearSelected();
};

} // namespace timberline_editor

#endif
