/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "DisplayAspect.h"

#include <algorithm>
#include <cmath>

namespace timberline_engine
{

DisplayAspectBucket classifyAspectRatio(float width, float height)
{
    if (width < 1.0f || height < 1.0f)
        return DisplayAspectBucket::Ratio16x9;

    const float a = width / height;
    // Midpoints between 16:10 (1.6), 16:9 (~1.778), 21:9 (~2.333).
    if (a < 1.69f)
        return DisplayAspectBucket::Ratio16x10;
    if (a < 2.05f)
        return DisplayAspectBucket::Ratio16x9;
    return DisplayAspectBucket::Ratio21x9;
}

DisplayAspectBucket resolveAspectBucket(
    const DisplayAspectPreference& preference,
    float viewportWidth,
    float viewportHeight)
{
    if (preference.mode == "16x9")
        return DisplayAspectBucket::Ratio16x9;
    if (preference.mode == "16x10")
        return DisplayAspectBucket::Ratio16x10;
    if (preference.mode == "21x9")
        return DisplayAspectBucket::Ratio21x9;
    // "auto" and unknown → viewport.
    return classifyAspectRatio(viewportWidth, viewportHeight);
}

const char* aspectBucketKey(DisplayAspectBucket bucket)
{
    switch (bucket)
    {
    case DisplayAspectBucket::Ratio16x10:
        return "16x10";
    case DisplayAspectBucket::Ratio21x9:
        return "21x9";
    case DisplayAspectBucket::Ratio16x9:
    case DisplayAspectBucket::Other:
    default:
        return "16x9";
    }
}

DisplayAspectBucket aspectBucketFromKey(const std::string& key)
{
    if (key == "16x10")
        return DisplayAspectBucket::Ratio16x10;
    if (key == "21x9")
        return DisplayAspectBucket::Ratio21x9;
    if (key == "16x9")
        return DisplayAspectBucket::Ratio16x9;
    return DisplayAspectBucket::Other;
}

std::string pickAspectImagePath(
    const std::map<std::string, std::string>& imageVariants,
    DisplayAspectBucket bucket,
    const std::string& fallbackPath)
{
    const char* key = aspectBucketKey(bucket);
    auto it = imageVariants.find(key);
    if (it != imageVariants.end() && !it->second.empty())
        return it->second;

    if (bucket != DisplayAspectBucket::Ratio16x9)
    {
        auto master = imageVariants.find("16x9");
        if (master != imageVariants.end() && !master->second.empty())
            return master->second;
    }

    return fallbackPath;
}

Rectangle coverCropSourceRect(float texW, float texH, float dstW, float dstH)
{
    if (texW < 1.0f || texH < 1.0f || dstW < 1.0f || dstH < 1.0f)
        return {0.0f, 0.0f, std::max(1.0f, texW), std::max(1.0f, texH)};

    const float texAspect = texW / texH;
    const float dstAspect = dstW / dstH;

    if (texAspect > dstAspect)
    {
        // Texture wider than viewport — crop left/right.
        const float srcH = texH;
        const float srcW = texH * dstAspect;
        const float srcX = (texW - srcW) * 0.5f;
        return {srcX, 0.0f, srcW, srcH};
    }

    // Texture taller (or equal) — crop top/bottom.
    const float srcW = texW;
    const float srcH = texW / dstAspect;
    const float srcY = (texH - srcH) * 0.5f;
    return {0.0f, srcY, srcW, srcH};
}

} // namespace timberline_engine
