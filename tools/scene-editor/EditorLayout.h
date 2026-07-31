/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#ifndef TIMBERLINE_EDITOR_LAYOUT_H
#define TIMBERLINE_EDITOR_LAYOUT_H

#include "EditorTheme.h"

#include <raylib.h>

namespace timberline_editor
{

// Pane splits and divider interaction for the resource editor chrome.
struct EditorLayout
{
    float leftPaneWidth = 0.0f;
    float topAreaHeight = 0.0f;
    bool userResizedLeftSplit = false;
    bool userResizedTopSplit = false;
    int lastScreenWidth = 0;
    int lastScreenHeight = 0;
    bool draggingVerticalDivider = false;
    bool draggingHorizontalDivider = false;

    float contentHeight(int screenHeight) const;
    void applyDefaultTopSplit(int screenHeight);
    void init(int screenWidth, int screenHeight);
    void syncToWindow(int screenWidth, int screenHeight);
    void clamp(int screenWidth, int screenHeight);

    static Rectangle expandHitRect(Rectangle bounds, float pad, bool vertical);

    bool isDraggingDivider() const;

    Rectangle topAreaBounds(int screenWidth) const;
    Rectangle leftPaneBounds(int screenWidth) const;
    Rectangle mainPaneBounds(int screenWidth) const;
    Rectangle bottomPaneBounds(int screenWidth, int screenHeight) const;
    Rectangle verticalDividerBounds(int screenWidth) const;
    Rectangle horizontalDividerBounds(int screenWidth) const;

    void handleDividerInput(int screenWidth, int screenHeight);
    void cancelDividerDrag();
};

} // namespace timberline_editor

#endif /* TIMBERLINE_EDITOR_LAYOUT_H */
