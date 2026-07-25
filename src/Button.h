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

#ifndef BUTTON_H
#define BUTTON_H

#include <raylib.h>

enum ButtonState
{
    NORMAL,
    HOVERED,
    PRESSED
};

struct ButtonStyle
{
    Color textColor;
    Color normalBg;
    Color hoverBg;
    Color pressedBg;
    Color disabledBg;
    Color borderColor;
    Color disabledBorderColor;
    Color disabledTextColor;
    float roundness;
    float fontSize;
};

class Button
{
    public:
    Button(const char* text, Vector2 position, Vector2 size, Font font, const ButtonStyle& style);
    ~Button();

    void draw() const;
    bool isClicked() const;
    Rectangle getBounds() const;
    void setState(ButtonState state);
    ButtonState getState() const { return state; }
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled; }
    void setStyle(const ButtonStyle& newStyle) { style = newStyle; }

    private:
    const char* text;
    Vector2 position;
    Vector2 size;
    Font font;
    ButtonStyle style;
    ButtonState state = NORMAL;
    bool enabled = true;
};

#endif /* BUTTON_H */