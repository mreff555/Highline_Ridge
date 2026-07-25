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
#include <RaylibCompat.h>

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

        Vector2 textSize = MeasureTextEx(font, text, style.fontSize, 1);
        Vector2 textPosition = {
            position.x + (size.x - textSize.x) / 2.0f,
            position.y + (size.y - textSize.y) / 2.0f
        };
        DrawTextEx(font, text, textPosition, style.fontSize, 1, style.disabledTextColor);
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

    Vector2 textSize = MeasureTextEx(font, text, style.fontSize, 1);
    Vector2 textPosition = {
        position.x + (size.x - textSize.x) / 2.0f,
        position.y + (size.y - textSize.y) / 2.0f
    };
    DrawTextEx(font, text, textPosition, style.fontSize, 1, style.textColor);
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