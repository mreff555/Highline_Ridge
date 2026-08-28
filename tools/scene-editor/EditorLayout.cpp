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

#include "EditorLayout.h"

namespace timberline_editor
{

float EditorLayout::contentHeight(int screenHeight) const
{
    const float height = static_cast<float>(screenHeight) - kStatusBarHeight;
    return height > 1.0f ? height : 1.0f;
}

void EditorLayout::applyDefaultTopSplit(int screenHeight)
{
    // Upper browser + canvas occupy 2/3 of the content area (above the status bar).
    topAreaHeight = contentHeight(screenHeight) * kTopAreaRatio;
}

void EditorLayout::init(int screenWidth, int screenHeight)
{
    leftPaneWidth = static_cast<float>(screenWidth) * kLeftPaneRatio;
    applyDefaultTopSplit(screenHeight);
    userResizedLeftSplit = false;
    userResizedTopSplit = false;
    lastScreenWidth = screenWidth;
    lastScreenHeight = screenHeight;
    clamp(screenWidth, screenHeight);
}

void EditorLayout::syncToWindow(int screenWidth, int screenHeight)
{
    if (screenWidth == lastScreenWidth && screenHeight == lastScreenHeight)
        return;

    if (!userResizedTopSplit || lastScreenHeight <= 0)
    {
        applyDefaultTopSplit(screenHeight);
    }
    else
    {
        const float previousContent = contentHeight(lastScreenHeight);
        const float ratio = previousContent > 1.0f ? (topAreaHeight / previousContent) : kTopAreaRatio;
        topAreaHeight = contentHeight(screenHeight) * ratio;
    }

    if (!userResizedLeftSplit || lastScreenWidth <= 0)
    {
        leftPaneWidth = static_cast<float>(screenWidth) * kLeftPaneRatio;
    }
    else
    {
        leftPaneWidth *= static_cast<float>(screenWidth) / static_cast<float>(lastScreenWidth);
    }

    lastScreenWidth = screenWidth;
    lastScreenHeight = screenHeight;
    clamp(screenWidth, screenHeight);
}

void EditorLayout::clamp(int screenWidth, int screenHeight)
{
    const float maxLeft =
        static_cast<float>(screenWidth) - kMinMainWidth - kDividerSize;
    if (leftPaneWidth < kMinLeftWidth)
        leftPaneWidth = kMinLeftWidth;
    if (leftPaneWidth > maxLeft)
        leftPaneWidth = maxLeft;

    const float contentH = contentHeight(screenHeight);
    const float maxTop = contentH - kMinBottomHeight - kDividerSize;
    if (topAreaHeight < kMinTopHeight)
        topAreaHeight = kMinTopHeight;
    if (topAreaHeight > maxTop)
        topAreaHeight = maxTop;
    if (topAreaHeight < 1.0f)
        topAreaHeight = contentH * kTopAreaRatio;
}

Rectangle EditorLayout::expandHitRect(Rectangle bounds, float pad, bool vertical)
{
    if (vertical)
    {
        return {
            bounds.x - pad,
            bounds.y,
            bounds.width + pad * 2.0f,
            bounds.height};
    }

    return {
        bounds.x,
        bounds.y - pad,
        bounds.width,
        bounds.height + pad * 2.0f};
}

bool EditorLayout::isDraggingDivider() const
{
    return draggingVerticalDivider || draggingHorizontalDivider;
}

Rectangle EditorLayout::topAreaBounds(int screenWidth) const
{
    return {0.0f, 0.0f, static_cast<float>(screenWidth), topAreaHeight};
}

Rectangle EditorLayout::leftPaneBounds(int screenWidth) const
{
    const Rectangle top = topAreaBounds(screenWidth);
    return {top.x, top.y, leftPaneWidth, top.height};
}

Rectangle EditorLayout::mainPaneBounds(int screenWidth) const
{
    const Rectangle top = topAreaBounds(screenWidth);
    return {top.x + leftPaneWidth + kDividerSize, top.y,
            top.width - leftPaneWidth - kDividerSize, top.height};
}

Rectangle EditorLayout::bottomPaneBounds(int screenWidth, int screenHeight) const
{
    const float contentH = contentHeight(screenHeight);
    return {
        0.0f,
        topAreaHeight + kDividerSize,
        static_cast<float>(screenWidth),
        contentH - topAreaHeight - kDividerSize};
}

Rectangle EditorLayout::verticalDividerBounds(int screenWidth) const
{
    const Rectangle top = topAreaBounds(screenWidth);
    return {leftPaneWidth, top.y, kDividerSize, top.height};
}

Rectangle EditorLayout::horizontalDividerBounds(int screenWidth) const
{
    return {0.0f, topAreaHeight, static_cast<float>(screenWidth), kDividerSize};
}

void EditorLayout::handleDividerInput(int screenWidth, int screenHeight)
{
    const Rectangle vDiv = verticalDividerBounds(screenWidth);
    const Rectangle hDiv = horizontalDividerBounds(screenWidth);
    const Rectangle vHit = expandHitRect(vDiv, kDividerHitPadding, true);
    const Rectangle hHit = expandHitRect(hDiv, kDividerHitPadding, false);
    const Vector2 mouse = GetMousePosition();

    const bool overVertical = CheckCollisionPointRec(mouse, vHit);
    const bool overHorizontal = CheckCollisionPointRec(mouse, hHit);

    if (draggingVerticalDivider || overVertical)
        SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
    else if (draggingHorizontalDivider || overHorizontal)
        SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
    else
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        // Prefer the split under the cursor; vertical first if both overlap.
        if (overVertical)
            draggingVerticalDivider = true;
        else if (overHorizontal)
            draggingHorizontalDivider = true;
    }

    if (draggingVerticalDivider && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        leftPaneWidth = mouse.x - vDiv.width * 0.5f;
        userResizedLeftSplit = true;
    }

    if (draggingHorizontalDivider && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        topAreaHeight = mouse.y - hDiv.height * 0.5f;
        userResizedTopSplit = true;
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        cancelDividerDrag();

    clamp(screenWidth, screenHeight);
}

void EditorLayout::cancelDividerDrag()
{
    draggingVerticalDivider = false;
    draggingHorizontalDivider = false;
}

} // namespace timberline_editor
