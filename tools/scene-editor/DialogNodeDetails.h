/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations bottom-center pane: details for the selected flowchart node.
 ******************************************************************************/

#ifndef TIMBERLINE_DIALOG_NODE_DETAILS_H
#define TIMBERLINE_DIALOG_NODE_DETAILS_H

#include "DialogFlowCanvas.h"

#include <functional>
#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_editor
{

/** Bottom-pane column ratios for Conversations (types | details | media). */
struct DialogBottomSplit
{
    Rectangle types{};
    Rectangle details{};
    Rectangle media{};
};

/** Split the conversations bottom strip into three panes (gap between). */
DialogBottomSplit computeDialogBottomSplit(Rectangle bottomBounds);

struct DialogNodeDetails
{
    DialogFlowCanvas* flow = nullptr;
    std::function<void(DialogFlowNode*)> onEditFight;
    std::function<void(DialogFlowNode*)> onEditActorDialog;
    std::function<void(DialogFlowNode*)> onEditEvent;
    Font uiFont{};
    Font uiFontBold{};
    float scrollY = 0.0f;
    int lastSelectedId = -2; // force reset on first draw

    void handleInput(Rectangle bounds, bool allowInteraction);
    void draw(Rectangle bounds);

private:
    enum class HitKind
    {
        None,
        OpenText,
        OpenTts,
        OpenPlayerLine,
        AddPlayerResponse,
        RemovePlayerResponse,
        OpenEditFight,
        OpenEditActorDialog,
        OpenEditEvent
    };

    struct HitTarget
    {
        HitKind kind = HitKind::None;
        Rectangle rect{};
        int nodeId = 0; // field owner or player node for remove/open
    };

    std::vector<HitTarget> hitTargets;
    Rectangle lastClip{0, 0, 0, 0};

    void rebuildAndDraw(Rectangle bounds);
    void clearHits();
    void addHit(HitKind kind, Rectangle rect, int nodeId);
};

} // namespace timberline_editor

#endif
