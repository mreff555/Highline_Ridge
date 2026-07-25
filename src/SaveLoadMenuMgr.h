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

#ifndef SAVE_LOAD_MENU_MGR_H
#define SAVE_LOAD_MENU_MGR_H

#include <Button.h>
#include <ModalPanel.h>
#include <SaveGame.h>
#include <UiBackdrop.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_engine
{

class SaveGameService;

enum class SaveLoadMenuMode
{
    Closed,
    Save,
    Load
};

class SaveLoadMenuMgr
{
    public:
    SaveLoadMenuMgr(int screenWidth, int screenHeight, Font uiFont);

    void setScreenSize(int width, int height);
    void setUiBackdrop(const UiBackdrop* backdrop);
    bool isOpen() const { return mode != SaveLoadMenuMode::Closed; }
    SaveLoadMenuMode getMode() const { return mode; }

    void openSaveMenu();
    void openLoadMenu();
    void closeMenu();
    void refreshLoadList();
    void setSaveGameService(const SaveGameService* service) { saveGameService = service; }

    void update();
    void draw() const;

    bool consumeBackRequest();
    bool consumeNamedSaveRequest(std::string& outSaveName);
    bool consumeLoadSlotRequest(std::string& outFilePath);

    private:
    void layoutButtons();
    void handleSaveInput();
    void handleLoadInput();
    void handleTextInput();
    void drawPanelFrame(const char* title) const;
    void drawSavePanel() const;
    void drawLoadPanel() const;
    void drawSaveNameField() const;
    void drawLoadRows() const;
    std::string truncateNameForRow(
        const std::string& name,
        float maxWidth,
        float fontSize) const;
    Rectangle getPanelBounds() const;
    Rectangle getContentBounds() const;

    int screenWidth;
    int screenHeight;
    Font uiFont;
    ButtonStyle baseButtonStyle{};
    ButtonStyle buttonStyle{};
    const UiBackdrop* uiBackdrop = nullptr;
    std::vector<Button> buttons;

    SaveLoadMenuMode mode = SaveLoadMenuMode::Closed;
    std::string saveNameInput;
    std::vector<SaveSlotListing> loadSlots;
    int selectedLoadIndex = -1;
    float loadScrollY = 0.0f;

    bool backRequested = false;
    bool namedSaveRequested = false;
    bool loadSlotRequested = false;
    std::string pendingLoadPath;
    ModalPanel modalPanel;
    const SaveGameService* saveGameService = nullptr;
};

}

#endif /* SAVE_LOAD_MENU_MGR_H */