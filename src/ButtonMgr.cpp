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
          18.0f
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
    const float actionCount = 4.0f;
    const float actionH = (contentH - gap * (actionCount - 1.0f)) / actionCount;

    const float moveBtnW = (moveW - gap) / 2.0f;
    const float moveBtnH = (contentH - gap * 2.0f) / 3.0f;

    addButton("Forward",
        { contentX + moveBtnW / 2.0f + gap / 2.0f, contentY, moveBtnW, moveBtnH });

    addButton("Left",
        { contentX, contentY + moveBtnH + gap, moveBtnW, moveBtnH });

    addButton("Right",
        { contentX + moveBtnW + gap, contentY + moveBtnH + gap, moveBtnW, moveBtnH });

    addButton("Backward",
        { contentX + moveBtnW / 2.0f + gap / 2.0f, contentY + (moveBtnH + gap) * 2.0f, moveBtnW, moveBtnH });

    addButton("Examine",
        { actionX, contentY, actionW, actionH });

    addButton("Speak",
        { actionX, contentY + (actionH + gap) * 1.0f, actionW, actionH });

    addButton("Hit",
        { actionX, contentY + (actionH + gap) * 2.0f, actionW, actionH });

    addButton("Use",
        { actionX, contentY + (actionH + gap) * 3.0f, actionW, actionH });

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
    DrawTextEx(buttonFont, label, { x, y }, 14.0f, 1, kSectionLabel);
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

    DrawTextEx(buttonFont, label, { bounds.x, bounds.y }, 12.0f, 1, kSectionLabel);
    DrawRectangleRounded(track, 0.18f, 8, kStatusTrack);
    DrawRectangleRoundedLines(track, 0.18f, 8, 2.0f, kPanelBorder);

    if (fillWidth > 0.0f)
    {
        DrawRectangleRounded(fill, 0.18f, 8, statusBarFillColor(label));
    }

    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%d%%", (int)percent);
    const Vector2 textSize = MeasureTextEx(buttonFont, percentText, 12.0f, 1);
    DrawTextEx(
        buttonFont,
        percentText,
        { bounds.x + (bounds.width - textSize.x) / 2.0f, barTop + (barHeight - textSize.y) / 2.0f },
        12.0f,
        1,
        {228, 220, 198, 220});
}

void ButtonMgr::setAvailability(const MovementStruct& movement, const ActionStruct& actions)
{
    buttons[0].setEnabled(movement.forward);
    buttons[1].setEnabled(movement.left);
    buttons[2].setEnabled(movement.right);
    buttons[3].setEnabled(movement.backward);
    buttons[4].setEnabled(actions.examine);
    buttons[5].setEnabled(actions.speak);
    buttons[6].setEnabled(actions.hit);
    buttons[7].setEnabled(actions.use);
    buttons[8].setEnabled(true);
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
    const int movementAndActionCount = 8;
    for (int i = 0; i < movementAndActionCount; ++i)
    {
        if (buttons[i].isEnabled() &&
            isPointInClickableBounds(mousePos, buttons[i].getBounds(), clickPadding))
            return i;
    }

    if (buttons[8].isEnabled() &&
        isPointInClickableBounds(mousePos, buttons[8].getBounds(), clickPadding))
        return 8;

    return -1;
}

void ButtonMgr::registerButtonClick(int buttonIndex)
{
    switch (buttonIndex)
    {
        case 0: forwardButtonClicked = true; break;
        case 1: leftButtonClicked = true; break;
        case 2: rightButtonClicked = true; break;
        case 3: backwardButtonClicked = true; break;
        case 4: examineButtonClicked = true; break;
        case 5: speakButtonClicked = true; break;
        case 7: useButtonClicked = true; break;
        default: break;
    }
}

void ButtonMgr::updatePressedFlags()
{
    forwardButtonPressed = buttons[0].isEnabled() && buttons[0].getState() == PRESSED;
    leftButtonPressed = buttons[1].isEnabled() && buttons[1].getState() == PRESSED;
    rightButtonPressed = buttons[2].isEnabled() && buttons[2].getState() == PRESSED;
    backButtonPressed = buttons[3].isEnabled() && buttons[3].getState() == PRESSED;
    examineButtonPressed = buttons[4].isEnabled() && buttons[4].getState() == PRESSED;
    speakButtonPressed = buttons[5].isEnabled() && buttons[5].getState() == PRESSED;
    hitButtonPressed = buttons[6].isEnabled() && buttons[6].getState() == PRESSED;
    useButtonPressed = buttons[7].isEnabled() && buttons[7].getState() == PRESSED;
    inventoryButtonPressed = buttons[8].isEnabled() && buttons[8].getState() == PRESSED;
}

void ButtonMgr::update()
{
    forwardButtonClicked = false;
    backwardButtonClicked = false;
    leftButtonClicked = false;
    rightButtonClicked = false;
    examineButtonClicked = false;
    speakButtonClicked = false;
    useButtonClicked = false;

    Vector2 mousePos = GetMousePosition();
    const bool mouseDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool mouseJustPressed = mouseDown && !mouseWasDownLastFrame;
    mouseWasDownLastFrame = mouseDown;

    if (mouseJustPressed)
    {
        const int buttonIndex = findEnabledButtonUnderMouse(mousePos);
        if (buttonIndex >= 0)
            registerButtonClick(buttonIndex);
    }

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