#include "Button.h"
#include "ButtonMgr.h"
#include <raylib.h>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace testgame
{

namespace
{
    const Color kPanelFill = {28, 26, 34, 255};
    const Color kPanelBorder = {168, 138, 72, 255};
    const Color kPanelAccent = {96, 78, 48, 255};
    const Color kSectionLabel = {132, 122, 104, 255};
    const Color kDivider = {68, 62, 54, 255};
    const Color kStatusTrack = {40, 38, 50, 255};
    const Color kHealthFill = {168, 72, 72, 255};
    const Color kEnergyFill = {168, 138, 72, 255};
    const Color kTenacityFill = {88, 118, 168, 255};
    const Color kLucidityFill = {118, 88, 168, 255};

    Color statusBarFillColor(const char* label)
    {
        switch (label[0])
        {
            case 'H': return kHealthFill;
            case 'E': return kEnergyFill;
            case 'T': return kTenacityFill;
            case 'L': return kLucidityFill;
            default: return kEnergyFill;
        }
    }

    const float kButtonHoldDurationSeconds = 0.1f;
}

ButtonMgr::ButtonMgr(Rectangle _buttonBox, Font _buttonFont)
    : buttonBox(_buttonBox),
      buttonFont(_buttonFont),
      healthBarBounds{},
      energyBarBounds{},
      tenacityBarBounds{},
      lucidityBarBounds{},
      buttonStyle{
          {228, 220, 198, 255},
          {54, 50, 64, 255},
          {78, 72, 92, 255},
          {40, 38, 50, 255},
          {30, 28, 36, 255},
          {118, 96, 58, 255},
          {62, 52, 34, 255},
          {108, 102, 92, 255},
          0.18f,
          17.0f
      }
{
    const float pad = 18.0f;
    const float labelHeight = 22.0f;
    const float gap = 10.0f;
    const float inventoryHeight = 52.0f;

    const float contentX = buttonBox.x + pad;
    const float contentY = buttonBox.y + pad + labelHeight;
    const float contentW = buttonBox.width - pad * 2.0f;
    const float contentH = buttonBox.height - pad * 2.0f - labelHeight - inventoryHeight - gap;

    const float moveW = contentW * 0.30f;
    const float actionW = contentW * 0.30f;
    const float statusW = contentW - moveW - actionW - gap * 2.0f;
    const float actionX = contentX + moveW + gap;
    const float statusX = actionX + actionW + gap;
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

    addButton("Up",
        { contentX, contentY, moveColLeftW, verticalBtnH });

    addButton("Down",
        { contentX, contentY + verticalBtnH + gap, moveColLeftW, verticalBtnH });

    addButton("Forward",
        { moveRightX + movePadBtnW / 2.0f + gap / 2.0f, contentY, movePadBtnW, movePadBtnH });

    addButton("Left",
        { moveRightX, contentY + movePadBtnH + gap, movePadBtnW, movePadBtnH });

    addButton("Right",
        { moveRightX + movePadBtnW + gap, contentY + movePadBtnH + gap, movePadBtnW, movePadBtnH });

    addButton("back",
        { moveRightX + movePadBtnW / 2.0f + gap / 2.0f, contentY + (movePadBtnH + gap) * 2.0f, movePadBtnW, movePadBtnH });

    addButton("Examine",
        { actionX, contentY, actionBtnW, actionBtnH });

    addButton("Speak",
        { actionX + actionBtnW + gap, contentY, actionBtnW, actionBtnH });

    addButton("Take",
        { actionX, contentY + (actionBtnH + gap) * 1.0f, actionBtnW, actionBtnH });

    addButton("Use",
        { actionX + actionBtnW + gap, contentY + (actionBtnH + gap) * 1.0f, actionBtnW, actionBtnH });

    addButton("Reserved",
        { actionX, contentY + (actionBtnH + gap) * 2.0f, actionBtnW, actionBtnH });

    addButton("Reserved",
        { actionX + actionBtnW + gap, contentY + (actionBtnH + gap) * 2.0f, actionBtnW, actionBtnH });

    const float statusRowH = (contentH - gap) / 2.0f;
    const float statusBarW = (statusW - gap) / 2.0f;
    const float statusRow2Y = contentY + statusRowH + gap;
    healthBarBounds = { statusX, contentY, statusBarW, statusRowH };
    energyBarBounds = { statusX + statusBarW + gap, contentY, statusBarW, statusRowH };
    tenacityBarBounds = { statusX, statusRow2Y, statusBarW, statusRowH };
    lucidityBarBounds = { statusX + statusBarW + gap, statusRow2Y, statusBarW, statusRowH };

    const float inventoryY = buttonBox.y + buttonBox.height - pad - inventoryHeight;
    addButton("Inventory",
        { contentX, inventoryY, contentW, inventoryHeight });
}

ButtonMgr::~ButtonMgr()
{
}

void ButtonMgr::setStatus(float health, float energy, float tenacity, float lucidity)
{
    healthPercent = std::max(0.0f, std::min(health, 100.0f));
    energyPercent = std::max(0.0f, std::min(energy, 100.0f));
    tenacityPercent = std::max(0.0f, std::min(tenacity, 100.0f));
    lucidityPercent = std::max(0.0f, std::min(lucidity, 100.0f));
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
    DrawTextEx(buttonFont, label, { x, y }, 15.0f, 1, kSectionLabel);
}

void ButtonMgr::drawStatusBar(const char* label, Rectangle bounds, float percent) const
{
    const float labelHeight = 16.0f;
    const float barTop = bounds.y + labelHeight;
    const float barHeight = bounds.height - labelHeight - 4.0f;
    const Rectangle track = { bounds.x, barTop, bounds.width, barHeight };
    const float fillWidth = (bounds.width - 4.0f) * (percent / 100.0f);
    const Rectangle fill = {
        bounds.x + 2.0f,
        barTop + 2.0f,
        fillWidth,
        barHeight - 4.0f
    };

    DrawTextEx(buttonFont, label, { bounds.x, bounds.y }, 13.0f, 1, kSectionLabel);
    DrawRectangleRounded(track, 0.18f, 8, kStatusTrack);
    DrawRectangleRoundedLines(track, 0.18f, 8, 2.0f, kPanelBorder);

    if (fillWidth > 0.0f)
    {
        DrawRectangleRounded(fill, 0.18f, 8, statusBarFillColor(label));
    }

    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%d%%", (int)percent);
    const Vector2 textSize = MeasureTextEx(buttonFont, percentText, 13.0f, 1);
    DrawTextEx(
        buttonFont,
        percentText,
        { bounds.x + (bounds.width - textSize.x) / 2.0f, barTop + (barHeight - textSize.y) / 2.0f },
        13.0f,
        1,
        {228, 220, 198, 220});
}

void ButtonMgr::setAvailability(const MovementStruct& movement, const ActionStruct& actions)
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
    buttons[10].setEnabled(false);
    buttons[11].setEnabled(false);
    buttons[12].setEnabled(true);
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
    const int movementAndActionCount = 12;
    for (int i = 0; i < movementAndActionCount; ++i)
    {
        if (buttons[i].isEnabled() &&
            isPointInClickableBounds(mousePos, buttons[i].getBounds(), clickPadding))
            return i;
    }

    if (buttons[12].isEnabled() &&
        isPointInClickableBounds(mousePos, buttons[12].getBounds(), clickPadding))
        return 12;

    return -1;
}

void ButtonMgr::registerButtonClick(int buttonIndex)
{
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
        case 9: useButtonClicked = true; break;
        case 12: inventoryButtonClicked = true; break;
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
    hitButtonPressed = false;
    useButtonPressed = buttons[9].isEnabled() && buttons[9].getState() == PRESSED;
    inventoryButtonPressed = buttons[12].isEnabled() && buttons[12].getState() == PRESSED;
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
    inventoryButtonClicked = false;

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
            if (GetTime() - activePressStartTime >= kButtonHoldDurationSeconds)
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

void ButtonMgr::draw() const
{
    const float pad = 18.0f;
    const float labelHeight = 22.0f;

    DrawRectangleRounded(buttonBox, 0.04f, 10, kPanelFill);
    DrawRectangleRoundedLines(buttonBox, 0.04f, 10, 3.0f, kPanelBorder);

    Rectangle accentBar = {
        buttonBox.x + 8.0f,
        buttonBox.y + 8.0f,
        buttonBox.width - 16.0f,
        4.0f
    };
    DrawRectangleRounded(accentBar, 1.0f, 4, kPanelAccent);

    drawSectionLabel("MOVE", buttonBox.x + pad, buttonBox.y + pad);
    drawSectionLabel("ACTIONS", buttonBox.x + pad + buttonBox.width * 0.32f, buttonBox.y + pad);
    drawSectionLabel("STATUS", healthBarBounds.x, buttonBox.y + pad);

    const float dividerY = buttonBox.y + buttonBox.height - pad - 52.0f - 8.0f;
    DrawLineEx(
        { buttonBox.x + pad, dividerY },
        { buttonBox.x + buttonBox.width - pad, dividerY },
        1.0f,
        kDivider);

    drawSectionLabel("INVENTORY", buttonBox.x + pad, dividerY + 6.0f);

    drawStatusBar("Health", healthBarBounds, healthPercent);
    drawStatusBar("Energy", energyBarBounds, energyPercent);
    drawStatusBar("Tenacity", tenacityBarBounds, tenacityPercent);
    drawStatusBar("Lucidity", lucidityBarBounds, lucidityPercent);

    for (const auto& button : buttons)
        button.draw();
}

}
