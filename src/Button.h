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
    virtual ~Button();

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