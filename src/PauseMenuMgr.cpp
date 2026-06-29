#include "PauseMenuMgr.h"
#include <AudioManager.h>
#include <RaylibCompat.h>
#include <ScrollPanel.h>
#include <algorithm>
#include <cstdio>

namespace highline_ridge
{

namespace
{
    const Color kPanelFill = {28, 26, 34, 255};
    const Color kPanelBorder = {168, 138, 72, 255};
    const Color kPanelAccent = {96, 78, 48, 255};
    const Color kSectionLabel = {132, 122, 104, 255};
    const Color kStatusTrack = {40, 38, 50, 255};
    const Color kSliderFill = {168, 138, 72, 255};
    const Color kOverlayDim = {8, 8, 12, 210};

    struct ResolutionPreset
    {
        int width;
        int height;
        const char* label;
    };

    const ResolutionPreset kResolutionPresets[] = {
        {1280, 720, "1280 x 720"},
        {1500, 1117, "1500 x 1117"},
        {1920, 1080, "1920 x 1080"},
        {2560, 1440, "2560 x 1440"}
    };

    const int kResolutionPresetCount = sizeof(kResolutionPresets) / sizeof(kResolutionPresets[0]);
    const int kAudioSliderCount = 4;
    const int kClickHoldSliderIndex = 4;
    const int kSliderCount = 5;
    const int kMaxSavePresetCount = 8;
    const int kMaxSavePresets[kMaxSavePresetCount] = { 5, 10, 15, 20, 25, 30, 40, 50 };
}

PauseMenuMgr::PauseMenuMgr(int screenWidth_, int screenHeight_, Font uiFont_)
    : screenWidth(screenWidth_),
      screenHeight(screenHeight_),
      uiFont(uiFont_),
      modalPanel(screenWidth_, screenHeight_),
      baseButtonStyle{
          {228, 220, 198, 255},
          {54, 50, 64, 255},
          {78, 72, 92, 255},
          {40, 38, 50, 255},
          {30, 28, 36, 255},
          {118, 96, 58, 255},
          {62, 52, 34, 255},
          {108, 102, 92, 255},
          0.18f,
          18.0f
      },
      buttonStyle{}
{
    buttonStyle = baseButtonStyle;
    layoutMainButtons();
}

void PauseMenuMgr::setUiBackdrop(const UiBackdrop* backdrop)
{
    uiBackdrop = backdrop;
    buttonStyle = (uiBackdrop != nullptr)
        ? uiBackdrop->contrastedButtonStyle(baseButtonStyle)
        : baseButtonStyle;
    if (open)
    {
        if (panel == PauseMenuPanel::Config)
            layoutConfigButtons();
        else
            layoutMainButtons();
    }
}

void PauseMenuMgr::setAudioManager(AudioManager* manager)
{
    audioManager = manager;
}

void PauseMenuMgr::setGameConfig(GameConfig* config)
{
    gameConfig = config;
}

void PauseMenuMgr::setConfigPath(const std::string& path)
{
    configPath = path;
}

void PauseMenuMgr::setScreenSize(int width, int height)
{
    screenWidth = width;
    screenHeight = height;
    modalPanel.setScreenSize(width, height);
    if (open)
    {
        if (panel == PauseMenuPanel::Config)
            layoutConfigButtons();
        else
            layoutMainButtons();
    }
}

void PauseMenuMgr::setOnDisplaySettingsChanged(const std::function<void()>& callback)
{
    onDisplaySettingsChanged = callback;
}

void PauseMenuMgr::setOnInputSettingsChanged(const std::function<void()>& callback)
{
    onInputSettingsChanged = callback;
}

const std::vector<PauseMenuMgr::ConfigRow>& PauseMenuMgr::getConfigRows() const
{
    static const std::vector<ConfigRow> rows = {
        { ConfigRowType::SectionHeader, "AUDIO", -1 },
        { ConfigRowType::Slider, "Master Volume", 0 },
        { ConfigRowType::Slider, "Music Volume", 1 },
        { ConfigRowType::Slider, "Ambient Volume", 2 },
        { ConfigRowType::Slider, "SFX Volume", 3 },
        { ConfigRowType::SectionHeader, "DISPLAY", -1 },
        { ConfigRowType::CycleButton, "Resolution", -1 },
        { ConfigRowType::ToggleButton, "Fullscreen", -1 },
        { ConfigRowType::SectionHeader, "INPUT", -1 },
        { ConfigRowType::Slider, "Click Hold Duration", kClickHoldSliderIndex },
        { ConfigRowType::SectionHeader, "SAVES", -1 },
        { ConfigRowType::CycleButton, "Max Number of Saves", -1 }
    };
    return rows;
}

Rectangle PauseMenuMgr::getPanelBounds() const
{
    const float panelWidth = 460.0f;
    const float panelHeight = panel == PauseMenuPanel::Main
        ? 430.0f
        : std::min(760.0f, (float)screenHeight * 0.88f);
    return {
        (screenWidth - panelWidth) * 0.5f,
        (screenHeight - panelHeight) * 0.5f,
        panelWidth,
        panelHeight
    };
}

Rectangle PauseMenuMgr::getConfigContentBounds() const
{
    const Rectangle panel = getPanelBounds();
    return {
        panel.x + 24.0f,
        panel.y + 78.0f,
        panel.width - 48.0f,
        panel.height - 166.0f
    };
}

float PauseMenuMgr::getConfigRowHeight(const ConfigRow& row) const
{
    switch (row.type)
    {
        case ConfigRowType::SectionHeader:
            return 30.0f;
        case ConfigRowType::Slider:
            return 58.0f;
        case ConfigRowType::ToggleButton:
        case ConfigRowType::CycleButton:
            return 48.0f;
    }

    return 48.0f;
}

float PauseMenuMgr::getConfigContentHeight() const
{
    float height = 0.0f;
    for (const ConfigRow& row : getConfigRows())
        height += getConfigRowHeight(row);
    return height;
}

float PauseMenuMgr::getConfigRowY(int rowIndex) const
{
    float y = 0.0f;
    const std::vector<ConfigRow>& rows = getConfigRows();
    for (int i = 0; i < rowIndex && i < (int)rows.size(); ++i)
        y += getConfigRowHeight(rows[(size_t)i]);
    return y;
}

Rectangle PauseMenuMgr::getConfigRowBounds(int rowIndex) const
{
    const Rectangle content = getConfigContentBounds();
    const std::vector<ConfigRow>& rows = getConfigRows();
    if (rowIndex < 0 || rowIndex >= (int)rows.size())
        return { 0.0f, 0.0f, 0.0f, 0.0f };

    const float rowY = getConfigRowY(rowIndex);
    const float rowHeight = getConfigRowHeight(rows[(size_t)rowIndex]);
    return {
        content.x,
        content.y + rowY - configScrollY,
        content.width,
        rowHeight
    };
}

void PauseMenuMgr::layoutMainButtons()
{
    panel = PauseMenuPanel::Main;
    buttons.clear();

    const Rectangle panel = getPanelBounds();
    const float buttonWidth = panel.width - 72.0f;
    const float buttonHeight = 52.0f;
    const float gap = 14.0f;
    const float startX = panel.x + 36.0f;
    float startY = panel.y + 108.0f;

    const char* labels[] = { "Load", "Save", "Config", "Quit" };
    for (const char* label : labels)
    {
        buttons.push_back(Button(
            label,
            { startX, startY },
            { buttonWidth, buttonHeight },
            uiFont,
            buttonStyle));
        startY += buttonHeight + gap;
    }
}

void PauseMenuMgr::layoutConfigButtons()
{
    panel = PauseMenuPanel::Config;
    buttons.clear();

    const Rectangle panel = getPanelBounds();
    const float buttonWidth = panel.width - 72.0f;
    const float buttonHeight = 48.0f;
    const float startX = panel.x + 36.0f;
    const float startY = panel.y + panel.height - 88.0f;

    buttons.push_back(Button(
        "Back",
        { startX, startY },
        { buttonWidth, buttonHeight },
        uiFont,
        buttonStyle));
}

void PauseMenuMgr::openMenu()
{
    open = true;
    showMainPanel();
    statusMessage.clear();
    statusMessageTimer = 0.0f;
}

void PauseMenuMgr::closeMenu()
{
    open = false;
    activeSliderIndex = -1;
    configScrollY = 0.0f;
    showMainPanel();
}

void PauseMenuMgr::showConfigPanel()
{
    layoutConfigButtons();
    activeSliderIndex = -1;
    configScrollY = 0.0f;
}

void PauseMenuMgr::showMainPanel()
{
    layoutMainButtons();
    activeSliderIndex = -1;
    configScrollY = 0.0f;
}

bool PauseMenuMgr::consumeQuitRequest()
{
    const bool requested = quitRequested;
    quitRequested = false;
    return requested;
}

bool PauseMenuMgr::consumeOpenSaveMenuRequest()
{
    const bool requested = openSaveMenuRequested;
    openSaveMenuRequested = false;
    return requested;
}

bool PauseMenuMgr::consumeOpenLoadMenuRequest()
{
    const bool requested = openLoadMenuRequested;
    openLoadMenuRequested = false;
    return requested;
}

void PauseMenuMgr::setStatusMessage(const std::string& message, float durationSeconds)
{
    statusMessage = message;
    statusMessageTimer = durationSeconds;
}

void PauseMenuMgr::applyAudioConfig() const
{
    if (audioManager != nullptr && gameConfig != nullptr)
        audioManager->setVolumes(gameConfig->audio);
}

void PauseMenuMgr::persistGameConfig() const
{
    if (gameConfig == nullptr)
        return;

    saveGameConfig(configPath, *gameConfig);
}

std::string PauseMenuMgr::resolutionLabel() const
{
    if (gameConfig == nullptr)
        return "Unknown";

    if (gameConfig->display.fullscreen)
        return "Fullscreen (Desktop)";

    for (int i = 0; i < kResolutionPresetCount; ++i)
    {
        const ResolutionPreset& preset = kResolutionPresets[i];
        if (preset.width == gameConfig->display.width && preset.height == gameConfig->display.height)
            return preset.label;
    }

    char buffer[32];
    snprintf(
        buffer,
        sizeof(buffer),
        "%d x %d",
        gameConfig->display.width,
        gameConfig->display.height);
    return buffer;
}

void PauseMenuMgr::cycleMaxNamedSaves()
{
    if (gameConfig == nullptr)
        return;

    int currentIndex = 0;
    for (int i = 0; i < kMaxSavePresetCount; ++i)
    {
        if (kMaxSavePresets[i] == gameConfig->saves.maxNamedSaves)
        {
            currentIndex = i;
            break;
        }
    }

    const int nextIndex = (currentIndex + 1) % kMaxSavePresetCount;
    gameConfig->saves.maxNamedSaves = kMaxSavePresets[nextIndex];
    persistGameConfig();
}

void PauseMenuMgr::cycleResolutionPreset()
{
    if (gameConfig == nullptr)
        return;

    int currentIndex = 0;
    for (int i = 0; i < kResolutionPresetCount; ++i)
    {
        const ResolutionPreset& preset = kResolutionPresets[i];
        if (preset.width == gameConfig->display.width && preset.height == gameConfig->display.height)
        {
            currentIndex = i;
            break;
        }
    }

    const int nextIndex = (currentIndex + 1) % kResolutionPresetCount;
    gameConfig->display.width = kResolutionPresets[nextIndex].width;
    gameConfig->display.height = kResolutionPresets[nextIndex].height;
    gameConfig->display.fullscreen = false;

    if (onDisplaySettingsChanged)
        onDisplaySettingsChanged();
}

void PauseMenuMgr::toggleFullscreen()
{
    if (gameConfig == nullptr)
        return;

    gameConfig->display.fullscreen = !gameConfig->display.fullscreen;
    if (onDisplaySettingsChanged)
        onDisplaySettingsChanged();
}

float PauseMenuMgr::clickHoldSliderValue() const
{
    if (gameConfig == nullptr)
        return 0.0f;

    return (gameConfig->input.clickHoldSeconds - 0.05f) / 0.45f;
}

void PauseMenuMgr::setClickHoldFromSlider(float normalizedValue)
{
    if (gameConfig == nullptr)
        return;

    normalizedValue = std::max(0.0f, std::min(1.0f, normalizedValue));
    gameConfig->input.clickHoldSeconds = 0.05f + normalizedValue * 0.45f;
    persistGameConfig();
    if (onInputSettingsChanged)
        onInputSettingsChanged();
}

float PauseMenuMgr::readSliderValue(const Rectangle& track) const
{
    const float innerX = track.x + 10.0f;
    const float innerWidth = track.width - 20.0f;
    if (innerWidth <= 0.0f)
        return 0.0f;

    const Vector2 mouse = GetMousePosition();
    const float t = (mouse.x - innerX) / innerWidth;
    return std::max(0.0f, std::min(1.0f, t));
}

void PauseMenuMgr::handleConfigScrollInput()
{
    const Rectangle content = getConfigContentBounds();
    ScrollPanel::applyWheelScroll(
        configScrollY,
        content,
        getConfigContentHeight(),
        content.height,
        36.0f);
}

void PauseMenuMgr::handleSliderInput()
{
    if (gameConfig == nullptr)
        return;

    const std::vector<ConfigRow>& rows = getConfigRows();
    const bool mouseDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);

    if (!mouseDown)
    {
        if (activeSliderIndex >= 0)
            persistGameConfig();
        activeSliderIndex = -1;
        return;
    }

    if (activeSliderIndex < 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        for (int rowIndex = 0; rowIndex < (int)rows.size(); ++rowIndex)
        {
            const ConfigRow& row = rows[(size_t)rowIndex];
            if (row.type != ConfigRowType::Slider)
                continue;

            const Rectangle rowBounds = getConfigRowBounds(rowIndex);
            const Rectangle track = {
                rowBounds.x,
                rowBounds.y + 20.0f,
                rowBounds.width,
                28.0f
            };

            if (CheckCollisionPointRec(GetMousePosition(), track))
            {
                activeSliderIndex = row.sliderIndex;
                break;
            }
        }
    }

    if (activeSliderIndex < 0 || activeSliderIndex >= kSliderCount)
        return;

    for (int rowIndex = 0; rowIndex < (int)rows.size(); ++rowIndex)
    {
        const ConfigRow& row = rows[(size_t)rowIndex];
        if (row.type != ConfigRowType::Slider || row.sliderIndex != activeSliderIndex)
            continue;

        const Rectangle rowBounds = getConfigRowBounds(rowIndex);
        const Rectangle track = {
            rowBounds.x,
            rowBounds.y + 20.0f,
            rowBounds.width,
            28.0f
        };

        const float normalizedValue = readSliderValue(track);
        if (activeSliderIndex == kClickHoldSliderIndex)
            setClickHoldFromSlider(normalizedValue);
        else if (activeSliderIndex >= 0 && activeSliderIndex < kAudioSliderCount)
        {
            float* targets[] = {
                &gameConfig->audio.master,
                &gameConfig->audio.music,
                &gameConfig->audio.ambient,
                &gameConfig->audio.sfx
            };
            *targets[activeSliderIndex] = normalizedValue;
            applyAudioConfig();
        }
        break;
    }
}

void PauseMenuMgr::handleConfigButtonInput()
{
    if (gameConfig == nullptr || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;

    const std::vector<ConfigRow>& rows = getConfigRows();
    for (int rowIndex = 0; rowIndex < (int)rows.size(); ++rowIndex)
    {
        const ConfigRow& row = rows[(size_t)rowIndex];
        if (row.type != ConfigRowType::ToggleButton && row.type != ConfigRowType::CycleButton)
            continue;

        const Rectangle rowBounds = getConfigRowBounds(rowIndex);
        const Rectangle buttonBounds = {
            rowBounds.x,
            rowBounds.y + 4.0f,
            rowBounds.width,
            40.0f
        };

        if (!CheckCollisionPointRec(GetMousePosition(), buttonBounds))
            continue;

        if (row.type == ConfigRowType::CycleButton)
        {
            if (std::string(row.label) == "Max Number of Saves")
                cycleMaxNamedSaves();
            else
                cycleResolutionPreset();
        }
        else
            toggleFullscreen();
        break;
    }
}

void PauseMenuMgr::handleMainInput()
{
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        Button& button = buttons[i];
        if (!button.isEnabled())
            continue;

        if (CheckCollisionPointRec(GetMousePosition(), button.getBounds()))
            button.setState(IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? PRESSED : HOVERED);
        else
            button.setState(NORMAL);

        if (!button.isClicked())
            continue;

        switch (i)
        {
            case 0:
                openLoadMenuRequested = true;
                break;
            case 1:
                openSaveMenuRequested = true;
                break;
            case 2:
                showConfigPanel();
                break;
            case 3:
                quitRequested = true;
                break;
            default:
                break;
        }
    }
}

void PauseMenuMgr::handleConfigInput()
{
    handleConfigScrollInput();
    handleSliderInput();
    handleConfigButtonInput();

    if (buttons.empty())
        return;

    Button& backButton = buttons[0];
    if (CheckCollisionPointRec(GetMousePosition(), backButton.getBounds()))
        backButton.setState(IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? PRESSED : HOVERED);
    else
        backButton.setState(NORMAL);

    if (backButton.isClicked())
    {
        persistGameConfig();
        showMainPanel();
    }
}

void PauseMenuMgr::update(float deltaSeconds)
{
    if (!open)
        return;

    if (statusMessageTimer > 0.0f)
    {
        statusMessageTimer -= deltaSeconds;
        if (statusMessageTimer <= 0.0f)
            statusMessage.clear();
    }

    if (panel == PauseMenuPanel::Config)
        handleConfigInput();
    else
        handleMainInput();
}

void PauseMenuMgr::drawPanelFrame(const char* title) const
{
    const Rectangle panel = getPanelBounds();
    modalPanel.drawOverlay();

    const Color panelBorder = (uiBackdrop != nullptr) ? uiBackdrop->panelBorderColor() : kPanelBorder;
    if (uiBackdrop != nullptr)
        uiBackdrop->drawPanel(panel, 0.05f, 12);
    else
        DrawRectangleRounded(panel, 0.05f, 12, kPanelFill);
    DrawRoundedBorder(panel, 0.05f, 12, 3.0f, panelBorder);

    const Rectangle accentBar = {
        panel.x + 12.0f,
        panel.y + 12.0f,
        panel.width - 24.0f,
        4.0f
    };
    if (uiBackdrop != nullptr)
        uiBackdrop->drawAccentBar(accentBar);
    else
        DrawRectangleRounded(accentBar, 1.0f, 4, kPanelAccent);

    const Vector2 titleSize = MeasureTextEx(uiFont, title, 24.0f, 1);
    DrawTextEx(
        uiFont,
        title,
        {
            panel.x + (panel.width - titleSize.x) * 0.5f,
            panel.y + 34.0f
        },
        24.0f,
        1,
        {228, 220, 198, 255});
}

void PauseMenuMgr::drawSliderRow(
    float x,
    float y,
    float width,
    const char* label,
    float value,
    const char* valueText) const
{
    DrawTextEx(uiFont, label, { x, y }, 14.0f, 1, kSectionLabel);

    const Rectangle track = {
        x,
        y + 20.0f,
        width,
        28.0f
    };
    DrawRectangleRounded(track, 0.18f, 8, kStatusTrack);
    DrawRoundedBorder(track, 0.18f, 8, 2.0f, kPanelBorder);

    const float fillWidth = (track.width - 8.0f) * value;
    if (fillWidth > 0.0f)
    {
        const Rectangle fill = {
            track.x + 4.0f,
            track.y + 4.0f,
            fillWidth,
            track.height - 8.0f
        };
        DrawRectangleRounded(fill, 0.18f, 8, kSliderFill);
    }

    const Vector2 textSize = MeasureTextEx(uiFont, valueText, 13.0f, 1);
    DrawTextEx(
        uiFont,
        valueText,
        {
            track.x + (track.width - textSize.x) * 0.5f,
            track.y + (track.height - textSize.y) * 0.5f
        },
        13.0f,
        1,
        {228, 220, 198, 220});
}

void PauseMenuMgr::drawConfigPanel() const
{
    if (gameConfig == nullptr)
        return;

    const Rectangle content = getConfigContentBounds();
    BeginScissorMode(
        (int)content.x,
        (int)content.y,
        (int)content.width,
        (int)content.height);

    const std::vector<ConfigRow>& rows = getConfigRows();
    for (int rowIndex = 0; rowIndex < (int)rows.size(); ++rowIndex)
    {
        const ConfigRow& row = rows[(size_t)rowIndex];
        const Rectangle rowBounds = getConfigRowBounds(rowIndex);

        if (rowBounds.y + rowBounds.height < content.y || rowBounds.y > content.y + content.height)
            continue;

        if (row.type == ConfigRowType::SectionHeader)
        {
            DrawTextEx(
                uiFont,
                row.label,
                { rowBounds.x, rowBounds.y + 6.0f },
                15.0f,
                1,
                {228, 220, 198, 255});
            continue;
        }

        if (row.type == ConfigRowType::Slider)
        {
            float sliderValue = 0.0f;
            char valueText[32];

            if (row.sliderIndex == kClickHoldSliderIndex)
            {
                sliderValue = clickHoldSliderValue();
                snprintf(
                    valueText,
                    sizeof(valueText),
                    "%.0f ms",
                    gameConfig->input.clickHoldSeconds * 1000.0f);
            }
            else if (row.sliderIndex >= 0 && row.sliderIndex < kAudioSliderCount)
            {
                const float* values[] = {
                    &gameConfig->audio.master,
                    &gameConfig->audio.music,
                    &gameConfig->audio.ambient,
                    &gameConfig->audio.sfx
                };
                sliderValue = *values[row.sliderIndex];
                snprintf(valueText, sizeof(valueText), "%d%%", (int)(sliderValue * 100.0f));
            }

            drawSliderRow(
                rowBounds.x,
                rowBounds.y,
                rowBounds.width,
                row.label,
                sliderValue,
                valueText);
            continue;
        }

        const Rectangle buttonBounds = {
            rowBounds.x,
            rowBounds.y + 4.0f,
            rowBounds.width,
            40.0f
        };
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), buttonBounds);
        DrawRectangleRounded(
            buttonBounds,
            0.18f,
            8,
            hovered ? Color{78, 72, 92, 255} : Color{54, 50, 64, 255});
        DrawRoundedBorder(buttonBounds, 0.18f, 8, 2.0f, kPanelBorder);

        char buttonText[96];
        if (row.type == ConfigRowType::CycleButton)
        {
            if (std::string(row.label) == "Max Number of Saves")
            {
                snprintf(
                    buttonText,
                    sizeof(buttonText),
                    "%s: %d",
                    row.label,
                    gameConfig->saves.maxNamedSaves);
            }
            else
            {
                snprintf(
                    buttonText,
                    sizeof(buttonText),
                    "%s: %s",
                    row.label,
                    resolutionLabel().c_str());
            }
        }
        else
        {
            snprintf(
                buttonText,
                sizeof(buttonText),
                "%s: %s",
                row.label,
                gameConfig->display.fullscreen ? "On" : "Off");
        }

        const Vector2 textSize = MeasureTextEx(uiFont, buttonText, 15.0f, 1);
        DrawTextEx(
            uiFont,
            buttonText,
            {
                buttonBounds.x + (buttonBounds.width - textSize.x) * 0.5f,
                buttonBounds.y + (buttonBounds.height - textSize.y) * 0.5f
            },
            15.0f,
            1,
            {228, 220, 198, 255});
    }

    EndScissorMode();
}

void PauseMenuMgr::drawStatusMessage() const
{
    if (statusMessage.empty())
        return;

    const Rectangle panel = getPanelBounds();
    const Vector2 textSize = MeasureTextEx(uiFont, statusMessage.c_str(), 14.0f, 1);
    DrawTextEx(
        uiFont,
        statusMessage.c_str(),
        {
            panel.x + (panel.width - textSize.x) * 0.5f,
            panel.y + panel.height - 36.0f
        },
        14.0f,
        1,
        kSectionLabel);
}

void PauseMenuMgr::draw() const
{
    if (!open)
        return;

    if (panel == PauseMenuPanel::Config)
    {
        drawPanelFrame("CONFIG");
        drawConfigPanel();
    }
    else
    {
        drawPanelFrame("PAUSED");
    }

    for (const Button& button : buttons)
        button.draw();

    drawStatusMessage();
}

}