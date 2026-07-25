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

#ifndef UI_BACKDROP_H
#define UI_BACKDROP_H

#include "Button.h"
#include <raylib.h>
#include <string>

namespace timberline_engine
{

enum class UiBackgroundId
{
    None,
    Morris,
    Damask
};

struct UiConfig
{
    UiBackgroundId background = UiBackgroundId::Morris;
};

class UiBackdrop
{
    public:
    UiBackdrop();
    ~UiBackdrop();

    bool load(const UiConfig& config, const std::string& assetRoot);
    bool isActive() const { return backgroundId != UiBackgroundId::None && textureReady; }

    void drawPanel(Rectangle bounds, float roundness = 0.04f, int segments = 10) const;
    void drawAccentBar(Rectangle bar) const;

    ButtonStyle contrastedButtonStyle(const ButtonStyle& base) const;
    Color panelBorderColor() const;
    Color panelAccentColor() const;
    Color sectionLabelColor() const;
    Color statusTrackColor() const;
    Color slotFillColor() const;
    Color slotHoverColor() const;
    Color slotSelectedColor() const;

    private:
    void drawTiledWallpaper(Rectangle bounds, float alpha) const;
    void drawDarkOverlay(Rectangle bounds, float roundness, int segments, unsigned char alpha) const;

    UiBackgroundId backgroundId = UiBackgroundId::None;
    Texture2D wallpaper{};
    bool textureReady = false;

    static const float kTileDisplayWidth;
    static const Color kPanelBorder;
    static const Color kPanelAccent;
    static const Color kSectionLabel;
    static const Color kPanelBase;
};

UiBackgroundId parseUiBackgroundId(const std::string& value);

}

#endif /* UI_BACKDROP_H */