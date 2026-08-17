/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Full-screen title-screen backdrop (ridge vignette + snow).
 ******************************************************************************/

#ifndef MAIN_MENU_BACKDROP_H
#define MAIN_MENU_BACKDROP_H

#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_engine
{

/**
 * Title-screen backdrop: static ridge vignette with falling snow.
 * Pose-swap animation was dropped (two-frame swaps looked too coarse).
 */
class MainMenuBackdrop
{
public:
    void load(const std::string& assetRoot);
    void unload();
    void update(float deltaSeconds);
    void draw(int screenWidth, int screenHeight, float dimAlpha = 0.42f) const;
    bool isLoaded() const { return backgroundLoaded; }

private:
    struct Snowflake
    {
        float x = 0.0f;
        float y = 0.0f;
        float speed = 40.0f;
        float drift = 0.0f;
        float size = 1.5f;
        float alpha = 180.0f;
    };

    void resetSnowflake(Snowflake& flake, int screenWidth, int screenHeight, bool scatterY);
    void ensureSnow(int screenWidth, int screenHeight) const;

    Texture2D background{};
    bool backgroundLoaded = false;

    mutable std::vector<Snowflake> snow;
    mutable int snowScreenW = 0;
    mutable int snowScreenH = 0;
};

} // namespace timberline_engine

#endif /* MAIN_MENU_BACKDROP_H */
