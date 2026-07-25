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

#include "SidePanel.h"

#include <ImageCompression.h>
#include <RaylibCompat.h>

namespace timberline_engine
{

namespace
{
    const Color kPanelFill = {28, 26, 34, 255};
    const Color kPanelBorder = {168, 138, 72, 255};
    const Color kPanelAccent = {96, 78, 48, 255};
    const Color kSectionLabel = {132, 122, 104, 255};
}

void SidePanelChrome::drawPanelBackground(Rectangle bounds)
{
    DrawRectangleRounded(bounds, 0.04f, 10, kPanelFill);
    DrawRoundedBorder(bounds, 0.04f, 10, 3.0f, kPanelBorder);

    const Rectangle accentBar = {
        bounds.x + 8.0f,
        bounds.y + 8.0f,
        bounds.width - 16.0f,
        4.0f
    };
    DrawRectangleRounded(accentBar, 1.0f, 4, kPanelAccent);
}

void SidePanelChrome::drawPanelHeader(Font font, Rectangle bounds, const char* title)
{
    const float pad = 14.0f;
    DrawTextEx(font, title, { bounds.x + pad, bounds.y + pad }, 17.0f, 1, kSectionLabel);
}

Rectangle SidePanelChrome::getCloseButtonBounds(Rectangle panelBounds, float closeButtonSize)
{
    const float pad = 14.0f;
    return {
        panelBounds.x + panelBounds.width - closeButtonSize - pad,
        panelBounds.y + pad,
        closeButtonSize,
        closeButtonSize
    };
}

bool SidePanelChrome::loadIconTexture(
    const std::string& primaryAssetRoot,
    const std::string& fallbackAssetRoot,
    const std::string& relativePath,
    Texture2D& outTexture)
{
    if (relativePath.empty())
        return false;

    const std::string primaryPath = primaryAssetRoot + "/" + relativePath;
    const std::string fallbackPath = fallbackAssetRoot + "/" + relativePath;

    if (loadTextureFromAssetFile(primaryPath, outTexture))
        return true;

    if (loadTextureFromAssetFile(fallbackPath, outTexture))
        return true;

    return false;
}

}