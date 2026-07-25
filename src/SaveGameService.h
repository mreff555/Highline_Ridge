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

#ifndef SAVE_GAME_SERVICE_H
#define SAVE_GAME_SERVICE_H

#include <SaveGame.h>
#include <string>

namespace timberline_engine
{

class SaveGameService
{
    public:
    bool quickSave(const SavedGameState& state) const;
    bool namedSave(const SavedGameState& state, const std::string& saveName, int maxNamedSaves) const;
    bool loadFromPath(const std::string& path, SavedGameState& outState) const;
    std::vector<SaveSlotListing> listSlots() const;
};

}

#endif /* SAVE_GAME_SERVICE_H */