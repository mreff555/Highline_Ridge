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

#ifndef TIMBERLINE_EDITOR_PATHS_H
#define TIMBERLINE_EDITOR_PATHS_H
#include <string>

namespace timberline_editor
{

std::string parentDirectory(const std::string& path);
std::string absolutePath(const std::string& path);
bool resourceDirectoryExists(const std::string& resourceDir);
bool scenesFileExists(const std::string& resourceDir);
bool findResourcesFromBase(
    const std::string& baseDir,
    std::string& outResourceDir,
    std::string& outAssetRoot);
bool resolveEditorPaths(std::string& outResourceDir, std::string& outAssetRoot);
bool ensureValidResourcePaths(std::string& resourceDir, std::string& assetRoot);

} // namespace timberline_editor

#endif
