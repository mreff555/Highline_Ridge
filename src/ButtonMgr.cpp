#include "Button.h"
#include "ButtonMgr.h"
#include <raylib.h>
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
}

ButtonMgr::ButtonMgr(Rectangle _buttonBox, Font _buttonFont)
    : buttonBox(_buttonBox),
      buttonFont(_buttonFont),
      buttonStyle{
          {228, 220, 198, 255},
          {54, 50, 64, 255},
          {78, 72, 92, 255},
          {40, 38, 50, 255},
          {118, 96, 58, 255},
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

    const float moveW = contentW * 0.34f;
    const float actionW = contentW * 0.40f;
    const float actionX = contentX + moveW + gap;
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

    const float inventoryY = buttonBox.y + buttonBox.height - pad - inventoryHeight;
    addButton("Inventory",
        { contentX, inventoryY, contentW, inventoryHeight });
}

ButtonMgr::~ButtonMgr()
{
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

void ButtonMgr::update()
{
    Vector2 mousePos = GetMousePosition();
    for (auto& button : buttons)
    {
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

    forwardButtonPressed = buttons[0].getState() == PRESSED;
    backButtonPressed = buttons[3].getState() == PRESSED;
    leftButtonPressed = buttons[1].getState() == PRESSED;
    rightButtonPressed = buttons[2].getState() == PRESSED;
    examineButtonPressed = buttons[4].getState() == PRESSED;
    speakButtonPressed = buttons[5].getState() == PRESSED;
    hitButtonPressed = buttons[6].getState() == PRESSED;
    useButtonPressed = buttons[7].getState() == PRESSED;
    inventoryButtonPressed = buttons[8].getState() == PRESSED;
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
    drawSectionLabel("ACTIONS", buttonBox.x + pad + buttonBox.width * 0.36f, buttonBox.y + pad);

    const float dividerY = buttonBox.y + buttonBox.height - pad - 52.0f - 8.0f;
    DrawLineEx(
        { buttonBox.x + pad, dividerY },
        { buttonBox.x + buttonBox.width - pad, dividerY },
        1.0f,
        kDivider);

    drawSectionLabel("INVENTORY", buttonBox.x + pad, dividerY + 6.0f);

    for (const auto& button : buttons)
        button.draw();
}

}