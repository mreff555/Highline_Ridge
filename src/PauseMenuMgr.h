#ifndef PAUSE_MENU_MGR_H
#define PAUSE_MENU_MGR_H

#include <Button.h>
#include <GameConfig.h>
#include <functional>
#include <raylib.h>
#include <string>
#include <vector>

namespace testgame
{

class AudioManager;

enum class PauseMenuPanel
{
    Main,
    Config
};

class PauseMenuMgr
{
    public:
    PauseMenuMgr(int screenWidth, int screenHeight, Font uiFont);

    void setAudioManager(AudioManager* audioManager);
    void setGameConfig(GameConfig* config);
    void setConfigPath(const std::string& path);
    void setScreenSize(int width, int height);
    void setOnDisplaySettingsChanged(const std::function<void()>& callback);
    void setOnInputSettingsChanged(const std::function<void()>& callback);

    bool isOpen() const { return open; }
    bool isConfigPanel() const { return panel == PauseMenuPanel::Config; }
    void openMenu();
    void closeMenu();
    void showConfigPanel();
    void showMainPanel();

    void update(float deltaSeconds);
    void draw() const;

    bool consumeQuitRequest();
    bool consumeOpenSaveMenuRequest();
    bool consumeOpenLoadMenuRequest();
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
    PauseMenuPanel panel = PauseMenuPanel::Main;
    ButtonStyle buttonStyle{};
    std::vector<Button> buttons;

    bool quitRequested = false;
    bool openSaveMenuRequested = false;
    bool openLoadMenuRequested = false;

    std::string statusMessage;
    float statusMessageTimer = 0.0f;

    int activeSliderIndex = -1;
    float configScrollY = 0.0f;
};

}

#endif /* PAUSE_MENU_MGR_H */