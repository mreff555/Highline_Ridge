#ifndef SAVE_LOAD_MENU_MGR_H
#define SAVE_LOAD_MENU_MGR_H

#include <Button.h>
#include <SaveGame.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace testgame
{

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
    bool isOpen() const { return mode != SaveLoadMenuMode::Closed; }
    SaveLoadMenuMode getMode() const { return mode; }

    void openSaveMenu();
    void openLoadMenu();
    void closeMenu();
    void refreshLoadList();

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
    ButtonStyle buttonStyle{};
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
};

}

#endif /* SAVE_LOAD_MENU_MGR_H */