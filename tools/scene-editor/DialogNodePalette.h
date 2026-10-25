/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations bottom-left palette: dialog node type stubs (Phase 1).
 ******************************************************************************/

#ifndef TIMBERLINE_DIALOG_NODE_PALETTE_H
#define TIMBERLINE_DIALOG_NODE_PALETTE_H

#include "DialogFlowCanvas.h"
#include "DialogFlowTypes.h"

#include <raylib.h>

namespace timberline_editor
{

struct DialogNodePalette
{
    DialogFlowCanvas* flow = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    /** Vertical scroll when the pane is shorter than the icon grid (#63). */
    float scrollY = 0.0f;
    bool draggingScroll = false;

    void handleInput(Rectangle bounds, bool allowInteraction);
    void draw(Rectangle bounds);

private:
    float iconSizeForBounds(Rectangle bounds) const;
};

} // namespace timberline_editor

#endif
