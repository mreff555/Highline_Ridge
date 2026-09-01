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

#include "Button.h"
#include "ButtonMgr.h"
#include <RaylibCompat.h>
#include <SceneLoader.h>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace timberline_engine
{

namespace
{
    const Color kPanelFill = {28, 26, 34, 255};
    const Color kPanelBorder = {168, 138, 72, 255};
    const Color kPanelAccent = {96, 78, 48, 255};
    const Color kSectionLabel = {132, 122, 104, 255};
    const Color kStatusTrack = {40, 38, 50, 255};
    const Color kHealthFill = {168, 72, 72, 255};
    const Color kEnergyFill = {168, 138, 72, 255};
    const Color kResolveFill = {88, 118, 168, 255};
    const Color kLucidityFill = {118, 88, 168, 255};
    const Color kCharismaFill = {168, 108, 88, 255};

    Color statusBarFillColor(const char* label)
    {
        switch (label[0])
        {
            case 'H': return kHealthFill;
            case 'E': return kEnergyFill;
            case 'R': return kResolveFill;
            case 'L': return kLucidityFill;
            case 'C': return kCharismaFill;
            default: return kEnergyFill;
        }
    }

}

ButtonMgr::ButtonMgr(Rectangle _buttonBox, Font _buttonFont, Font _boldButtonFont)
    : buttonBox(_buttonBox),
      healthBarBounds{},
      energyBarBounds{},
      resolveBarBounds{},
      lucidityBarBounds{},
      charismaBarBounds{},
      reservedBarBounds{},
      buttonFont(_buttonFont),
      boldButtonFont(_boldButtonFont.texture.id != 0 ? _boldButtonFont : _buttonFont),
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
          19.0f
      },
      buttonStyle{}
{
    buttonStyle = baseButtonStyle;
    buildButtonLayout();
}

void ButtonMgr::buildButtonLayout()
{
    buttons.clear();

    const float pad = 14.0f;
    const float gap = 8.0f;
    const float inventoryHeight = 48.0f;
    // Compact horizontal status strip (per-stat label + thin bar).
    // Labels are 14pt (+1 from prior compact pass); reserve matching strip height.
    const float statusLabelH = 15.0f;
    const float statusBarH = 14.0f;
    const float statusBlockH = statusLabelH + statusBarH + 4.0f;
    const float sectionLabelH = 18.0f;
    const float statusToBarsGap = 2.0f;      // STATUS caption → bar labels
    const float statusToMoveGap = 3.0f;      // status bars → MOVE/ACTIONS captions

    const float contentX = buttonBox.x + pad;
    const float contentW = buttonBox.width - pad * 2.0f;

    // STATUS — full width row above MOVE/ACTIONS (below a small section caption).
    const float statusY = buttonBox.y + pad + sectionLabelH + statusToBarsGap;
    const float statusGap = 6.0f;
    const int statusCount = 5;
    const float statusBarW =
        (contentW - statusGap * static_cast<float>(statusCount - 1)) / static_cast<float>(statusCount);
    healthBarBounds = { contentX, statusY, statusBarW, statusBlockH };
    energyBarBounds = { contentX + (statusBarW + statusGap) * 1.0f, statusY, statusBarW, statusBlockH };
    resolveBarBounds = { contentX + (statusBarW + statusGap) * 2.0f, statusY, statusBarW, statusBlockH };
    lucidityBarBounds = { contentX + (statusBarW + statusGap) * 3.0f, statusY, statusBarW, statusBlockH };
    charismaBarBounds = { contentX + (statusBarW + statusGap) * 4.0f, statusY, statusBarW, statusBlockH };
    reservedBarBounds = { 0, 0, 0, 0 };

    const float controlsTop = statusY + statusBlockH + statusToMoveGap + sectionLabelH;
    const float inventoryY = buttonBox.y + buttonBox.height - pad - inventoryHeight;
    const float controlsBottom = inventoryY - gap;
    const float contentH = std::max(72.0f, controlsBottom - controlsTop);

    const float moveW = contentW * 0.48f;
    const float actionW = contentW - moveW - gap;
    const float actionX = contentX + moveW + gap;
    const float actionCols = 2.0f;
    const float actionRows = 3.0f;
    const float actionBtnW = (actionW - gap) / actionCols;
    const float actionBtnH = (contentH - gap * (actionRows - 1.0f)) / actionRows;

    const float moveColLeftW = moveW * 0.32f;
    const float moveColRightW = moveW - moveColLeftW - gap;
    const float moveRightX = contentX + moveColLeftW + gap;
    const float verticalBtnH = (contentH - gap) / 2.0f;
    const float movePadBtnW = (moveColRightW - gap) / 2.0f;
    const float movePadBtnH = (contentH - gap * 2.0f) / 3.0f;
    const float contentY = controlsTop;

    addButton("Up",
        { contentX, contentY, moveColLeftW, verticalBtnH });

    addButton("Down",
        { contentX, contentY + verticalBtnH + gap, moveColLeftW, verticalBtnH });

    addButton("Forward",
        { moveRightX, contentY, moveColRightW, movePadBtnH });

    addButton("Left",
        { moveRightX, contentY + movePadBtnH + gap, movePadBtnW, movePadBtnH });

    addButton("Right",
        { moveRightX + movePadBtnW + gap, contentY + movePadBtnH + gap, movePadBtnW, movePadBtnH });

    addButton("Back",
        { moveRightX, contentY + (movePadBtnH + gap) * 2.0f, moveColRightW, movePadBtnH });

    addButton("Examine",
        { actionX, contentY, actionBtnW, actionBtnH });

    addButton("Speak",
        { actionX + actionBtnW + gap, contentY, actionBtnW, actionBtnH });

    addButton("Take",
        { actionX, contentY + (actionBtnH + gap) * 1.0f, actionBtnW, actionBtnH });

    addButton("Use",
        { actionX + actionBtnW + gap, contentY + (actionBtnH + gap) * 1.0f, actionBtnW, actionBtnH });

    addButton("Attack",
        { actionX, contentY + (actionBtnH + gap) * 2.0f, actionW, actionBtnH });

    addButton("Inventory",
        { contentX, inventoryY, contentW, inventoryHeight });

    if (uiBackdrop != nullptr)
        refreshButtonStyles();
}

void ButtonMgr::setUiBackdrop(const UiBackdrop* backdrop)
{
    uiBackdrop = backdrop;
    refreshButtonStyles();
}

void ButtonMgr::refreshButtonStyles()
{
    buttonStyle = (uiBackdrop != nullptr)
        ? uiBackdrop->contrastedButtonStyle(baseButtonStyle)
        : baseButtonStyle;

    for (auto& button : buttons)
        button.setStyle(buttonStyle);
}

void ButtonMgr::relayout(Rectangle newButtonBox)
{
    buttonBox = newButtonBox;
    buildButtonLayout();
}

void ButtonMgr::setClickHoldDuration(float seconds)
{
    // Floor at 0 so authors can disable intentional click-hold lag if desired.
    clickHoldDurationSeconds = std::max(0.0f, std::min(seconds, 0.5f));
}

ButtonMgr::~ButtonMgr()
{
    if (needsLightIcon.id != 0)
        UnloadTexture(needsLightIcon);
    if (needsGearIcon.id != 0)
        UnloadTexture(needsGearIcon);
    if (needsLockIcon.id != 0)
        UnloadTexture(needsLockIcon);
}

void ButtonMgr::setMovementBlockOverlays(const MovementBlockOverlays& overlays)
{
    movementBlockOverlays = overlays;
}

void ButtonMgr::ensureBlockIconsLoaded() const
{
    if (blockIconsLoaded)
        return;
    blockIconsLoaded = true;

    Texture2D lightTex{};
    if (loadResourceTexture(".", "resources/ui/exit_needs_light_icon.png", lightTex))
        needsLightIcon = lightTex;
    else
        TraceLog(LOG_WARNING, "Failed to load movement block icon resources/ui/exit_needs_light_icon.png");

    Texture2D gearTex{};
    if (loadResourceTexture(".", "resources/ui/exit_needs_gear_icon.png", gearTex))
        needsGearIcon = gearTex;
    else
        TraceLog(LOG_WARNING, "Failed to load movement block icon resources/ui/exit_needs_gear_icon.png");

    Texture2D lockTex{};
    if (loadResourceTexture(".", "resources/ui/exit_needs_lock_icon.png", lockTex))
        needsLockIcon = lockTex;
    else
        TraceLog(LOG_WARNING, "Failed to load movement block icon resources/ui/exit_needs_lock_icon.png");
}

const Texture2D* ButtonMgr::iconForBlockReason(MovementBlockReason reason) const
{
    ensureBlockIconsLoaded();
    switch (reason)
    {
        case MovementBlockReason::NeedsLight:
            return needsLightIcon.id != 0 ? &needsLightIcon : nullptr;
        case MovementBlockReason::NeedsGear:
            return needsGearIcon.id != 0 ? &needsGearIcon : nullptr;
        case MovementBlockReason::NeedsLock:
            return needsLockIcon.id != 0 ? &needsLockIcon : nullptr;
        case MovementBlockReason::None:
        case MovementBlockReason::Other:
        default:
            return nullptr;
    }
}

MovementBlockReason ButtonMgr::blockReasonForMovementIndex(int buttonIndex) const
{
    switch (buttonIndex)
    {
        case 0: return movementBlockOverlays.up;
        case 1: return movementBlockOverlays.down;
        case 2: return movementBlockOverlays.forward;
        case 3: return movementBlockOverlays.left;
        case 4: return movementBlockOverlays.right;
        case 5: return movementBlockOverlays.backward;
        default: return MovementBlockReason::None;
    }
}

bool ButtonMgr::movementIndexHasBlockIcon(int buttonIndex) const
{
    return iconForBlockReason(blockReasonForMovementIndex(buttonIndex)) != nullptr;
}

void ButtonMgr::drawDisabledMovementChrome(Rectangle bounds) const
{
    DrawRectangleRounded(bounds, buttonStyle.roundness, 8, buttonStyle.disabledBg);
    DrawRoundedBorder(bounds, buttonStyle.roundness, 8, 2.0f, buttonStyle.disabledBorderColor);
}

void ButtonMgr::drawMovementBlockBadge(int buttonIndex, Rectangle bounds) const
{
    const MovementBlockReason reason = blockReasonForMovementIndex(buttonIndex);
    const Texture2D* icon = iconForBlockReason(reason);
    if (icon == nullptr)
        return;

    // Quiet centered outline glyph, tinted to match disabled button text.
    const float badge = std::min(bounds.width, bounds.height) * 0.55f;
    const Rectangle dest = {
        bounds.x + (bounds.width - badge) * 0.5f,
        bounds.y + (bounds.height - badge) * 0.5f,
        badge,
        badge
    };
    const Rectangle src = {
        0.0f,
        0.0f,
        static_cast<float>(icon->width),
        static_cast<float>(icon->height)
    };
    DrawTexturePro(*icon, src, dest, {0.0f, 0.0f}, 0.0f, buttonStyle.disabledTextColor);
}

void ButtonMgr::setStatus(float health, float energy, float resolve, float lucidity, float charisma)
{
    healthPercent = std::max(0.0f, std::min(health, 100.0f));
    energyPercent = std::max(0.0f, std::min(energy, 100.0f));
    resolvePercent = std::max(0.0f, std::min(resolve, 100.0f));
    lucidityPercent = std::max(0.0f, std::min(lucidity, 100.0f));
    charismaPercent = std::max(0.0f, std::min(charisma, 100.0f));
}

void ButtonMgr::addButton(const char* label, Rectangle bounds)
{
    buttons.push_back(Button(
        label,
        { bounds.x, bounds.y },
        { bounds.width, bounds.height },
        buttonFont,
        buttonStyle));
}

void ButtonMgr::drawSectionLabel(const char* label, float x, float y) const
{
    DrawTextEx(buttonFont, label, { x, y }, 17.0f, 1, kSectionLabel);
}

void ButtonMgr::drawStatusBar(const char* label, Rectangle bounds, float percent) const
{
    // Compact strip: short label above a thin track (fits a 5-across row).
    const float labelFontSize = 14.0f; // one point larger than prior compact pass
    const float labelHeight = 15.0f;
    const float barTop = bounds.y + labelHeight;
    const float barHeight = std::max(10.0f, bounds.height - labelHeight - 2.0f);
    const Rectangle track = { bounds.x, barTop, bounds.width, barHeight };
    const float fillWidth = (bounds.width - 4.0f) * (percent / 100.0f);
    const Rectangle fill = {
        bounds.x + 2.0f,
        barTop + 2.0f,
        fillWidth,
        barHeight - 4.0f
    };

    const Color sectionLabel = (uiBackdrop != nullptr)
        ? uiBackdrop->sectionLabelColor()
        : kSectionLabel;
    const Color statusTrack = (uiBackdrop != nullptr)
        ? uiBackdrop->statusTrackColor()
        : kStatusTrack;
    const Color panelBorder = (uiBackdrop != nullptr)
        ? uiBackdrop->panelBorderColor()
        : kPanelBorder;

    DrawTextEx(buttonFont, label, { bounds.x, bounds.y }, labelFontSize, 1, sectionLabel);
    DrawRectangleRounded(track, 0.18f, 8, statusTrack);
    DrawRoundedBorder(track, 0.18f, 8, 1.5f, panelBorder);

    if (fillWidth > 0.0f)
    {
        DrawRectangleRounded(fill, 0.18f, 8, statusBarFillColor(label));
    }

    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%d%%", (int)percent);
    const float percentFontSize = 14.0f;
    const Vector2 textSize = MeasureTextEx(boldButtonFont, percentText, percentFontSize, 1);
    DrawTextEx(
        boldButtonFont,
        percentText,
        { bounds.x + (bounds.width - textSize.x) / 2.0f, barTop + (barHeight - textSize.y) / 2.0f },
        percentFontSize,
        1,
        {228, 220, 198, 255});
}

void ButtonMgr::setAvailability(
    const MovementStruct& movement,
    const ActionStruct& actions,
    bool enableInventory)
{
    buttons[0].setEnabled(movement.up);
    buttons[1].setEnabled(movement.down);
    buttons[2].setEnabled(movement.forward);
    buttons[3].setEnabled(movement.left);
    buttons[4].setEnabled(movement.right);
    buttons[5].setEnabled(movement.backward);
    buttons[6].setEnabled(actions.examine);
    buttons[7].setEnabled(actions.speak);
    buttons[8].setEnabled(actions.take);
    buttons[9].setEnabled(actions.use);
    buttons[10].setEnabled(actions.hit);
    buttons[11].setEnabled(enableInventory);
}

namespace
{
    bool isPointInClickableBounds(Vector2 point, Rectangle bounds, float padding)
    {
        const Rectangle expanded = {
            bounds.x - padding,
            bounds.y - padding,
            bounds.width + padding * 2.0f,
            bounds.height + padding * 2.0f
        };
        return CheckCollisionPointRec(point, expanded);
    }
}

int ButtonMgr::findEnabledButtonUnderMouse(Vector2 mousePos) const
{
    const float clickPadding = 3.0f;
    const int movementAndActionCount = 11;
    for (int i = 0; i < movementAndActionCount; ++i)
    {
        if (buttons[i].isEnabled() &&
            isPointInClickableBounds(mousePos, buttons[i].getBounds(), clickPadding))
            return i;
    }

    if (buttons[11].isEnabled() &&
        isPointInClickableBounds(mousePos, buttons[11].getBounds(), clickPadding))
        return 11;

    return -1;
}

void ButtonMgr::registerButtonClick(int buttonIndex)
{
    if (buttonIndex >= 0 && buttonIndex <= 10)
        moveOrActionButtonClicked = true;

    switch (buttonIndex)
    {
        case 0: upButtonClicked = true; break;
        case 1: downButtonClicked = true; break;
        case 2: forwardButtonClicked = true; break;
        case 3: leftButtonClicked = true; break;
        case 4: rightButtonClicked = true; break;
        case 5: backwardButtonClicked = true; break;
        case 6: examineButtonClicked = true; break;
        case 7: speakButtonClicked = true; break;
        case 8: takeButtonClicked = true; break;
        case 9: useButtonClicked = true; break;
        case 10: hitButtonClicked = true; break;
        case 11: inventoryButtonClicked = true; break;
        default: break;
    }
}

void ButtonMgr::updatePressedFlags()
{
    upButtonPressed = buttons[0].isEnabled() && buttons[0].getState() == PRESSED;
    downButtonPressed = buttons[1].isEnabled() && buttons[1].getState() == PRESSED;
    forwardButtonPressed = buttons[2].isEnabled() && buttons[2].getState() == PRESSED;
    leftButtonPressed = buttons[3].isEnabled() && buttons[3].getState() == PRESSED;
    rightButtonPressed = buttons[4].isEnabled() && buttons[4].getState() == PRESSED;
    backButtonPressed = buttons[5].isEnabled() && buttons[5].getState() == PRESSED;
    examineButtonPressed = buttons[6].isEnabled() && buttons[6].getState() == PRESSED;
    speakButtonPressed = buttons[7].isEnabled() && buttons[7].getState() == PRESSED;
    hitButtonPressed = buttons[10].isEnabled() && buttons[10].getState() == PRESSED;
    useButtonPressed = buttons[9].isEnabled() && buttons[9].getState() == PRESSED;
    inventoryButtonPressed = buttons[11].isEnabled() && buttons[11].getState() == PRESSED;
}

void ButtonMgr::update()
{
    upButtonClicked = false;
    downButtonClicked = false;
    forwardButtonClicked = false;
    backwardButtonClicked = false;
    leftButtonClicked = false;
    rightButtonClicked = false;
    examineButtonClicked = false;
    speakButtonClicked = false;
    useButtonClicked = false;
    takeButtonClicked = false;
    hitButtonClicked = false;
    inventoryButtonClicked = false;
    moveOrActionButtonClicked = false;

    Vector2 mousePos = GetMousePosition();
    const bool mouseDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool mouseJustPressed = mouseDown && !mouseWasDownLastFrame;
    const bool mouseJustReleased = !mouseDown && mouseWasDownLastFrame;

    if (mouseJustPressed)
    {
        activePressButtonIndex = findEnabledButtonUnderMouse(mousePos);
        activePressStartTime = GetTime();
        activePressClickFired = false;
    }

    if (mouseDown &&
        activePressButtonIndex >= 0 &&
        !activePressClickFired)
    {
        if (findEnabledButtonUnderMouse(mousePos) == activePressButtonIndex)
        {
            if (GetTime() - activePressStartTime >= clickHoldDurationSeconds)
            {
                registerButtonClick(activePressButtonIndex);
                activePressClickFired = true;
            }
        }
        else
        {
            activePressButtonIndex = -1;
        }
    }

    if (mouseJustReleased)
    {
        if (activePressButtonIndex >= 0 && !activePressClickFired)
            registerButtonClick(activePressButtonIndex);

        activePressButtonIndex = -1;
        activePressClickFired = false;
    }

    mouseWasDownLastFrame = mouseDown;

    for (auto& button : buttons)
    {
        if (!button.isEnabled())
        {
            button.setState(NORMAL);
            continue;
        }

        if (CheckCollisionPointRec(mousePos, button.getBounds()))
        {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                button.setState(PRESSED);
            else
                button.setState(HOVERED);
        }
        else
        {
            button.setState(NORMAL);
        }
    }

    updatePressedFlags();
}

bool ButtonMgr::consumeExamineButtonClick()
{
    const bool clicked = examineButtonClicked;
    examineButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeSpeakButtonClick()
{
    const bool clicked = speakButtonClicked;
    speakButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeTakeButtonClick()
{
    const bool clicked = takeButtonClicked;
    takeButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeUseButtonClick()
{
    const bool clicked = useButtonClicked;
    useButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeUpButtonClick()
{
    const bool clicked = upButtonClicked;
    upButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeDownButtonClick()
{
    const bool clicked = downButtonClicked;
    downButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeForwardButtonClick()
{
    const bool clicked = forwardButtonClicked;
    forwardButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeBackwardButtonClick()
{
    const bool clicked = backwardButtonClicked;
    backwardButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeLeftButtonClick()
{
    const bool clicked = leftButtonClicked;
    leftButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeRightButtonClick()
{
    const bool clicked = rightButtonClicked;
    rightButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeInventoryButtonClick()
{
    const bool clicked = inventoryButtonClicked;
    inventoryButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeMoveOrActionButtonClick()
{
    const bool clicked = moveOrActionButtonClicked;
    moveOrActionButtonClicked = false;
    return clicked;
}

bool ButtonMgr::consumeHitButtonClick()
{
    const bool clicked = hitButtonClicked;
    hitButtonClicked = false;
    return clicked;
}

void ButtonMgr::draw() const
{
    const float pad = 14.0f;
    const Color panelBorder = (uiBackdrop != nullptr)
        ? uiBackdrop->panelBorderColor()
        : kPanelBorder;

    // Fill the whole controls box (extends with column height on tall screens).
    // Outer column border is drawn by GameSession; keep an inner accent only.
    if (uiBackdrop != nullptr)
        uiBackdrop->drawPanel(buttonBox, 0.0f, 0);
    else
        DrawRectangleRec(buttonBox, kPanelFill);

    DrawRectangleLinesEx(buttonBox, 2.0f, panelBorder);

    Rectangle accentBar = {
        buttonBox.x + 8.0f,
        buttonBox.y + 8.0f,
        buttonBox.width - 16.0f,
        4.0f
    };
    if (uiBackdrop != nullptr)
        uiBackdrop->drawAccentBar(accentBar);
    else
        DrawRectangleRounded(accentBar, 1.0f, 4, kPanelAccent);

    // STATUS sits in a compact row above MOVE / ACTIONS.
    // Gaps must match buildButtonLayout: 2px under STATUS, 3px above MOVE/ACTIONS.
    constexpr float statusToMoveGap = 3.0f;
    drawSectionLabel("STATUS", buttonBox.x + pad, buttonBox.y + pad);
    drawStatusBar("Health", healthBarBounds, healthPercent);
    drawStatusBar("Energy", energyBarBounds, energyPercent);
    drawStatusBar("Resolve", resolveBarBounds, resolvePercent);
    drawStatusBar("Lucidity", lucidityBarBounds, lucidityPercent);
    drawStatusBar("Charisma", charismaBarBounds, charismaPercent);

    const float moveLabelY = healthBarBounds.y + healthBarBounds.height + statusToMoveGap;
    drawSectionLabel("MOVE", buttonBox.x + pad, moveLabelY);
    const float actionColX = buttonBox.x + pad + (buttonBox.width - pad * 2.0f) * 0.48f + 8.0f;
    drawSectionLabel("ACTIONS", actionColX, moveLabelY);

    for (size_t i = 0; i < buttons.size(); ++i)
    {
        const int index = static_cast<int>(i);
        const Rectangle bounds = buttons[i].getBounds();
        // Darkness (etc.) badges replace the direction label so the outline glyph
        // stays quiet and centered instead of stacking on the text.
        if (i < 6 && !buttons[i].isEnabled() && movementIndexHasBlockIcon(index))
        {
            drawDisabledMovementChrome(bounds);
            drawMovementBlockBadge(index, bounds);
        }
        else
        {
            buttons[i].draw();
        }
    }
}

}
