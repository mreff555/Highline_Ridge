#include "UiBackdrop.h"

#include <algorithm>
#include <cctype>

namespace testgame
{

const float UiBackdrop::kTileDisplayWidth = 280.0f;
const Color UiBackdrop::kPanelBorder = {168, 138, 72, 255};
const Color UiBackdrop::kPanelAccent = {96, 78, 48, 255};
const Color UiBackdrop::kSectionLabel = {132, 122, 104, 255};
const Color UiBackdrop::kPanelBase = {28, 26, 34, 255};

namespace
{

std::string toLowerAscii(std::string value)
{
    for (char& ch : value)
        ch = (char)std::tolower((unsigned char)ch);
    return value;
}

std::string wallpaperPathFor(UiBackgroundId id)
{
    switch (id)
    {
        case UiBackgroundId::Morris:
            return "resources/ui/backgrounds/wallpaper_morris_1890s.jpg";
        case UiBackgroundId::Damask:
            return "resources/ui/backgrounds/wallpaper_damask_1890s.jpg";
        default:
            return "";
    }
}

bool tryLoadWallpaper(const std::string& path, Texture2D& outTexture)
{
    if (!FileExists(path.c_str()))
        return false;

    const Texture2D texture = LoadTexture(path.c_str());
    if (texture.id == 0)
        return false;

    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    outTexture = texture;
    return true;
}

}

UiBackgroundId parseUiBackgroundId(const std::string& value)
{
    const std::string normalized = toLowerAscii(value);
    if (normalized == "morris")
        return UiBackgroundId::Morris;
    if (normalized == "damask")
        return UiBackgroundId::Damask;
    return UiBackgroundId::None;
}

UiBackdrop::UiBackdrop() = default;

UiBackdrop::~UiBackdrop()
{
    if (textureReady)
        UnloadTexture(wallpaper);
}

bool UiBackdrop::load(const UiConfig& config, const std::string& assetRoot)
{
    backgroundId = config.background;
    if (textureReady)
    {
        UnloadTexture(wallpaper);
        wallpaper = Texture2D{};
        textureReady = false;
    }

    if (backgroundId == UiBackgroundId::None)
        return true;

    const std::string relativePath = wallpaperPathFor(backgroundId);
    const std::string primaryPath = assetRoot + "/" + relativePath;
    const std::string fallbackPath = (assetRoot == ".") ? "../" + relativePath : "./" + relativePath;

    if (tryLoadWallpaper(primaryPath, wallpaper) || tryLoadWallpaper(relativePath, wallpaper) ||
        tryLoadWallpaper(fallbackPath, wallpaper))
    {
        textureReady = true;
        TraceLog(LOG_INFO, "Loaded UI backdrop: %s", relativePath.c_str());
        return true;
    }

    TraceLog(LOG_WARNING, "Failed to load UI backdrop '%s'; using flat panels", relativePath.c_str());
    backgroundId = UiBackgroundId::None;
    return false;
}

void UiBackdrop::drawTiledWallpaper(Rectangle bounds, float alpha) const
{
    if (!textureReady)
        return;

    const float scale = kTileDisplayWidth / (float)wallpaper.width;
    const float tileW = (float)wallpaper.width * scale;
    const float tileH = (float)wallpaper.height * scale;
    const Color tint = {255, 255, 255, (unsigned char)(alpha * 255.0f)};

    BeginScissorMode(
        (int)bounds.x,
        (int)bounds.y,
        (int)bounds.width,
        (int)bounds.height);

    for (float y = bounds.y; y < bounds.y + bounds.height; y += tileH)
    {
        for (float x = bounds.x; x < bounds.x + bounds.width; x += tileW)
        {
            const Rectangle dest = { x, y, tileW, tileH };
            DrawTexturePro(
                wallpaper,
                { 0.0f, 0.0f, (float)wallpaper.width, (float)wallpaper.height },
                dest,
                { 0.0f, 0.0f },
                0.0f,
                tint);
        }
    }

    EndScissorMode();
}

void UiBackdrop::drawDarkOverlay(
    Rectangle bounds,
    float roundness,
    int segments,
    unsigned char alpha) const
{
    const Color overlay = {kPanelBase.r, kPanelBase.g, kPanelBase.b, alpha};
    if (roundness > 0.0f)
        DrawRectangleRounded(bounds, roundness, segments, overlay);
    else
        DrawRectangleRec(bounds, overlay);
}

void UiBackdrop::drawPanel(Rectangle bounds, float roundness, int segments) const
{
    if (!isActive())
    {
        if (roundness > 0.0f)
            DrawRectangleRounded(bounds, roundness, segments, kPanelBase);
        else
            DrawRectangleRec(bounds, kPanelBase);
        return;
    }

    DrawRectangleRec(bounds, {14, 12, 18, 255});
    drawTiledWallpaper(bounds, 0.55f);
    drawDarkOverlay(bounds, roundness, segments, 198);
}

void UiBackdrop::drawDialogPanel(Rectangle bounds) const
{
    if (!isActive())
        return;

    DrawRectangleRec(bounds, {14, 12, 18, 255});
    drawTiledWallpaper(bounds, 0.45f);
    drawDarkOverlay(bounds, 0.0f, 4, 210);
    DrawRectangleRec(bounds, {36, 32, 42, 48});
}

void UiBackdrop::drawAccentBar(Rectangle bar) const
{
    DrawRectangleRounded(bar, 1.0f, 4, kPanelAccent);
}

Color UiBackdrop::panelBorderColor() const { return kPanelBorder; }
Color UiBackdrop::panelAccentColor() const { return kPanelAccent; }
Color UiBackdrop::sectionLabelColor() const { return kSectionLabel; }

Color UiBackdrop::statusTrackColor() const
{
    return isActive() ? Color{24, 22, 30, 245} : Color{40, 38, 50, 255};
}

Color UiBackdrop::slotFillColor() const
{
    return isActive() ? Color{22, 20, 28, 248} : Color{40, 38, 50, 255};
}

Color UiBackdrop::slotHoverColor() const
{
    return isActive() ? Color{34, 30, 40, 250} : Color{54, 50, 64, 255};
}

Color UiBackdrop::slotSelectedColor() const
{
    return isActive() ? Color{42, 34, 24, 252} : Color{62, 52, 34, 255};
}

ButtonStyle UiBackdrop::contrastedButtonStyle(const ButtonStyle& base) const
{
    if (!isActive())
        return base;

    ButtonStyle style = base;
    style.normalBg = {22, 20, 28, 248};
    style.hoverBg = {34, 30, 40, 250};
    style.pressedBg = {18, 16, 24, 255};
    style.disabledBg = {20, 18, 26, 220};
    style.borderColor = {92, 76, 48, 255};
    style.disabledBorderColor = {48, 44, 56, 255};
    return style;
}

}