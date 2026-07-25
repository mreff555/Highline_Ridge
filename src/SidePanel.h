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

#ifndef SIDE_PANEL_H
#define SIDE_PANEL_H

#include <raylib.h>
#include <string>

namespace timberline_engine
{

struct SidePanelChrome
{
    static void drawPanelBackground(Rectangle bounds);
    static void drawPanelHeader(Font font, Rectangle bounds, const char* title);
    static Rectangle getCloseButtonBounds(Rectangle panelBounds, float closeButtonSize);
    static bool loadIconTexture(
        const std::string& primaryAssetRoot,
        const std::string& fallbackAssetRoot,
        const std::string& relativePath,
        Texture2D& outTexture);
};

}

#endif /* SIDE_PANEL_H */