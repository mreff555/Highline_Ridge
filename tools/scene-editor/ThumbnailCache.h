/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Lazy scene image thumbnails for the list and map cards.
 * CPU decode runs on JobSystem; GPU upload on the main thread via poll().
 ******************************************************************************/

#ifndef TIMBERLINE_THUMBNAIL_CACHE_H
#define TIMBERLINE_THUMBNAIL_CACHE_H

#include "EditorTypes.h"
#include "SceneDocument.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace timberline_editor
{

class ThumbnailCache
{
public:
    /**
     * Begin (or return) a thumbnail for sceneId. Never blocks on xz/PNG decode —
     * returns a not-yet-loaded entry while a JobSystem worker runs; call poll()
     * each frame so completed Images become textures.
     */
    ThumbnailEntry& getOrLoad(
        const std::string& sceneId,
        const timberline_engine::SceneDocument& scenes,
        const std::string& assetRoot,
        const std::string& resourceDir);

    /** Drain JobSystem completions that belong to this cache (main / GL thread). */
    void poll();

    void clear();

    /** Drop one cached thumbnail so the next getOrLoad reloads from disk. */
    void invalidate(const std::string& sceneId);

private:
    void enqueueDecode(
        const std::string& sceneId,
        const std::string& imagePath,
        const std::string& assetRoot,
        const std::string& resourceDir);

    std::map<std::string, ThumbnailEntry> entries;
    std::set<std::string> inFlight;
    std::uint64_t generation = 0;
};

} // namespace timberline_editor

#endif /* TIMBERLINE_THUMBNAIL_CACHE_H */
