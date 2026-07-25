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

#include "ScrollPanel.h"

#include <RaylibCompat.h>
#include <algorithm>

namespace timberline_engine
{

const float ScrollPanel::kDefaultScrollbarWidth = 16.0f;

namespace
{
    const Color kScrollTrack = {206, 186, 148, 255};
    const Color kScrollThumb = {176, 148, 108, 255};
    const Color kScrollThumbHover = {148, 118, 78, 255};
}

ScrollPanel::ScrollPanel()
{
}

void ScrollPanel::setContentHeight(float height)
{
    contentHeight = height;
}

void ScrollPanel::setVisibleArea(Rectangle area)
{
    visibleArea = area;
}

void ScrollPanel::setScrollTrack(Rectangle track)
{
    scrollTrack = track;
    hasScrollTrack = true;
}

void ScrollPanel::setWheelStep(float step)
{
    wheelStep = step;
}

void ScrollPanel::setWheelMultiplier(float multiplier)
{
    wheelMultiplier = multiplier;
}

void ScrollPanel::setWheelEnabled(bool enabled)
{
    wheelEnabled = enabled;
}

void ScrollPanel::setInputBlocked(bool blocked)
{
    inputBlocked = blocked;
}

void ScrollPanel::setClampToBottom(bool clamp)
{
    clampToBottom = clamp;
}

float ScrollPanel::getMaxScroll() const
{
    const float visibleHeight = visibleArea.height > 0.0f
        ? visibleArea.height
        : (hasScrollTrack ? scrollTrack.height : 0.0f);
    return std::max(0.0f, contentHeight - visibleHeight);
}

void ScrollPanel::setScrollY(float y)
{
    scrollY = std::max(0.0f, std::min(y, getMaxScroll()));
}

void ScrollPanel::resetScroll()
{
    scrollY = 0.0f;
    scrollbarDragging = false;
    scrollbarDragOffsetY = 0.0f;
}

void ScrollPanel::applyWheelScroll(
    float& scrollY,
    const Rectangle& wheelArea,
    float contentHeight,
    float visibleHeight,
    float wheelStep)
{
    const float maxScroll = std::max(0.0f, contentHeight - visibleHeight);
    const Vector2 mouse = GetMousePosition();

    if (CheckCollisionPointRec(mouse, wheelArea))
        scrollY -= GetMouseWheelMove() * wheelStep;

    scrollY = std::max(0.0f, std::min(scrollY, maxScroll));
}

void ScrollPanel::handleInput()
{
    const float visibleHeight = visibleArea.height > 0.0f
        ? visibleArea.height
        : scrollTrack.height;
    const float maxScroll = std::max(0.0f, contentHeight - visibleHeight);
    const Vector2 mousePos = GetMousePosition();

    if (hasScrollTrack)
    {
        const float thumbHeight = (contentHeight <= 0.0f)
            ? visibleHeight
            : std::max(24.0f, visibleHeight * (visibleHeight / contentHeight));
        const float thumbTravel = std::max(0.0f, visibleHeight - thumbHeight);
        const float thumbY = scrollTrack.y + (maxScroll > 0.0f
            ? (scrollY / maxScroll) * thumbTravel
            : 0.0f);

        const Rectangle scrollThumb = {
            scrollTrack.x + 2.0f,
            thumbY,
            scrollTrack.width - 4.0f,
            thumbHeight
        };

        if (!inputBlocked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (CheckCollisionPointRec(mousePos, scrollThumb))
            {
                scrollbarDragging = true;
                scrollbarDragOffsetY = mousePos.y - scrollThumb.y;
            }
            else if (CheckCollisionPointRec(mousePos, scrollTrack) && thumbTravel > 0.0f)
            {
                scrollY = ((mousePos.y - scrollTrack.y - thumbHeight * 0.5f) / thumbTravel) * maxScroll;
            }
        }

        if (scrollbarDragging)
        {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && thumbTravel > 0.0f)
                scrollY = ((mousePos.y - scrollTrack.y - scrollbarDragOffsetY) / thumbTravel) * maxScroll;
            else
                scrollbarDragging = false;
        }
    }

    if (wheelEnabled && !inputBlocked && visibleArea.width > 0.0f && visibleArea.height > 0.0f)
    {
        if (CheckCollisionPointRec(mousePos, visibleArea))
            scrollY -= GetMouseWheelMove() * wheelStep * wheelMultiplier;
    }

    scrollY = std::max(0.0f, std::min(scrollY, maxScroll));

    if (clampToBottom && contentHeight > visibleHeight && scrollY >= maxScroll - 1.0f)
        scrollY = maxScroll;
}

void ScrollPanel::drawScrollbar() const
{
    if (!hasScrollTrack)
        return;

    const float visibleHeight = scrollTrack.height;
    const float maxScroll = std::max(0.0f, contentHeight - visibleHeight);

    DrawRectangleRec(scrollTrack, kScrollTrack);

    if (maxScroll <= 0.0f)
        return;

    const float thumbHeight = std::max(24.0f, visibleHeight * (visibleHeight / contentHeight));
    const float thumbTravel = std::max(0.0f, visibleHeight - thumbHeight);
    const float thumbY = scrollTrack.y + (scrollY / maxScroll) * thumbTravel;

    const Rectangle scrollThumb = {
        scrollTrack.x + 2.0f,
        thumbY,
        scrollTrack.width - 4.0f,
        thumbHeight
    };

    const bool thumbHovered = CheckCollisionPointRec(GetMousePosition(), scrollThumb);
    DrawRectangleRounded(scrollThumb, 0.4f, 6, thumbHovered ? kScrollThumbHover : kScrollThumb);
}

}