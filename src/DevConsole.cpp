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

#include "DevConsole.h"

#include <RaylibCompat.h>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace timberline_engine
{

namespace
{
const char* kGiveItemCommand = "give-item";
const float kFontSize = 20.0f;
const float kRowHeight = 24.0f;
}

std::string DevConsole::toLower(std::string value)
{
    for (char& ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

bool DevConsole::matchesFilter(const std::string& haystack, const std::string& filterLower)
{
    if (filterLower.empty())
        return true;
    return toLower(haystack).find(filterLower) != std::string::npos;
}

void DevConsole::toggle()
{
    open = !open;
    if (open)
    {
        input.clear();
        statusMessage.clear();
        suggestions.clear();
        selectedIndex = 0;
        scrollOffset = 0;
        // Drain the opening ` / ~ key so it is not inserted into the prompt.
        while (GetCharPressed() > 0)
        {
        }
        rebuildSuggestions();
    }
}

void DevConsole::close()
{
    open = false;
}

bool DevConsole::parseGiveItemArg(std::string& outArg) const
{
    outArg.clear();
    std::string trimmed = input;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();

    const std::string lower = toLower(trimmed);
    const std::string prefix = std::string(kGiveItemCommand);
    if (lower.rfind(prefix, 0) != 0)
        return false;

    if (trimmed.size() == prefix.size())
    {
        outArg.clear();
        return true;
    }

    if (!std::isspace(static_cast<unsigned char>(trimmed[prefix.size()])))
        return false;

    outArg = trimmed.substr(prefix.size());
    while (!outArg.empty() && std::isspace(static_cast<unsigned char>(outArg.front())))
        outArg.erase(outArg.begin());
    return true;
}

void DevConsole::rebuildSuggestions()
{
    suggestions.clear();
    if (itemDatabase == nullptr)
        return;

    std::string arg;
    if (!parseGiveItemArg(arg))
        return;

    const std::string filterLower = toLower(arg);
    for (const auto& entry : itemDatabase->allDefs())
    {
        const std::string& id = entry.first;
        const std::string& name = entry.second.name;
        if (!matchesFilter(id, filterLower) && !matchesFilter(name, filterLower))
            continue;

        Suggestion suggestion;
        suggestion.id = id;
        suggestion.label = name.empty() ? id : (id + "  —  " + name);
        suggestions.push_back(std::move(suggestion));
    }

    std::sort(
        suggestions.begin(),
        suggestions.end(),
        [](const Suggestion& a, const Suggestion& b) { return a.id < b.id; });

    if (selectedIndex >= static_cast<int>(suggestions.size()))
        selectedIndex = suggestions.empty() ? 0 : static_cast<int>(suggestions.size()) - 1;
    if (selectedIndex < 0)
        selectedIndex = 0;
    ensureSelectionVisible();
}

void DevConsole::moveSelection(int delta)
{
    if (suggestions.empty())
        return;
    const int count = static_cast<int>(suggestions.size());
    selectedIndex = (selectedIndex + delta) % count;
    if (selectedIndex < 0)
        selectedIndex += count;
    ensureSelectionVisible();
}

void DevConsole::ensureSelectionVisible()
{
    if (suggestions.empty())
    {
        scrollOffset = 0;
        return;
    }
    if (selectedIndex < scrollOffset)
        scrollOffset = selectedIndex;
    if (selectedIndex >= scrollOffset + visibleRows)
        scrollOffset = selectedIndex - visibleRows + 1;
    const int maxScroll = std::max(0, static_cast<int>(suggestions.size()) - visibleRows);
    scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
}

void DevConsole::autoPopulateSelection()
{
    if (suggestions.empty()
        || selectedIndex < 0
        || selectedIndex >= static_cast<int>(suggestions.size()))
        return;

    input = std::string(kGiveItemCommand) + " " + suggestions[static_cast<size_t>(selectedIndex)].id;
    statusMessage.clear();
    rebuildSuggestions();
}

void DevConsole::executeCommand()
{
    std::string trimmed = input;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();

    if (trimmed.empty())
    {
        statusMessage = "Commands: give-item <item id>";
        return;
    }

    std::string arg;
    if (!parseGiveItemArg(arg))
    {
        statusMessage = "Unknown command. Try: give-item <item id>";
        return;
    }

    if (arg.empty())
    {
        statusMessage = "Usage: give-item <item id>";
        rebuildSuggestions();
        return;
    }

    // Prefer exact id; otherwise use highlighted suggestion if it uniquely matches.
    std::string itemId = arg;
    if (itemDatabase == nullptr || !itemDatabase->hasDef(itemId))
    {
        if (!suggestions.empty()
            && selectedIndex >= 0
            && selectedIndex < static_cast<int>(suggestions.size())
            && matchesFilter(suggestions[static_cast<size_t>(selectedIndex)].id, toLower(arg)))
        {
            itemId = suggestions[static_cast<size_t>(selectedIndex)].id;
        }
        else
        {
            statusMessage = "Unknown item: " + arg;
            return;
        }
    }

    if (!giveItemHandler)
    {
        statusMessage = "Give-item handler not set";
        return;
    }

    std::string message;
    giveItemHandler(itemId, message);
    statusMessage = message;
    input = std::string(kGiveItemCommand) + " ";
    rebuildSuggestions();
}

void DevConsole::update()
{
    if (!open)
        return;

    if (IsKeyPressed(KEY_ESCAPE))
    {
        close();
        return;
    }

    if (IsKeyPressed(KEY_UP))
        moveSelection(-1);
    if (IsKeyPressed(KEY_DOWN))
        moveSelection(1);

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && !suggestions.empty())
    {
        moveSelection(wheel > 0.0f ? -1 : 1);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !suggestionRowBounds.empty())
    {
        const Vector2 mouse = GetMousePosition();
        for (size_t i = 0; i < suggestionRowBounds.size(); ++i)
        {
            if (!CheckCollisionPointRec(mouse, suggestionRowBounds[i]))
                continue;
            selectedIndex = scrollOffset + static_cast<int>(i);
            autoPopulateSelection();
            return;
        }
    }

    if (IsKeyPressed(KEY_TAB))
    {
        autoPopulateSelection();
        return;
    }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        std::string arg;
        const bool isGive = parseGiveItemArg(arg);
        if (isGive && !suggestions.empty())
        {
            const std::string& selectedId =
                suggestions[static_cast<size_t>(selectedIndex)].id;
            if (arg != selectedId)
            {
                autoPopulateSelection();
                return;
            }
        }
        executeCommand();
        return;
    }

    if (IsKeyPressed(KEY_BACKSPACE) && !input.empty())
    {
        input.pop_back();
        rebuildSuggestions();
    }

    for (;;)
    {
        const int codepoint = GetCharPressed();
        if (codepoint <= 0)
            break;
        // Printable ASCII only for the console.
        if (codepoint >= 32 && codepoint < 127)
        {
            input.push_back(static_cast<char>(codepoint));
            rebuildSuggestions();
        }
    }
}

void DevConsole::draw(int screenWidth, int screenHeight) const
{
    if (!open)
        return;

    suggestionRowBounds.clear();

    const float margin = 16.0f;
    const float panelWidth = std::min(720.0f, static_cast<float>(screenWidth) - margin * 2.0f);
    const int rows = std::min(visibleRows, static_cast<int>(suggestions.size()));
    const float listHeight = rows > 0 ? (kRowHeight * static_cast<float>(rows) + 8.0f) : 0.0f;
    const float inputHeight = 36.0f;
    const float statusHeight = statusMessage.empty() ? 0.0f : 26.0f;
    const float headerHeight = 28.0f;
    const float panelHeight = headerHeight + inputHeight + listHeight + statusHeight + 18.0f;
    const Rectangle panel = {
        margin,
        static_cast<float>(screenHeight) - panelHeight - margin,
        panelWidth,
        panelHeight
    };

    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.35f));
    DrawRectangleRounded(panel, 0.04f, 8, Color{28, 26, 34, 240});
    DrawRoundedBorder(panel, 0.04f, 8, 2.0f, Color{168, 138, 72, 255});

    const Font useFont = font.texture.id != 0 ? font : GetFontDefault();
    const Font useBold = boldFont.texture.id != 0 ? boldFont : useFont;

    DrawTextEx(
        useBold,
        "Developer console  (~ to close)",
        { panel.x + 12.0f, panel.y + 8.0f },
        18.0f,
        1.0f,
        Color{196, 178, 120, 255});

    const Rectangle inputBox = {
        panel.x + 12.0f,
        panel.y + headerHeight,
        panel.width - 24.0f,
        inputHeight
    };
    DrawRectangleRounded(inputBox, 0.08f, 6, Color{18, 16, 22, 255});
    DrawRoundedBorder(inputBox, 0.08f, 6, 1.0f, Color{96, 78, 48, 255});

    const std::string prompt = "> " + input + (static_cast<int>(GetTime() * 2.0) % 2 == 0 ? "_" : " ");
    DrawTextEx(
        useFont,
        prompt.c_str(),
        { inputBox.x + 10.0f, inputBox.y + 8.0f },
        kFontSize,
        1.0f,
        Color{228, 220, 198, 255});

    float listTop = inputBox.y + inputBox.height + 6.0f;
    if (rows > 0)
    {
        const Rectangle listBox = {
            panel.x + 12.0f,
            listTop,
            panel.width - 24.0f,
            listHeight
        };
        DrawRectangleRounded(listBox, 0.06f, 6, Color{22, 20, 28, 255});

        for (int row = 0; row < rows; ++row)
        {
            const int index = scrollOffset + row;
            if (index < 0 || index >= static_cast<int>(suggestions.size()))
                break;

            const Rectangle rowBounds = {
                listBox.x + 4.0f,
                listBox.y + 4.0f + kRowHeight * static_cast<float>(row),
                listBox.width - 8.0f,
                kRowHeight - 2.0f
            };
            suggestionRowBounds.push_back(rowBounds);

            const bool selected = index == selectedIndex;
            if (selected)
                DrawRectangleRounded(rowBounds, 0.1f, 4, Color{70, 58, 36, 255});

            DrawTextEx(
                useFont,
                suggestions[static_cast<size_t>(index)].label.c_str(),
                { rowBounds.x + 8.0f, rowBounds.y + 2.0f },
                18.0f,
                1.0f,
                selected ? Color{240, 228, 180, 255} : Color{180, 172, 156, 255});
        }

        listTop = listBox.y + listBox.height + 4.0f;
    }

    if (!statusMessage.empty())
    {
        DrawTextEx(
            useFont,
            statusMessage.c_str(),
            { panel.x + 14.0f, listTop + 2.0f },
            16.0f,
            1.0f,
            Color{168, 138, 72, 255});
    }
}

}
