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

#include "PlatformPath.h"

#include <filesystem>

namespace timberline_engine
{

namespace fs = std::filesystem;

std::string pathJoin(const std::string& base, const std::string& relative)
{
    if (base.empty())
        return relative;
    if (relative.empty())
        return base;

    return (fs::path(base) / fs::path(relative)).lexically_normal().string();
}

bool ensureParentDirectories(const std::string& filePath)
{
    const fs::path path(filePath);
    if (!path.has_parent_path())
        return true;

    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    return !error;
}

bool ensureDirectory(const std::string& directoryPath)
{
    if (directoryPath.empty())
        return false;

    std::error_code error;
    fs::create_directories(fs::path(directoryPath), error);
    return !error;
}

std::vector<std::string> listDirectoryFileNames(const std::string& directoryPath)
{
    std::vector<std::string> fileNames;
    std::error_code error;
    if (!fs::is_directory(directoryPath, error))
        return fileNames;

    for (const fs::directory_entry& entry : fs::directory_iterator(directoryPath, error))
    {
        if (error)
            break;

        if (!entry.is_regular_file(error))
            continue;

        fileNames.push_back(entry.path().filename().string());
    }

    return fileNames;
}

}