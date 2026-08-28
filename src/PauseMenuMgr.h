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

#ifndef PAUSE_MENU_MGR_H
#define PAUSE_MENU_MGR_H

#include <Button.h>
#include <GameConfig.h>
#include <ModalPanel.h>
#include <UiBackdrop.h>
#include <functional>
#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_engine
{

class AudioManager;

enum class PauseMenuPanel
{
    Main,
    Config
};

/** Title = boot main menu; InGame = ESC pause menu. */
enum class PauseMenuContext
{
    Title,
    InGame
};

class PauseMenuMgr
{
    public:
    PauseMenuMgr(int screenWidth, int screenHeight, Font uiFont);

    void setAudioManager(AudioManager* audioManager);
    void setGameConfig(GameConfig* config);
    void setConfigPath(const std::string& path);
    void setScreenSize(int width, int height);
    void setUiBackdrop(const UiBackdrop* backdrop);
    void setOnDisplaySettingsChanged(const std::function<void()>& callback);
    void setOnInputSettingsChanged(const std::function<void()>& callback);
    /** When false, Continue is disabled on the title screen. */
    void setContinueAvailable(bool available);

    bool isOpen() const { return open; }
    bool isConfigPanel() const { return panel == PauseMenuPanel::Config; }
    bool isTitleContext() const { return context == PauseMenuContext::Title; }
    PauseMenuContext getContext() const { return context; }

    /** ESC pause menu (Resume instead of Continue). */
    void openMenu();
    /** Boot / main menu (Continue / New Game / …). */
    void openTitleMenu();
    void closeMenu();
    void showConfigPanel();
    void showMainPanel();

    void update(float deltaSeconds);
    void draw() const;

    bool consumeQuitRequest();
    bool consumeOpenSaveMenuRequest();
    bool consumeOpenLoadMenuRequest();
    bool consumeContinueRequest();
    bool consumeNewGameRequest();
    bool consumeResumeRequest();
    void setStatusMessage(const std::string& message, float durationSeconds = 2.5f);
    const std::string& getStatusMessage() const { return statusMessage; }

    private:
    enum class ConfigRowType
    {
        SectionHeader,
        Slider,
        ToggleButton,
        CycleButton
    };

    struct ConfigRow
    {
        ConfigRowType type;
        const char* label;
        int sliderIndex;
    };

    void layoutMainButtons();
    void layoutConfigButtons();
    void handleMainInput();
    void handleConfigInput();
    void drawPanelFrame(const char* title) const;
    const char* mainPanelTitle() const;
    void drawStatusMessage() const;
    void drawConfigPanel() const;
    void drawSliderRow(
        float x,
        float y,
        float width,
        const char* label,
        float value,
        const char* valueText) const;
    void handleConfigScrollInput();
    void handleSliderInput();
    void handleConfigButtonInput();
    void cycleResolutionPreset();
    void cycleMaxNamedSaves();
    void toggleFullscreen();
    void applyAudioConfig() const;
    void persistGameConfig() const;
    float readSliderValue(const Rectangle& track) const;
    float clickHoldSliderValue() const;
    void setClickHoldFromSlider(float normalizedValue);
    std::string resolutionLabel() const;
    Rectangle getPanelBounds() const;
    Rectangle getConfigContentBounds() const;
    float getConfigContentHeight() const;
    float getConfigRowY(int rowIndex) const;
    float getConfigRowHeight(const ConfigRow& row) const;
    Rectangle getConfigRowBounds(int rowIndex) const;
    const std::vector<ConfigRow>& getConfigRows() const;

    int screenWidth;
    int screenHeight;
    Font uiFont;
    AudioManager* audioManager = nullptr;
    GameConfig* gameConfig = nullptr;
    std::string configPath = "resources/game_config.json";
    std::function<void()> onDisplaySettingsChanged;
    std::function<void()> onInputSettingsChanged;

    bool open = false;
    PauseMenuContext context = PauseMenuContext::InGame;
    PauseMenuPanel panel = PauseMenuPanel::Main;
    bool continueAvailable = false;
    ButtonStyle baseButtonStyle{};
    ButtonStyle buttonStyle{};
    const UiBackdrop* uiBackdrop = nullptr;
    std::vector<Button> buttons;

    bool quitRequested = false;
    bool openSaveMenuRequested = false;
    bool openLoadMenuRequested = false;
    bool continueRequested = false;
    bool newGameRequested = false;
    bool resumeRequested = false;

    std::string statusMessage;
    float statusMessageTimer = 0.0f;

    int activeSliderIndex = -1;
    float configScrollY = 0.0f;
    ModalPanel modalPanel;
};

}

#endif /* PAUSE_MENU_MGR_H */