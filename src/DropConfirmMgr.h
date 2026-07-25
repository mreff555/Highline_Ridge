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

#ifndef DROP_CONFIRM_MGR_H
#define DROP_CONFIRM_MGR_H

#include <Button.h>
#include <ModalPanel.h>
#include <UiBackdrop.h>
#include <raylib.h>
#include <string>

namespace timberline_engine
{

class DropConfirmMgr
{
    public:
    DropConfirmMgr(int screenWidth, int screenHeight, Font uiFont);

    void setScreenSize(int width, int height);
    void setUiBackdrop(const UiBackdrop* backdrop);

    bool isOpen() const { return open; }
    const std::string& getPendingItemId() const { return pendingItemId; }

    void openForItem(const std::string& itemId, const std::string& itemName);
    void close();

    void update();
    void draw() const;

    std::string consumeConfirmedItemId();
    bool consumeCancelled();
    bool consumeDontShowAgainRequest();

    private:
    void layoutButtons();
    Rectangle getPanelBounds() const;
    Rectangle getCheckboxBounds() const;
    void drawPanelFrame() const;
    void drawCheckbox() const;
    void handleCheckboxInput();

    int screenWidth;
    int screenHeight;
    Font uiFont;
    ButtonStyle baseButtonStyle{};
    ButtonStyle buttonStyle{};
    const UiBackdrop* uiBackdrop = nullptr;

    bool open = false;
    std::string pendingItemId;
    std::string pendingItemName;
    bool dontShowAgainChecked = false;
    std::string confirmedItemId;
    bool cancelled = false;
    bool dontShowAgainRequested = false;

    Button yesButton;
    Button noButton;
    ModalPanel modalPanel;
};

}

#endif /* DROP_CONFIRM_MGR_H */