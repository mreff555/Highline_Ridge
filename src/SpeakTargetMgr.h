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

#ifndef SPEAK_TARGET_MGR_H
#define SPEAK_TARGET_MGR_H

#include <ScrollPanel.h>
#include <SpeakTargetDef.h>
#include <UiBackdrop.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_engine
{

class SpeakTargetMgr
{
    public:
    SpeakTargetMgr();
    ~SpeakTargetMgr() = default;

    void setPanelBounds(Rectangle bounds);
    void setFont(Font font);
    void setUiBackdrop(const UiBackdrop* backdrop);

    bool isOpen() const { return openState; }
    void open();
    void close();

    void setAvailableTargets(const std::vector<SpeakTargetDef>& targets);
    bool isEmpty() const { return options.empty(); }

    SpeakTargetDef consumePendingTarget();

    void update();
    void draw() const;

    private:
    void drawCloseButton() const;
    void drawOptionList() const;
    void handleCloseButtonInput();
    void handleOptionInput();
    void handleScrollInput();
    void layoutOptionBounds();
    Rectangle getContentBounds() const;
    Rectangle getCloseButtonBounds() const;
    float getRowHeight() const;
    float getContentHeight() const;

    static const float kCloseButtonSize;
    static const float kRowGap;
    static const float kScrollbarWidth;

    Font panelFont{};
    Rectangle panelBounds{};
    const UiBackdrop* uiBackdrop = nullptr;
    ScrollPanel optionScroll;
    bool openState = false;
    std::vector<SpeakTargetDef> options;
    mutable std::vector<Rectangle> optionBounds;
    SpeakTargetDef pendingTarget;
};

}

#endif /* SPEAK_TARGET_MGR_H */