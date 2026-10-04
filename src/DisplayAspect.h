/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Aspect-ratio buckets, variant keys, and cover-crop UV math for scene plates.
 ******************************************************************************/

#ifndef TIMBERLINE_DISPLAY_ASPECT_H
#define TIMBERLINE_DISPLAY_ASPECT_H

#include <raylib.h>

#include <map>
#include <string>

namespace timberline_engine
{

/** Runtime aspect class for picking optional imageVariants. */
enum class DisplayAspectBucket
{
    Ratio16x9,  // ~1.78 — primary authored target
    Ratio16x10, // ~1.60 — many laptops
    Ratio21x9,  // ~2.33 — ultrawide
    Other
};

/** Preference string stored in DisplayConfig / user_config.json. */
struct DisplayAspectPreference
{
    /** "auto" | "16x9" | "16x10" | "21x9" — v1 UI uses auto only. */
    std::string mode = "auto";
};

DisplayAspectBucket classifyAspectRatio(float width, float height);

/** Resolve preference + viewport into a bucket (auto → classify viewport). */
DisplayAspectBucket resolveAspectBucket(
    const DisplayAspectPreference& preference,
    float viewportWidth,
    float viewportHeight);

/** JSON / map key for a bucket ("16x9", …). */
const char* aspectBucketKey(DisplayAspectBucket bucket);

DisplayAspectBucket aspectBucketFromKey(const std::string& key);

/**
 * Pick a plate path from variants + fallback.
 * Preference order: exact bucket → 16x9 → fallbackPath.
 */
std::string pickAspectImagePath(
    const std::map<std::string, std::string>& imageVariants,
    DisplayAspectBucket bucket,
    const std::string& fallbackPath);

/**
 * Source rect for DrawTexturePro: uniform scale to cover dst, center crop.
 * texW/texH = texture pixels; dstW/dstH = destination size.
 */
Rectangle coverCropSourceRect(float texW, float texH, float dstW, float dstH);

} // namespace timberline_engine

#endif /* TIMBERLINE_DISPLAY_ASPECT_H */
