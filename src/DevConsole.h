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

#ifndef DEV_CONSOLE_H
#define DEV_CONSOLE_H

#include <ItemDatabase.h>
#include <raylib.h>
#include <functional>
#include <string>
#include <vector>

namespace timberline_engine
{

/**
 * In-game developer command console (toggle with ` / ~).
 * Only compiled when HIGHLINE_DEV_TOOLS is enabled.
 */
class DevConsole
{
    public:
    using GiveItemFn = std::function<bool(const std::string& itemId, std::string& message)>;

    void setItemDatabase(const ItemDatabase* database) { itemDatabase = database; }
    void setGiveItemHandler(GiveItemFn handler) { giveItemHandler = std::move(handler); }
    void setFonts(Font regular, Font bold)
    {
        font = regular;
        boldFont = bold.texture.id != 0 ? bold : regular;
    }

    bool isOpen() const { return open; }
    void toggle();
    void close();

    /** Consume input while open. Returns true if the console handled the frame. */
    void update();
    void draw(int screenWidth, int screenHeight) const;

    private:
    struct Suggestion
    {
        std::string id;
        std::string label;
    };

    void rebuildSuggestions();
    void moveSelection(int delta);
    void ensureSelectionVisible();
    void autoPopulateSelection();
    void executeCommand();
    bool parseGiveItemArg(std::string& outArg) const;
    static std::string toLower(std::string value);
    static bool matchesFilter(const std::string& haystack, const std::string& filterLower);

    bool open = false;
    std::string input;
    std::string statusMessage;
    std::vector<Suggestion> suggestions;
    int selectedIndex = 0;
    int scrollOffset = 0;
    int visibleRows = 8;

    const ItemDatabase* itemDatabase = nullptr;
    GiveItemFn giveItemHandler;
    Font font{};
    Font boldFont{};

    mutable std::vector<Rectangle> suggestionRowBounds;
};

}

#endif /* DEV_CONSOLE_H */
