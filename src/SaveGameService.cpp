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

#include "SaveGameService.h"

namespace timberline_engine
{

bool SaveGameService::quickSave(const SavedGameState& state) const
{
    SaveSlotMetadata metadata;
    metadata.name = "Quick Save";
    metadata.timestampIso = currentTimestampIso(metadata.unixTime);
    metadata.isQuickSave = true;
    return writeSaveFile(quickSavePath(), state, metadata);
}

bool SaveGameService::namedSave(
    const SavedGameState& state,
    const std::string& saveName,
    int maxNamedSaves) const
{
    if (saveName.empty())
        return false;

    enforceNamedSaveLimit(maxNamedSaves);

    SaveSlotMetadata metadata;
    metadata.name = saveName;
    metadata.timestampIso = currentTimestampIso(metadata.unixTime);
    metadata.isQuickSave = false;
    return writeSaveFile(buildNamedSavePath(metadata.unixTime), state, metadata);
}

bool SaveGameService::loadFromPath(const std::string& path, SavedGameState& outState) const
{
    return readSaveFile(path, outState);
}

std::vector<SaveSlotListing> SaveGameService::listSlots() const
{
    return listSaveSlots();
}

}