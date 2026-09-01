/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Async scene thumbnails: JobSystem decodes images; main thread uploads.
 ******************************************************************************/

#include "ThumbnailCache.h"

#include "EditorPaths.h"
#include "ImageCompression.h"
#include "JobSystem.h"
#include "PlatformPath.h"
#include "SimdUtil.h"

#include <raylib.h>

#include <memory>
#include <mutex>
#include <vector>

using timberline_engine::JobSystem;
using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::downscaleImageToFit;
using timberline_engine::kEditorThumbMaxH;
using timberline_engine::kEditorThumbMaxW;
using timberline_engine::loadImageFromAssetFile;
using timberline_engine::pathJoin;

namespace timberline_editor
{
namespace
{

std::mutex gThumbDecodeMutex;

bool tryDecodeImageAtPath(const std::string& path, Image& outImage)
{
    const std::string compressedPath = compressedAssetPath(path);
    if (FileExists(compressedPath.c_str()))
    {
        std::lock_guard<std::mutex> lock(gThumbDecodeMutex);
        if (loadImageFromAssetFile(compressedPath, outImage))
            return true;
    }
    if (FileExists(path.c_str()))
    {
        std::lock_guard<std::mutex> lock(gThumbDecodeMutex);
        if (loadImageFromAssetFile(path, outImage))
            return true;
    }
    return false;
}

bool decodeSceneThumbnailImage(
    const std::string& imagePath,
    const std::string& assetRoot,
    const std::string& resourceDir,
    Image& outImage)
{
    outImage = Image{};
    const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, imagePath);
    for (const std::string& path : paths)
    {
        if (tryDecodeImageAtPath(path, outImage))
            return true;
    }

    const std::string underResources = pathJoin(parentDirectory(resourceDir), imagePath);
    if (!underResources.empty() && tryDecodeImageAtPath(underResources, outImage))
        return true;

    return false;
}

struct ThumbDecodeResult
{
    std::uint64_t generation = 0;
    std::string sceneId;
    Image image{};
    bool ok = false;
};

} // namespace

ThumbnailEntry& ThumbnailCache::getOrLoad(
    const std::string& sceneId,
    const timberline_engine::SceneDocument& scenes,
    const std::string& assetRoot,
    const std::string& resourceDir)
{
    ThumbnailEntry& entry = entries[sceneId];
    if (entry.loaded || entry.missing || entry.loading)
        return entry;

    const std::string imagePath = scenes.getSceneImagePath(sceneId);
    if (imagePath.empty())
    {
        entry.missing = true;
        return entry;
    }

    entry.loading = true;
    enqueueDecode(sceneId, imagePath, assetRoot, resourceDir);
    return entry;
}

void ThumbnailCache::enqueueDecode(
    const std::string& sceneId,
    const std::string& imagePath,
    const std::string& assetRoot,
    const std::string& resourceDir)
{
    if (inFlight.count(sceneId) > 0)
        return;
    inFlight.insert(sceneId);

    const std::uint64_t gen = generation;
    auto result = std::make_shared<ThumbDecodeResult>();
    result->generation = gen;
    result->sceneId = sceneId;

    const std::string pathCopy = imagePath;
    const std::string rootCopy = assetRoot;
    const std::string resCopy = resourceDir;

    JobSystem::global().enqueue(
        [result, pathCopy, rootCopy, resCopy]() {
            result->ok =
                decodeSceneThumbnailImage(pathCopy, rootCopy, resCopy, result->image);
            if (result->ok)
                downscaleImageToFit(result->image, kEditorThumbMaxW, kEditorThumbMaxH);
        },
        [this, result]() {
            inFlight.erase(result->sceneId);
            if (result->generation != generation)
            {
                if (result->ok && result->image.data != nullptr)
                    UnloadImage(result->image);
                return;
            }

            auto it = entries.find(result->sceneId);
            if (it == entries.end())
            {
                if (result->ok && result->image.data != nullptr)
                    UnloadImage(result->image);
                return;
            }

            ThumbnailEntry& entry = it->second;
            entry.loading = false;

            if (!result->ok || result->image.data == nullptr)
            {
                entry.missing = true;
                return;
            }

            Texture2D tex = LoadTextureFromImage(result->image);
            UnloadImage(result->image);
            result->image = Image{};
            if (tex.id == 0)
            {
                entry.missing = true;
                return;
            }

            if (entry.loaded && entry.texture.id != 0)
                UnloadTexture(entry.texture);
            entry.texture = tex;
            entry.loaded = true;
            entry.missing = false;
        });
}

void ThumbnailCache::poll()
{
    JobSystem::global().pollCompletions();
}

void ThumbnailCache::clear()
{
    ++generation;
    inFlight.clear();
    for (auto& pair : entries)
    {
        if (pair.second.loaded && pair.second.texture.id != 0)
            UnloadTexture(pair.second.texture);
    }
    entries.clear();
}

void ThumbnailCache::invalidate(const std::string& sceneId)
{
    inFlight.erase(sceneId);
    auto it = entries.find(sceneId);
    if (it == entries.end())
        return;
    if (it->second.loaded && it->second.texture.id != 0)
        UnloadTexture(it->second.texture);
    entries.erase(it);
}

} // namespace timberline_editor
