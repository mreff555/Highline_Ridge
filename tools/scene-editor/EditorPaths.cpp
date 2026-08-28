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

#include "EditorPaths.h"
#include "PlatformPath.h"
#include <raylib.h>
#include <filesystem>
#include <string>
namespace fs=std::filesystem;
using timberline_engine::pathJoin;

namespace timberline_editor
{

std::string parentDirectory(const std::string& path)
{
    if (path.empty())
        return "";

    const fs::path parent = fs::path(path).parent_path();
    if (parent.empty())
        return "";

    return parent.lexically_normal().string();
}
std::string absolutePath(const std::string& path)
{
    if (path.empty())
        return path;

    std::error_code error;
    const fs::path resolved = fs::absolute(fs::path(path), error);
    if (error)
        return path;

    return resolved.lexically_normal().string();
}
bool resourceDirectoryExists(const std::string& resourceDir)
{
    std::error_code error;
    return fs::is_directory(fs::path(resourceDir), error);
}
bool scenesFileExists(const std::string& resourceDir)
{
    return FileExists(pathJoin(resourceDir, "scenes.json").c_str());
}
bool findResourcesFromBase(
    const std::string& baseDir,
    std::string& outResourceDir,
    std::string& outAssetRoot)
{
    std::string dir = baseDir;
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth)
    {
        const std::string resourcesDir = pathJoin(dir, "resources");
        if (scenesFileExists(resourcesDir))
        {
            outResourceDir = resourcesDir;
            outAssetRoot = dir;
            return true;
        }

        const std::string parent = parentDirectory(dir);
        if (parent.empty() || parent == dir)
            break;
        dir = parent;
    }

    return false;
}
bool resolveEditorPaths(std::string& outResourceDir, std::string& outAssetRoot)
{
    bool found = false;
    const char* appDir = GetApplicationDirectory();
    if (appDir != nullptr && appDir[0] != '\0')
    {
        const std::string bundledResources = pathJoin(appDir, "resources");
        if (scenesFileExists(bundledResources))
        {
            outResourceDir = bundledResources;
            outAssetRoot = appDir;
            found = true;
        }
        else if (findResourcesFromBase(appDir, outResourceDir, outAssetRoot))
        {
            found = true;
        }
        else
        {
            const std::string fallbackResources = pathJoin(appDir, "../../../resources");
            if (scenesFileExists(fallbackResources))
            {
                outResourceDir = fs::path(fallbackResources).lexically_normal().string();
                outAssetRoot = fs::path(pathJoin(appDir, "../../..")).lexically_normal().string();
                found = true;
            }
        }
    }

    if (!found)
    {
        const char* workingDir = GetWorkingDirectory();
        if (workingDir != nullptr && workingDir[0] != '\0' &&
            findResourcesFromBase(workingDir, outResourceDir, outAssetRoot))
        {
            found = true;
        }
    }

    if (!found)
    {
        if (appDir != nullptr && appDir[0] != '\0')
        {
            outResourceDir = fs::path(pathJoin(appDir, "../../../resources")).lexically_normal().string();
            outAssetRoot = fs::path(pathJoin(appDir, "../../..")).lexically_normal().string();
        }
        else
        {
            outResourceDir = "../../../resources";
            outAssetRoot = "../../..";
        }
    }

    outResourceDir = absolutePath(outResourceDir);
    outAssetRoot = absolutePath(outAssetRoot);
    return found || scenesFileExists(outResourceDir);
}
bool ensureValidResourcePaths(std::string& resourceDir, std::string& assetRoot)
{
    resourceDir = absolutePath(resourceDir);
    if (!assetRoot.empty())
        assetRoot = absolutePath(assetRoot);
    else
        assetRoot = parentDirectory(resourceDir);

    if (scenesFileExists(resourceDir))
    {
        // Image paths in scenes.json are like "resources/images/cabin.png", so
        // the asset root is the parent of the resources directory.
        if (assetRoot.empty() || !scenesFileExists(pathJoin(assetRoot, "resources")))
            assetRoot = parentDirectory(resourceDir);
        return true;
    }

    return resolveEditorPaths(resourceDir, assetRoot);
}

} // namespace timberline_editor
