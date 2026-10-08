/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations bottom-center pane: details for the selected flowchart node.
 ******************************************************************************/

#ifndef TIMBERLINE_DIALOG_NODE_DETAILS_H
#define TIMBERLINE_DIALOG_NODE_DETAILS_H

#include "DialogFlowCanvas.h"

#include <raylib.h>

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
    Font uiFont{};
    Font uiFontBold{};
    float scrollY = 0.0f;
    int lastSelectedId = -2; // force reset on first draw

    void handleInput(Rectangle bounds, bool allowInteraction);
    void draw(Rectangle bounds);
};

} // namespace timberline_editor

#endif
