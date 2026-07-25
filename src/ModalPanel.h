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

#ifndef MODAL_PANEL_H
#define MODAL_PANEL_H

#include <raylib.h>

namespace timberline_engine
{

struct ModalPanelStyle
{
    Color overlayDim;
    Color panelFill;
    Color panelBorder;
    Color panelAccent;
    Color titleColor;
};

class ModalPanel
{
    public:
    ModalPanel();
    ModalPanel(int screenWidth, int screenHeight);

    void setScreenSize(int width, int height);
    void drawOverlay() const;
    void drawChrome(const Rectangle& panel) const;
    void drawCenteredTitle(
        Font font,
        const Rectangle& panel,
        const char* title,
        float fontSize = 24.0f) const;

    static const ModalPanelStyle& defaultStyle();

    private:
    int screenWidth = 0;
    int screenHeight = 0;
};

}

#endif /* MODAL_PANEL_H */