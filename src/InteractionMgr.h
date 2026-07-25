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

#ifndef INTERACTION_MGR_H
#define INTERACTION_MGR_H

#include <SceneInteractionDef.h>
#include <UiBackdrop.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_engine
{

class InteractionMgr
{
    public:
    InteractionMgr();
    ~InteractionMgr() = default;

    void setPanelBounds(Rectangle bounds);
    void setFont(Font font);
    void setUiBackdrop(const UiBackdrop* backdrop);

    bool isOpen() const { return openState; }
    void open();
    void close();

    void setAvailableInteractions(const std::vector<SceneInteractionDef>& interactions);
    bool isEmpty() const { return options.empty(); }

    std::string consumePendingInteractionId();

    void update();
    void draw() const;

    private:
    void drawCloseButton() const;
    void drawOptionList() const;
    void handleCloseButtonInput();
    void handleOptionInput();
    void layoutOptionBounds();
    Rectangle getCloseButtonBounds() const;
    float getRowHeight() const;

    static const float kCloseButtonSize;
    static const float kRowGap;

    Font panelFont{};
    Rectangle panelBounds{};
    const UiBackdrop* uiBackdrop = nullptr;
    bool openState = false;
    std::vector<SceneInteractionDef> options;
    mutable std::vector<Rectangle> optionBounds;
    std::string pendingInteractionId;
};

}

#endif /* INTERACTION_MGR_H */