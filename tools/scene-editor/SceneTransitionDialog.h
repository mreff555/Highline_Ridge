/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Edit constrained enter/exit transition SFX for a map exit edge.
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_TRANSITION_DIALOG_H
#define TIMBERLINE_SCENE_TRANSITION_DIALOG_H

#include "DocumentWorkspace.h"
#include "SceneGraphModel.h"

#include <functional>
#include <string>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct SceneTransitionDialog
{
    DocumentWorkspace* docs = nullptr;
    SceneGraphModel* graph = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;

    std::string sceneA;
    std::string sceneB;
    std::string ownerId;    // scene that stores audio.sfx for this edge
    std::string neighborId; // the other endpoint

    std::string enterPath;
    std::string exitPath;
    int focusField = 0; // 0 enter, 1 exit
    float sfxListScroll = 0.0f;
    std::vector<std::string> sfxFiles; // relative resources/audio/sfx/*.mp3

    std::string status;
    std::string error;

    std::function<void()> onSaved;

    void openForLink(
        const std::string& fromId,
        const std::string& toId,
        const std::string& preferOwnerId);
    void closeDialog();
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    void refreshSfxFileList();
    void loadPathsFromOwner();
    void typeIntoFocusedField();
    std::string* focusedPath();
    void cycleOwner();
    bool applyChanges();
};

} // namespace timberline_editor

#endif
