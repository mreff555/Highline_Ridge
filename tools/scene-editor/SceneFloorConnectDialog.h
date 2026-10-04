/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Connect a scene to another floor via mutually exclusive up/down exits.
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_FLOOR_CONNECT_DIALOG_H
#define TIMBERLINE_SCENE_FLOOR_CONNECT_DIALOG_H

#include "DocumentWorkspace.h"
#include "SceneGraphModel.h"

#include <functional>
#include <string>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct SceneFloorConnectDialog
{
    DocumentWorkspace* docs = nullptr;
    SceneGraphModel* graph = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;

    std::string sourceId; // parent scene being connected
    int sourceLevel = 0;

    /** true = Above (source goes up / target is above), false = Below. */
    bool connectAbove = true;

    struct Row
    {
        std::string sceneId;
        int level = 0;
        bool isCurrentLink = false; // already our vertical partner
        bool occupied = false;      // has some other vertical link
        bool selectable = false;
    };
    std::vector<Row> rows;
    float listScroll = 0.0f;
    /** Pending choice in the list (Accept applies this). */
    std::string selectedId;

    std::string status;
    std::string error;

    std::function<void()> onSaved;

    void openForScene(const std::string& mapNodeId);
    void closeDialog();
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    void refreshRows();
    bool applySelected();
    bool selectedIsAcceptable() const;
    Rectangle aboveBelowSwitchTrack{0, 0, 0, 0};
};

} // namespace timberline_editor

#endif
