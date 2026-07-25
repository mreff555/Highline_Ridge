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

#include "DropConfirmMgr.h"
#include <RaylibCompat.h>
#include <cstdio>

namespace timberline_engine
{

namespace
{
    const Color kPanelFill = {28, 26, 34, 255};
    const Color kPanelBorder = {168, 138, 72, 255};
    const Color kPanelAccent = {96, 78, 48, 255};
    const Color kBodyText = {228, 220, 198, 255};
    const Color kMutedText = {132, 122, 104, 255};
    const Color kOverlayDim = {8, 8, 12, 210};
    const Color kCheckboxFill = {40, 38, 50, 255};
    const Color kCheckboxBorder = {168, 138, 72, 255};
}

DropConfirmMgr::DropConfirmMgr(int screenWidth_, int screenHeight_, Font uiFont_)
    : screenWidth(screenWidth_),
      screenHeight(screenHeight_),
      uiFont(uiFont_),
      modalPanel(screenWidth_, screenHeight_),
      baseButtonStyle{
          {228, 220, 198, 255},
          {54, 50, 64, 255},
          {78, 72, 92, 255},
          {40, 38, 50, 255},
          {30, 28, 36, 255},
          {118, 96, 58, 255},
          {62, 52, 34, 255},
          {108, 102, 92, 255},
          0.18f,
          18.0f
      },
      buttonStyle{},
      yesButton("Yes", {0.0f, 0.0f}, {120.0f, 40.0f}, uiFont_, buttonStyle),
      noButton("No", {0.0f, 0.0f}, {120.0f, 40.0f}, uiFont_, buttonStyle)
{
    buttonStyle = baseButtonStyle;
    layoutButtons();
}

void DropConfirmMgr::setUiBackdrop(const UiBackdrop* backdrop)
{
    uiBackdrop = backdrop;
    buttonStyle = (uiBackdrop != nullptr)
        ? uiBackdrop->contrastedButtonStyle(baseButtonStyle)
        : baseButtonStyle;
    layoutButtons();
}

void DropConfirmMgr::setScreenSize(int width, int height)
{
    screenWidth = width;
    screenHeight = height;
    modalPanel.setScreenSize(width, height);
    if (open)
        layoutButtons();
}

Rectangle DropConfirmMgr::getPanelBounds() const
{
    const float panelWidth = 500.0f;
    const float panelHeight = 250.0f;
    return {
        ((float)screenWidth - panelWidth) * 0.5f,
        ((float)screenHeight - panelHeight) * 0.5f,
        panelWidth,
        panelHeight
    };
}

void DropConfirmMgr::layoutButtons()
{
    const Rectangle panel = getPanelBounds();
    const float buttonWidth = 120.0f;
    const float buttonHeight = 40.0f;
    const float buttonY = panel.y + panel.height - 58.0f;
    const float gap = 24.0f;
    const float totalWidth = buttonWidth * 2.0f + gap;
    const float startX = panel.x + (panel.width - totalWidth) * 0.5f;

    yesButton = Button("Yes", { startX, buttonY }, { buttonWidth, buttonHeight }, uiFont, buttonStyle);
    noButton = Button("No", { startX + buttonWidth + gap, buttonY }, { buttonWidth, buttonHeight }, uiFont, buttonStyle);
}

Rectangle DropConfirmMgr::getCheckboxBounds() const
{
    const Rectangle panel = getPanelBounds();
    return {
        panel.x + 42.0f,
        panel.y + panel.height - 104.0f,
        18.0f,
        18.0f
    };
}

void DropConfirmMgr::openForItem(const std::string& itemId, const std::string& itemName)
{
    pendingItemId = itemId;
    pendingItemName = itemName;
    dontShowAgainChecked = false;
    confirmedItemId.clear();
    cancelled = false;
    dontShowAgainRequested = false;
    open = true;
    layoutButtons();
}

void DropConfirmMgr::close()
{
    open = false;
    pendingItemId.clear();
    pendingItemName.clear();
    dontShowAgainChecked = false;
}

std::string DropConfirmMgr::consumeConfirmedItemId()
{
    const std::string itemId = confirmedItemId;
    confirmedItemId.clear();
    return itemId;
}

bool DropConfirmMgr::consumeCancelled()
{
    if (!cancelled)
        return false;

    cancelled = false;
    return true;
}

bool DropConfirmMgr::consumeDontShowAgainRequest()
{
    if (!dontShowAgainRequested)
        return false;

    dontShowAgainRequested = false;
    return true;
}

void DropConfirmMgr::handleCheckboxInput()
{
    const Rectangle checkbox = getCheckboxBounds();
    const Rectangle labelBounds = {
        checkbox.x + checkbox.width + 10.0f,
        checkbox.y - 2.0f,
        280.0f,
        24.0f
    };

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;

    const Vector2 mousePos = GetMousePosition();
    if (CheckCollisionPointRec(mousePos, checkbox) ||
        CheckCollisionPointRec(mousePos, labelBounds))
    {
        dontShowAgainChecked = !dontShowAgainChecked;
    }
}

void DropConfirmMgr::update()
{
    if (!open)
        return;

    handleCheckboxInput();

    if (yesButton.isClicked())
    {
        if (dontShowAgainChecked)
            dontShowAgainRequested = true;
        confirmedItemId = pendingItemId;
        close();
    }
    else if (noButton.isClicked())
    {
        cancelled = true;
        close();
    }
}

void DropConfirmMgr::drawPanelFrame() const
{
    const Rectangle panel = getPanelBounds();

    modalPanel.drawOverlay();

    const Color panelBorder = (uiBackdrop != nullptr) ? uiBackdrop->panelBorderColor() : kPanelBorder;
    if (uiBackdrop != nullptr)
        uiBackdrop->drawPanel(panel, 0.05f, 12);
    else
        DrawRectangleRounded(panel, 0.05f, 12, kPanelFill);
    DrawRoundedBorder(panel, 0.05f, 12, 3.0f, panelBorder);

    const Rectangle accentBar = {
        panel.x + 12.0f,
        panel.y + 12.0f,
        panel.width - 24.0f,
        4.0f
    };
    if (uiBackdrop != nullptr)
        uiBackdrop->drawAccentBar(accentBar);
    else
        DrawRectangleRounded(accentBar, 1.0f, 4, kPanelAccent);

    DrawTextEx(
        uiFont,
        "DROP ITEM",
        { panel.x + 34.0f, panel.y + 30.0f },
        22.0f,
        1.0f,
        kBodyText);

    const char* prompt = "Do you wish to drop this item?";
    DrawTextEx(
        uiFont,
        prompt,
        { panel.x + 34.0f, panel.y + 72.0f },
        18.0f,
        1.0f,
        kBodyText);

    if (!pendingItemName.empty())
    {
        char itemLine[128];
        std::snprintf(itemLine, sizeof(itemLine), "(%s)", pendingItemName.c_str());
        DrawTextEx(
            uiFont,
            itemLine,
            { panel.x + 34.0f, panel.y + 98.0f },
            17.0f,
            1.0f,
            kMutedText);
    }
}

void DropConfirmMgr::drawCheckbox() const
{
    const Rectangle checkbox = getCheckboxBounds();
    DrawRectangleRounded(checkbox, 0.2f, 4, kCheckboxFill);
    DrawRoundedBorder(checkbox, 0.2f, 4, 1.5f, kCheckboxBorder);

    if (dontShowAgainChecked)
    {
        DrawLineEx(
            { checkbox.x + 4.0f, checkbox.y + 10.0f },
            { checkbox.x + 8.0f, checkbox.y + 14.0f },
            2.0f,
            kBodyText);
        DrawLineEx(
            { checkbox.x + 8.0f, checkbox.y + 14.0f },
            { checkbox.x + 15.0f, checkbox.y + 5.0f },
            2.0f,
            kBodyText);
    }

    DrawTextEx(
        uiFont,
        "Don't show this again",
        { checkbox.x + checkbox.width + 10.0f, checkbox.y - 1.0f },
        16.0f,
        1.0f,
        kMutedText);
}

void DropConfirmMgr::draw() const
{
    if (!open)
        return;

    drawPanelFrame();
    drawCheckbox();
    yesButton.draw();
    noButton.draw();
}

}