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

#ifndef TIMBERLINE_THUMBNAIL_CACHE_H
#define TIMBERLINE_THUMBNAIL_CACHE_H

#include "EditorTypes.h"
#include "SceneDocument.h"

#include <map>
#include <string>

namespace timberline_editor
{

// Lazy scene image thumbnails for the list and map cards.
class ThumbnailCache
{
public:
    // Load on first request; returns a stable entry (loaded or missing).
    ThumbnailEntry& getOrLoad(
        const std::string& sceneId,
        const timberline_engine::SceneDocument& scenes,
        const std::string& assetRoot,
        const std::string& resourceDir);

    void clear();

private:
    std::map<std::string, ThumbnailEntry> entries;
};

} // namespace timberline_editor

#endif /* TIMBERLINE_THUMBNAIL_CACHE_H */
