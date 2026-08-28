/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "MainMenuBackdrop.h"
#include "ImageCompression.h"

#include <algorithm>
#include <cstdlib>

namespace timberline_engine
{

namespace
{

bool loadMenuTexture(const std::string& assetRoot, const std::string& rel, Texture2D& out)
{
    const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, rel);
    for (const std::string& path : paths)
    {
        const std::string compressed = compressedAssetPath(path);
        if (FileExists(compressed.c_str()) && loadTextureFromAssetFile(compressed, out))
            return true;
        if (FileExists(path.c_str()) && loadTextureFromAssetFile(path, out))
            return true;
    }
    return false;
}

void drawTextureCover(const Texture2D& tex, int screenW, int screenH, Color tint)
{
    if (tex.id == 0 || screenW <= 0 || screenH <= 0)
        return;
    const float sx = static_cast<float>(screenW) / static_cast<float>(tex.width);
    const float sy = static_cast<float>(screenH) / static_cast<float>(tex.height);
    const float scale = std::max(sx, sy);
    const float dw = tex.width * scale;
    const float dh = tex.height * scale;
    const float dx = (screenW - dw) * 0.5f;
    const float dy = (screenH - dh) * 0.5f;
    DrawTexturePro(
        tex,
        {0, 0, static_cast<float>(tex.width), static_cast<float>(tex.height)},
        {dx, dy, dw, dh},
        {0, 0},
        0.0f,
        tint);
}

} // namespace

void MainMenuBackdrop::load(const std::string& assetRoot)
{
    unload();
    backgroundLoaded = loadMenuTexture(
        assetRoot, "resources/images/menu/title_ridge_idle.png", background);
    if (!backgroundLoaded)
        TraceLog(LOG_WARNING, "Main menu backdrop missing");
}

void MainMenuBackdrop::unload()
{
    if (backgroundLoaded)
    {
        UnloadTexture(background);
        background = {};
        backgroundLoaded = false;
    }
    snow.clear();
    snowScreenW = 0;
    snowScreenH = 0;
}

void MainMenuBackdrop::resetSnowflake(
    Snowflake& flake,
    int screenWidth,
    int screenHeight,
    bool scatterY)
{
    flake.x = static_cast<float>(std::rand() % std::max(1, screenWidth));
    flake.y = scatterY
        ? static_cast<float>(std::rand() % std::max(1, screenHeight))
        : -static_cast<float>(std::rand() % 40);
    flake.speed = 35.0f + static_cast<float>(std::rand() % 90);
    flake.drift = -25.0f + static_cast<float>(std::rand() % 50);
    flake.size = 1.2f + static_cast<float>(std::rand() % 28) * 0.1f;
    flake.alpha = 120.0f + static_cast<float>(std::rand() % 100);
}

void MainMenuBackdrop::ensureSnow(int screenWidth, int screenHeight) const
{
    if (screenWidth == snowScreenW && screenHeight == snowScreenH && !snow.empty())
        return;
    snowScreenW = screenWidth;
    snowScreenH = screenHeight;
    const int count = 160;
    snow.resize(static_cast<size_t>(count));
    for (Snowflake& flake : snow)
        const_cast<MainMenuBackdrop*>(this)->resetSnowflake(
            flake, screenWidth, screenHeight, true);
}

void MainMenuBackdrop::update(float deltaSeconds)
{
    if (snowScreenW <= 0 || snowScreenH <= 0)
        return;

    for (Snowflake& flake : snow)
    {
        flake.y += flake.speed * deltaSeconds;
        flake.x += flake.drift * deltaSeconds;
        if (flake.y > static_cast<float>(snowScreenH) + 8.0f
            || flake.x < -20.0f
            || flake.x > static_cast<float>(snowScreenW) + 20.0f)
        {
            resetSnowflake(flake, snowScreenW, snowScreenH, false);
        }
    }
}

void MainMenuBackdrop::draw(int screenWidth, int screenHeight, float dimAlpha) const
{
    ensureSnow(screenWidth, screenHeight);

    DrawRectangle(0, 0, screenWidth, screenHeight, Color{12, 10, 16, 255});

    if (backgroundLoaded)
        drawTextureCover(background, screenWidth, screenHeight, WHITE);

    for (const Snowflake& flake : snow)
    {
        DrawCircleV(
            {flake.x, flake.y},
            flake.size,
            Color{230, 235, 245, static_cast<unsigned char>(flake.alpha)});
    }

    const unsigned char dim = static_cast<unsigned char>(
        std::max(0.0f, std::min(220.0f, dimAlpha * 255.0f)));
    DrawRectangle(0, 0, screenWidth, screenHeight, Color{8, 8, 12, dim});
}

} // namespace timberline_engine
