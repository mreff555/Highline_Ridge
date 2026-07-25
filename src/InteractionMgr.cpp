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

#include <InteractionMgr.h>
#include <SidePanel.h>

#include <algorithm>

namespace timberline_engine
{

namespace
{
    const Color kPanelFill = {28, 26, 34, 255};
    const Color kPanelBorder = {168, 138, 72, 255};
    const Color kPanelAccent = {96, 78, 48, 255};
    const Color kSectionLabel = {132, 122, 104, 255};
    const Color kOptionFill = {40, 38, 50, 255};
    const Color kOptionHover = {62, 52, 34, 255};
    const Color kOptionText = {228, 220, 198, 255};
    const Color kCloseHover = {210, 178, 108, 255};
}

const float InteractionMgr::kCloseButtonSize = 28.0f;
const float InteractionMgr::kRowGap = 10.0f;

InteractionMgr::InteractionMgr() = default;

void InteractionMgr::setPanelBounds(Rectangle bounds)
{
    panelBounds = bounds;
}

void InteractionMgr::setFont(Font font)
{
    panelFont = font;
}

void InteractionMgr::setUiBackdrop(const UiBackdrop* backdrop)
{
    uiBackdrop = backdrop;
}

void InteractionMgr::open()
{
    openState = true;
}

void InteractionMgr::close()
{
    openState = false;
}

void InteractionMgr::setAvailableInteractions(const std::vector<SceneInteractionDef>& interactions)
{
    options = interactions;
    optionBounds.clear();
}

std::string InteractionMgr::consumePendingInteractionId()
{
    const std::string id = pendingInteractionId;
    pendingInteractionId.clear();
    return id;
}

float InteractionMgr::getRowHeight() const
{
    return 42.0f;
}

Rectangle InteractionMgr::getCloseButtonBounds() const
{
    return SidePanelChrome::getCloseButtonBounds(panelBounds, kCloseButtonSize);
}

void InteractionMgr::layoutOptionBounds()
{
    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float contentX = panelBounds.x + pad;
    const float contentY = panelBounds.y + pad + headerHeight;
    const float contentW = panelBounds.width - pad * 2.0f;
    const float rowHeight = getRowHeight();

    optionBounds.clear();
    optionBounds.reserve(options.size());

    for (size_t i = 0; i < options.size(); ++i)
    {
        optionBounds.push_back({
            contentX,
            contentY + (float)i * (rowHeight + kRowGap),
            contentW,
            rowHeight
        });
    }
}

void InteractionMgr::handleCloseButtonInput()
{
    const Rectangle closeBounds = getCloseButtonBounds();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), closeBounds))
    {
        close();
    }
}

void InteractionMgr::handleOptionInput()
{
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;

    const Vector2 mousePos = GetMousePosition();
    for (size_t i = 0; i < optionBounds.size() && i < options.size(); ++i)
    {
        if (CheckCollisionPointRec(mousePos, optionBounds[i]))
        {
            pendingInteractionId = options[i].id;
            return;
        }
    }
}

void InteractionMgr::update()
{
    pendingInteractionId.clear();

    if (!openState)
        return;

    layoutOptionBounds();
    handleCloseButtonInput();
    handleOptionInput();
}

void InteractionMgr::drawCloseButton() const
{
    const Rectangle closeBounds = getCloseButtonBounds();
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), closeBounds);
    const Color lineColor = hovered ? kCloseHover : kSectionLabel;

    DrawLineEx(
        { closeBounds.x + 8.0f, closeBounds.y + 8.0f },
        { closeBounds.x + closeBounds.width - 8.0f, closeBounds.y + closeBounds.height - 8.0f },
        2.0f,
        lineColor);
    DrawLineEx(
        { closeBounds.x + closeBounds.width - 8.0f, closeBounds.y + 8.0f },
        { closeBounds.x + 8.0f, closeBounds.y + closeBounds.height - 8.0f },
        2.0f,
        lineColor);
}

void InteractionMgr::drawOptionList() const
{
    const float textPad = 12.0f;
    const float fontSize = 18.0f;

    for (size_t i = 0; i < options.size() && i < optionBounds.size(); ++i)
    {
        const Rectangle row = optionBounds[i];
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
        DrawRectangleRounded(row, 0.16f, 8, hovered ? kOptionHover : kOptionFill);
        DrawTextEx(
            panelFont,
            options[i].label.c_str(),
            { row.x + textPad, row.y + (row.height - fontSize) * 0.5f },
            fontSize,
            1.0f,
            kOptionText);
    }
}

void InteractionMgr::draw() const
{
    if (!openState)
        return;

    const Color panelBorder = (uiBackdrop != nullptr) ? uiBackdrop->panelBorderColor() : kPanelBorder;
    const Color sectionLabel = (uiBackdrop != nullptr) ? uiBackdrop->sectionLabelColor() : kSectionLabel;

    if (uiBackdrop != nullptr)
        uiBackdrop->drawPanel(panelBounds, 0.04f, 10);
    else
        DrawRectangleRounded(panelBounds, 0.04f, 10, kPanelFill);

    DrawRectangleLinesEx(panelBounds, 3.0f, panelBorder);

    Rectangle accentBar = {
        panelBounds.x + 8.0f,
        panelBounds.y + 8.0f,
        panelBounds.width - 16.0f,
        4.0f
    };
    if (uiBackdrop != nullptr)
        uiBackdrop->drawAccentBar(accentBar);
    else
        DrawRectangleRounded(accentBar, 1.0f, 4, kPanelAccent);

    const float pad = 14.0f;
    DrawTextEx(
        panelFont,
        "INTERACT",
        { panelBounds.x + pad, panelBounds.y + pad },
        17.0f,
        1.0f,
        sectionLabel);

    drawCloseButton();
    drawOptionList();
}

}