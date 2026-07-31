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

#include "ThumbnailCache.h"

#include "EditorPaths.h"
#include "ImageCompression.h"
#include "PlatformPath.h"

#include <raylib.h>

#include <vector>

using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::loadTextureFromAssetFile;
using timberline_engine::pathJoin;

namespace timberline_editor
{

ThumbnailEntry& ThumbnailCache::getOrLoad(
    const std::string& sceneId,
    const timberline_engine::SceneDocument& scenes,
    const std::string& assetRoot,
    const std::string& resourceDir)
{
    ThumbnailEntry& entry = entries[sceneId];
    if (entry.loaded || entry.missing)
        return entry;

    const std::string imagePath = scenes.getSceneImagePath(sceneId);
    if (imagePath.empty())
    {
        entry.missing = true;
        return entry;
    }

    // Match SceneLoader: prefer .png.xz (git-stored) then uncompressed paths.
    const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, imagePath);
    for (const std::string& path : paths)
    {
        const std::string compressedPath = compressedAssetPath(path);
        if (FileExists(compressedPath.c_str()) &&
            loadTextureFromAssetFile(compressedPath, entry.texture))
        {
            entry.loaded = true;
            return entry;
        }

        if (FileExists(path.c_str()) &&
            loadTextureFromAssetFile(path, entry.texture))
        {
            entry.loaded = true;
            return entry;
        }
    }

    // Also search under the resource directory itself (resourceDir may be a
    // symlink beside the binary while image paths are resources/images/...).
    const std::string underResources = pathJoin(parentDirectory(resourceDir), imagePath);
    if (!underResources.empty())
    {
        const std::string compressedPath = compressedAssetPath(underResources);
        if (FileExists(compressedPath.c_str()) &&
            loadTextureFromAssetFile(compressedPath, entry.texture))
        {
            entry.loaded = true;
            return entry;
        }

        if (FileExists(underResources.c_str()) &&
            loadTextureFromAssetFile(underResources, entry.texture))
        {
            entry.loaded = true;
            return entry;
        }
    }

    entry.missing = true;
    return entry;
}

void ThumbnailCache::clear()
{
    for (std::map<std::string, ThumbnailEntry>::iterator it = entries.begin();
         it != entries.end();
         ++it)
    {
        if (it->second.loaded && it->second.texture.id != 0)
            UnloadTexture(it->second.texture);
    }
    entries.clear();
}

} // namespace timberline_editor
