#include "Button.h"
#include <RaylibCompat.h>

namespace
{
    void drawClippedLabel(
        Rectangle bounds,
        Font font,
        const char* text,
        float baseFontSize,
        Color textColor)
    {
        const float horizontalPad = 4.0f;
        const float minFontSize = 12.0f;
        float fontSize = baseFontSize;
        Vector2 textSize = MeasureTextEx(font, text, fontSize, 1);

        while (fontSize > minFontSize &&
               textSize.x > bounds.width - horizontalPad)
        {
            fontSize -= 1.0f;
            textSize = MeasureTextEx(font, text, fontSize, 1);
        }

        Vector2 textPosition = {
            bounds.x + (bounds.width - textSize.x) / 2.0f,
            bounds.y + (bounds.height - textSize.y) / 2.0f
        };

        if (textSize.x > bounds.width - horizontalPad)
            textPosition.x = bounds.x + horizontalPad * 0.5f;

        const int clipX = (int)bounds.x;
        const int clipY = (int)bounds.y;
        const int clipW = (int)(bounds.width + 0.5f);
        const int clipH = (int)(bounds.height + 0.5f);

        if (clipW <= 0 || clipH <= 0)
            return;

        BeginScissorMode(clipX, clipY, clipW, clipH);
        DrawTextEx(font, text, textPosition, fontSize, 1, textColor);
        EndScissorMode();
    }
}

Button::Button(const char* text, Vector2 position, Vector2 size, Font font, const ButtonStyle& style)
    : text(text), position(position), size(size), font(font), style(style)
{
}

Button::~Button()
{
}

void Button::draw() const
{
    Rectangle bounds = getBounds();

    if (!enabled)
    {
        DrawRectangleRounded(bounds, style.roundness, 8, style.disabledBg);
        DrawRoundedBorder(bounds, style.roundness, 8, 2.0f, style.disabledBorderColor);
        drawClippedLabel(bounds, font, text, style.fontSize, style.disabledTextColor);
        return;
    }

    Color fillColor = style.normalBg;
    if (state == HOVERED)
        fillColor = style.hoverBg;
    else if (state == PRESSED)
        fillColor = style.pressedBg;

    DrawRectangleRounded(bounds, style.roundness, 8, fillColor);
    DrawRoundedBorder(bounds, style.roundness, 8, 2.0f, style.borderColor);

    if (state == HOVERED)
    {
        Rectangle highlight = { bounds.x + 2, bounds.y + 2, bounds.width - 4, bounds.height * 0.35f };
        DrawRectangleRounded(highlight, style.roundness, 8, Fade(WHITE, 0.08f));
    }

    drawClippedLabel(bounds, font, text, style.fontSize, style.textColor);
}

bool Button::isClicked() const
{
    if (!enabled)
        return false;

    return IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
           CheckCollisionPointRec(GetMousePosition(), getBounds());
}

Rectangle Button::getBounds() const
{
    return { position.x, position.y, size.x, size.y };
}

void Button::setState(ButtonState state)
{
    if (!enabled)
    {
        this->state = NORMAL;
        return;
    }

    this->state = state;
}

void Button::setEnabled(bool enabled)
{
    this->enabled = enabled;
    if (!enabled)
        state = NORMAL;
}