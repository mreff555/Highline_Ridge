#include "SaveLoadMenuMgr.h"
#include <RaylibCompat.h>
#include <algorithm>
#include <cctype>

namespace testgame
{

namespace
{
    const Color kPanelFill = {28, 26, 34, 255};
    const Color kPanelBorder = {168, 138, 72, 255};
    const Color kPanelAccent = {96, 78, 48, 255};
    const Color kSectionLabel = {132, 122, 104, 255};
    const Color kOverlayDim = {8, 8, 12, 210};
    const Color kFieldFill = {40, 38, 50, 255};
    const Color kRowFill = {54, 50, 64, 255};
    const Color kRowSelected = {62, 52, 34, 255};
    const Color kRowHover = {72, 64, 48, 255};
    const float kRowHeight = 42.0f;
    const size_t kMaxSaveNameLength = 40;
}

SaveLoadMenuMgr::SaveLoadMenuMgr(int screenWidth_, int screenHeight_, Font uiFont_)
    : screenWidth(screenWidth_),
      screenHeight(screenHeight_),
      uiFont(uiFont_),
      buttonStyle{
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
      }
{
    layoutButtons();
}

void SaveLoadMenuMgr::setScreenSize(int width, int height)
{
    screenWidth = width;
    screenHeight = height;
    if (isOpen())
        layoutButtons();
}

Rectangle SaveLoadMenuMgr::getPanelBounds() const
{
    const float panelWidth = 560.0f;
    const float panelHeight = mode == SaveLoadMenuMode::Load ? 560.0f : 360.0f;
    return {
        (screenWidth - panelWidth) * 0.5f,
        (screenHeight - panelHeight) * 0.5f,
        panelWidth,
        panelHeight
    };
}

Rectangle SaveLoadMenuMgr::getContentBounds() const
{
    const Rectangle panel = getPanelBounds();
    return {
        panel.x + 28.0f,
        panel.y + 88.0f,
        panel.width - 56.0f,
        panel.height - 176.0f
    };
}

void SaveLoadMenuMgr::layoutButtons()
{
    buttons.clear();
    const Rectangle panel = getPanelBounds();
    const float buttonWidth = (panel.width - 72.0f - 14.0f) * 0.5f;
    const float buttonHeight = 48.0f;
    const float startX = panel.x + 36.0f;
    const float startY = panel.y + panel.height - 88.0f;

    if (mode == SaveLoadMenuMode::Save)
    {
        buttons.push_back(Button(
            "Save",
            { startX, startY },
            { buttonWidth, buttonHeight },
            uiFont,
            buttonStyle));
        buttons.push_back(Button(
            "Back",
            { startX + buttonWidth + 14.0f, startY },
            { buttonWidth, buttonHeight },
            uiFont,
            buttonStyle));
        buttons[0].setEnabled(!saveNameInput.empty());
    }
    else if (mode == SaveLoadMenuMode::Load)
    {
        buttons.push_back(Button(
            "Load",
            { startX, startY },
            { buttonWidth, buttonHeight },
            uiFont,
            buttonStyle));
        buttons.push_back(Button(
            "Back",
            { startX + buttonWidth + 14.0f, startY },
            { buttonWidth, buttonHeight },
            uiFont,
            buttonStyle));
        buttons[0].setEnabled(selectedLoadIndex >= 0 && selectedLoadIndex < (int)loadSlots.size());
    }
}

void SaveLoadMenuMgr::openSaveMenu()
{
    mode = SaveLoadMenuMode::Save;
    saveNameInput.clear();
    backRequested = false;
    namedSaveRequested = false;
    loadSlotRequested = false;
    layoutButtons();
}

void SaveLoadMenuMgr::openLoadMenu()
{
    mode = SaveLoadMenuMode::Load;
    refreshLoadList();
    selectedLoadIndex = loadSlots.empty() ? -1 : 0;
    loadScrollY = 0.0f;
    backRequested = false;
    namedSaveRequested = false;
    loadSlotRequested = false;
    layoutButtons();
}

void SaveLoadMenuMgr::closeMenu()
{
    mode = SaveLoadMenuMode::Closed;
    saveNameInput.clear();
    loadSlots.clear();
    selectedLoadIndex = -1;
    loadScrollY = 0.0f;
    buttons.clear();
}

void SaveLoadMenuMgr::refreshLoadList()
{
    loadSlots = listSaveSlots();
}

bool SaveLoadMenuMgr::consumeBackRequest()
{
    const bool requested = backRequested;
    backRequested = false;
    return requested;
}

bool SaveLoadMenuMgr::consumeNamedSaveRequest(std::string& outSaveName)
{
    if (!namedSaveRequested)
        return false;

    outSaveName = saveNameInput;
    namedSaveRequested = false;
    return !outSaveName.empty();
}

bool SaveLoadMenuMgr::consumeLoadSlotRequest(std::string& outFilePath)
{
    if (!loadSlotRequested)
        return false;

    outFilePath = pendingLoadPath;
    pendingLoadPath.clear();
    loadSlotRequested = false;
    return !outFilePath.empty();
}

std::string SaveLoadMenuMgr::truncateNameForRow(
    const std::string& name,
    float maxWidth,
    float fontSize) const
{
    if (name.empty() || maxWidth <= 0.0f)
        return name;

    const Vector2 fullSize = MeasureTextEx(uiFont, name.c_str(), fontSize, 1);
    if (fullSize.x <= maxWidth)
        return name;

    const std::string ellipsis = "...";
    const Vector2 ellipsisSize = MeasureTextEx(uiFont, ellipsis.c_str(), fontSize, 1);
    const float targetWidth = std::max(0.0f, maxWidth - ellipsisSize.x);

    std::string truncated;
    truncated.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i)
    {
        truncated.push_back(name[i]);
        const Vector2 size = MeasureTextEx(uiFont, truncated.c_str(), fontSize, 1);
        if (size.x > targetWidth)
        {
            if (!truncated.empty())
                truncated.pop_back();
            break;
        }
    }

    return truncated + ellipsis;
}

void SaveLoadMenuMgr::handleTextInput()
{
    if (mode != SaveLoadMenuMode::Save)
        return;

    int key = GetCharPressed();
    while (key > 0)
    {
        if (key >= 32 && key <= 125 && saveNameInput.size() < kMaxSaveNameLength)
            saveNameInput.push_back(static_cast<char>(key));
        key = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && !saveNameInput.empty())
        saveNameInput.pop_back();

    if (!buttons.empty())
        buttons[0].setEnabled(!saveNameInput.empty());
}

void SaveLoadMenuMgr::handleSaveInput()
{
    handleTextInput();

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

        if (i == 0)
            namedSaveRequested = true;
        else
            backRequested = true;
    }
}

void SaveLoadMenuMgr::handleLoadInput()
{
    const Rectangle content = getContentBounds();
    const Vector2 mousePos = GetMousePosition();
    const float contentHeight = loadSlots.empty()
        ? 0.0f
        : loadSlots.size() * kRowHeight;
    const float maxScroll = std::max(0.0f, contentHeight - content.height);

    if (CheckCollisionPointRec(mousePos, content))
        loadScrollY -= GetMouseWheelMove() * kRowHeight;
    loadScrollY = std::max(0.0f, std::min(loadScrollY, maxScroll));

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        for (size_t i = 0; i < loadSlots.size(); ++i)
        {
            const float rowY = content.y + (float)i * kRowHeight - loadScrollY;
            const Rectangle rowBounds = {
                content.x,
                rowY,
                content.width,
                kRowHeight - 4.0f
            };

            if (rowY + rowBounds.height < content.y || rowY > content.y + content.height)
                continue;

            if (CheckCollisionPointRec(mousePos, rowBounds))
            {
                selectedLoadIndex = (int)i;
                if (!buttons.empty())
                    buttons[0].setEnabled(true);
                break;
            }
        }
    }

    for (size_t i = 0; i < buttons.size(); ++i)
    {
        Button& button = buttons[i];
        if (!button.isEnabled())
            continue;

        if (CheckCollisionPointRec(mousePos, button.getBounds()))
            button.setState(IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? PRESSED : HOVERED);
        else
            button.setState(NORMAL);

        if (!button.isClicked())
            continue;

        if (i == 0 && selectedLoadIndex >= 0 && selectedLoadIndex < (int)loadSlots.size())
        {
            pendingLoadPath = loadSlots[(size_t)selectedLoadIndex].filePath;
            loadSlotRequested = true;
        }
        else
            backRequested = true;
    }
}

void SaveLoadMenuMgr::update()
{
    if (!isOpen())
        return;

    if (mode == SaveLoadMenuMode::Save)
        handleSaveInput();
    else if (mode == SaveLoadMenuMode::Load)
        handleLoadInput();
}

void SaveLoadMenuMgr::drawPanelFrame(const char* title) const
{
    const Rectangle panel = getPanelBounds();

    DrawRectangle(0, 0, screenWidth, screenHeight, kOverlayDim);
    DrawRectangleRounded(panel, 0.05f, 12, kPanelFill);
    DrawRoundedBorder(panel, 0.05f, 12, 3.0f, kPanelBorder);

    const Rectangle accentBar = {
        panel.x + 12.0f,
        panel.y + 12.0f,
        panel.width - 24.0f,
        4.0f
    };
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

void SaveLoadMenuMgr::drawSaveNameField() const
{
    const Rectangle content = getContentBounds();
    DrawTextEx(
        uiFont,
        "Save name",
        { content.x, content.y },
        15.0f,
        1,
        kSectionLabel);

    const Rectangle field = {
        content.x,
        content.y + 28.0f,
        content.width,
        44.0f
    };
    DrawRectangleRounded(field, 0.16f, 8, kFieldFill);
    DrawRoundedBorder(field, 0.16f, 8, 2.0f, kPanelBorder);

    const char* displayText = saveNameInput.empty() ? "Enter a name..." : saveNameInput.c_str();
    const Color textColor = saveNameInput.empty()
        ? Color{108, 102, 92, 255}
        : Color{228, 220, 198, 255};
    DrawTextEx(
        uiFont,
        displayText,
        { field.x + 14.0f, field.y + 12.0f },
        17.0f,
        1,
        textColor);
}

void SaveLoadMenuMgr::drawLoadRows() const
{
    const Rectangle content = getContentBounds();
    if (loadSlots.empty())
    {
        DrawTextEx(
            uiFont,
            "No saved games found.",
            { content.x, content.y + 8.0f },
            16.0f,
            1,
            kSectionLabel);
        return;
    }

    BeginScissorMode(
        (int)content.x,
        (int)content.y,
        (int)content.width,
        (int)content.height);

    const float fontSize = 16.0f;
    const float rowPadX = 12.0f;
    const float minGap = MeasureTextEx(uiFont, "    ", fontSize, 1).x;

    for (size_t i = 0; i < loadSlots.size(); ++i)
    {
        const float rowY = content.y + (float)i * kRowHeight - loadScrollY;
        const Rectangle rowBounds = {
            content.x,
            rowY,
            content.width,
            kRowHeight - 4.0f
        };

        if (rowY + rowBounds.height < content.y || rowY > content.y + content.height)
            continue;

        const bool selected = (int)i == selectedLoadIndex;
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), rowBounds);
        const Color fill = selected ? kRowSelected : (hovered ? kRowHover : kRowFill);
        DrawRectangleRounded(rowBounds, 0.14f, 8, fill);
        DrawRoundedBorder(rowBounds, 0.14f, 8, 2.0f, selected ? kPanelBorder : kPanelAccent);

        const std::string timestampText = formatSaveTimestampForDisplay(loadSlots[i].metadata);
        const Vector2 timestampSize = MeasureTextEx(uiFont, timestampText.c_str(), fontSize, 1);
        const float timestampX = rowBounds.x + rowBounds.width - rowPadX - timestampSize.x;
        const float maxNameWidth = std::max(
            0.0f,
            timestampX - (rowBounds.x + rowPadX) - minGap);
        const std::string displayName = truncateNameForRow(
            loadSlots[i].metadata.name,
            maxNameWidth,
            fontSize);

        DrawTextEx(
            uiFont,
            displayName.c_str(),
            { rowBounds.x + rowPadX, rowBounds.y + 10.0f },
            fontSize,
            1,
            {228, 220, 198, 255});
        DrawTextEx(
            uiFont,
            timestampText.c_str(),
            { timestampX, rowBounds.y + 10.0f },
            fontSize,
            1,
            kSectionLabel);
    }

    EndScissorMode();
}

void SaveLoadMenuMgr::drawSavePanel() const
{
    drawSaveNameField();
}

void SaveLoadMenuMgr::drawLoadPanel() const
{
    drawLoadRows();
}

void SaveLoadMenuMgr::draw() const
{
    if (!isOpen())
        return;

    if (mode == SaveLoadMenuMode::Save)
    {
        drawPanelFrame("SAVE GAME");
        drawSavePanel();
    }
    else if (mode == SaveLoadMenuMode::Load)
    {
        drawPanelFrame("LOAD GAME");
        drawLoadPanel();
    }

    for (const Button& button : buttons)
        button.draw();
}

}