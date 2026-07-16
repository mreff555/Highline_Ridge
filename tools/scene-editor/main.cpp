#include "ImageCompression.h"
#include "PlatformPath.h"
#include "SceneDocument.h"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using highline_ridge::SceneActor;
using highline_ridge::SceneDocument;
using highline_ridge::SceneLayout;
using highline_ridge::buildAssetSearchPaths;
using highline_ridge::loadTextureFromAssetFile;
using highline_ridge::pathJoin;

namespace
{

const Color kPanelFill = {28, 26, 34, 255};
const Color kPanelBorder = {168, 138, 72, 255};
const Color kPanelAccent = {96, 78, 48, 255};
const Color kTextPrimary = {220, 212, 196, 255};
const Color kTextMuted = {132, 122, 104, 255};
const Color kCanvasBg = {18, 17, 22, 255};
const Color kSelection = {120, 96, 48, 180};
const Color kExitArrow = {168, 138, 72, 220};

const float kDividerSize = 6.0f;
const float kMinLeftWidth = 180.0f;
const float kMinTopHeight = 200.0f;
const float kMinBottomHeight = 140.0f;
const float kTabHeight = 30.0f;
const float kSceneCardWidth = 140.0f;
const float kSceneCardHeight = 100.0f;
const float kListThumbSize = 48.0f;
const float kListRowHeight = 56.0f;

namespace fs = std::filesystem;

std::string parentDirectory(const std::string& path)
{
    if (path.empty())
        return "";

    const fs::path parent = fs::path(path).parent_path();
    if (parent.empty())
        return "";

    return parent.lexically_normal().string();
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
    const char* appDir = GetApplicationDirectory();
    if (appDir != nullptr && appDir[0] != '\0')
    {
        if (findResourcesFromBase(appDir, outResourceDir, outAssetRoot))
            return true;

        const std::string fallbackResources = pathJoin(appDir, "../../../resources");
        if (scenesFileExists(fallbackResources))
        {
            outResourceDir = fs::path(fallbackResources).lexically_normal().string();
            outAssetRoot = fs::path(pathJoin(appDir, "../../..")).lexically_normal().string();
            return true;
        }
    }

    const char* workingDir = GetWorkingDirectory();
    if (workingDir != nullptr && workingDir[0] != '\0')
    {
        if (findResourcesFromBase(workingDir, outResourceDir, outAssetRoot))
            return true;
    }

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

    return scenesFileExists(outResourceDir);
}

void drawWrappedText(
    Font font,
    const std::string& text,
    Vector2 position,
    float maxWidth,
    float fontSize,
    float lineSpacing,
    Color color)
{
    std::string line;
    float y = position.y;

    auto flushLine = [&]()
    {
        if (line.empty())
            return;
        DrawTextEx(font, line.c_str(), {position.x, y}, fontSize, 1.0f, color);
        y += fontSize + lineSpacing;
        line.clear();
    };

    std::istringstream stream(text);
    std::string word;
    while (stream >> word)
    {
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (MeasureTextEx(font, candidate.c_str(), fontSize, 1.0f).x <= maxWidth)
        {
            line = candidate;
            continue;
        }

        flushLine();
        line = word;
    }

    flushLine();
}

enum class DragSource
{
    None,
    SceneList,
    Canvas
};

struct ThumbnailEntry
{
    Texture2D texture{};
    bool loaded = false;
    bool missing = false;
};

struct SceneEditorApp
{
    std::string resourceDir = "../../../resources";
    std::string assetRoot = "../../..";
    std::string loadError;
    SceneDocument scenesDoc;

    std::vector<std::string> jsonTabs;
    int activeTabIndex = 0;

    std::string selectedSceneId;
    int canvasLevel = 0;

    float leftPaneWidth = 0.0f;
    float topAreaHeight = 0.0f;

    float leftScroll = 0.0f;
    float variablesScroll = 0.0f;
    float actorsScroll = 0.0f;
    Vector2 canvasScroll{0.0f, 0.0f};

    bool draggingVerticalDivider = false;
    bool draggingHorizontalDivider = false;

    DragSource dragSource = DragSource::None;
    std::string dragSceneId;
    Vector2 dragOffset{0.0f, 0.0f};

    std::map<std::string, ThumbnailEntry> thumbnails;
    bool dirty = false;

    void initLayout(int screenWidth, int screenHeight)
    {
        leftPaneWidth = screenWidth * 0.2f;
        topAreaHeight = screenHeight * (2.0f / 3.0f);
    }

    void clampLayout(int screenWidth, int screenHeight)
    {
        const float maxLeft = screenWidth - kMinLeftWidth - 200.0f;
        if (leftPaneWidth < kMinLeftWidth)
            leftPaneWidth = kMinLeftWidth;
        if (leftPaneWidth > maxLeft)
            leftPaneWidth = maxLeft;

        const float maxTop = screenHeight - kMinBottomHeight - 80.0f;
        if (topAreaHeight < kMinTopHeight)
            topAreaHeight = kMinTopHeight;
        if (topAreaHeight > maxTop)
            topAreaHeight = maxTop;
    }

    Rectangle topAreaBounds(int screenWidth) const
    {
        return {0.0f, 0.0f, static_cast<float>(screenWidth), topAreaHeight};
    }

    Rectangle leftPaneBounds(int screenWidth) const
    {
        const Rectangle top = topAreaBounds(screenWidth);
        return {top.x, top.y, leftPaneWidth, top.height};
    }

    Rectangle mainPaneBounds(int screenWidth) const
    {
        const Rectangle top = topAreaBounds(screenWidth);
        return {top.x + leftPaneWidth + kDividerSize, top.y,
                top.width - leftPaneWidth - kDividerSize, top.height};
    }

    Rectangle bottomPaneBounds(int screenWidth, int screenHeight) const
    {
        return {0.0f, topAreaHeight + kDividerSize,
                static_cast<float>(screenWidth),
                static_cast<float>(screenHeight) - topAreaHeight - kDividerSize};
    }

    Rectangle verticalDividerBounds(int screenWidth) const
    {
        const Rectangle top = topAreaBounds(screenWidth);
        return {leftPaneWidth, top.y, kDividerSize, top.height};
    }

    Rectangle horizontalDividerBounds(int screenWidth) const
    {
        return {0.0f, topAreaHeight, static_cast<float>(screenWidth), kDividerSize};
    }

    std::vector<std::string> listJsonResources() const
    {
        std::vector<std::string> files;
        if (!DirectoryExists(resourceDir.c_str()))
            return files;

        FilePathList entries = LoadDirectoryFilesEx(resourceDir.c_str(), "*.json", false);
        for (unsigned int i = 0; i < entries.count; ++i)
        {
            const char* path = entries.paths[i];
            const char* slash = std::max(strrchr(path, '/'), strrchr(path, '\\'));
            const std::string filename = slash != nullptr ? slash + 1 : path;
            if (!filename.empty())
                files.push_back(filename);
        }
        UnloadDirectoryFiles(entries);

        std::sort(files.begin(), files.end());
        return files;
    }

    bool loadActiveDocument()
    {
        loadError.clear();

        if (!DirectoryExists(resourceDir.c_str()))
        {
            loadError = "Resources folder not found:\n" + resourceDir +
                "\n\nLaunch with:\n./scene-editor /path/to/resources";
            scenesDoc = SceneDocument{};
            selectedSceneId.clear();
            return false;
        }

        if (jsonTabs.empty())
        {
            loadError = "No .json files found in:\n" + resourceDir;
            scenesDoc = SceneDocument{};
            selectedSceneId.clear();
            return false;
        }

        const std::string filename = jsonTabs[static_cast<size_t>(activeTabIndex)];
        if (filename != "scenes.json")
        {
            loadError = filename + " editing is not implemented yet.\nSelect the scenes.json tab.";
            scenesDoc = SceneDocument{};
            selectedSceneId.clear();
            return false;
        }

        const std::string path = pathJoin(resourceDir, filename);
        if (!scenesDoc.load(path))
        {
            loadError = "Failed to load scenes.json:\n" + path;
            scenesDoc = SceneDocument{};
            selectedSceneId.clear();
            return false;
        }

        dirty = false;
        if (selectedSceneId.empty() && scenesDoc.isLoaded())
        {
            const std::vector<std::string> ids = scenesDoc.sceneIds();
            if (!ids.empty())
                selectedSceneId = ids.front();
        }

        ensureDefaultLayouts();
        return true;
    }

    void ensureDefaultLayouts()
    {
        if (!scenesDoc.isLoaded())
            return;

        const std::vector<std::string> ids = scenesDoc.sceneIds();
        float x = 40.0f;
        float y = 40.0f;
        for (const std::string& id : ids)
        {
            SceneLayout layout = scenesDoc.getLayout(id);
            if (layout.x == 0.0f && layout.y == 0.0f)
            {
                layout.x = x;
                layout.y = y;
                layout.level = 0;
                scenesDoc.setLayout(id, layout);
                x += kSceneCardWidth + 24.0f;
                if (x > 800.0f)
                {
                    x = 40.0f;
                    y += kSceneCardHeight + 24.0f;
                }
            }
        }
    }

    void refreshTabs()
    {
        jsonTabs = listJsonResources();
        if (jsonTabs.empty())
        {
            activeTabIndex = 0;
            return;
        }

        int scenesIndex = -1;
        for (size_t i = 0; i < jsonTabs.size(); ++i)
        {
            if (jsonTabs[i] == "scenes.json")
            {
                scenesIndex = static_cast<int>(i);
                break;
            }
        }

        if (scenesIndex >= 0)
            activeTabIndex = scenesIndex;
        else if (activeTabIndex >= static_cast<int>(jsonTabs.size()))
            activeTabIndex = 0;
    }

    ThumbnailEntry& ensureThumbnail(const std::string& sceneId)
    {
        ThumbnailEntry& entry = thumbnails[sceneId];
        if (entry.loaded || entry.missing)
            return entry;

        const std::string imagePath = scenesDoc.getSceneImagePath(sceneId);
        if (imagePath.empty())
        {
            entry.missing = true;
            return entry;
        }

        const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, imagePath);
        for (const std::string& path : paths)
        {
            if (loadTextureFromAssetFile(path, entry.texture))
            {
                entry.loaded = true;
                return entry;
            }
        }

        entry.missing = true;
        return entry;
    }

    void unloadThumbnails()
    {
        for (std::map<std::string, ThumbnailEntry>::iterator it = thumbnails.begin();
             it != thumbnails.end();
             ++it)
        {
            if (it->second.loaded && it->second.texture.id != 0)
                UnloadTexture(it->second.texture);
        }
        thumbnails.clear();
    }

    void markDirty()
    {
        dirty = true;
    }

    bool saveDocument()
    {
        if (!scenesDoc.isLoaded())
            return false;
        if (scenesDoc.save())
        {
            dirty = false;
            return true;
        }
        return false;
    }

    void drawPanel(Rectangle bounds) const
    {
        DrawRectangleRounded(bounds, 0.02f, 8, kPanelFill);
        DrawRectangleLinesEx(bounds, 1.0f, kPanelBorder);
    }

    void drawTabs(Rectangle leftBounds)
    {
        if (jsonTabs.empty())
        {
            DrawTextEx(GetFontDefault(), "No resource JSON files",
                       {leftBounds.x + 8.0f, leftBounds.y + 8.0f}, 12.0f, 1.0f, kTextMuted);
            return;
        }

        const float tabWidth = leftBounds.width / static_cast<float>(std::max<size_t>(1, jsonTabs.size()));
        float x = leftBounds.x;
        for (size_t i = 0; i < jsonTabs.size(); ++i)
        {
            const Rectangle tab = {x, leftBounds.y, tabWidth, kTabHeight};
            const bool active = static_cast<int>(i) == activeTabIndex;
            DrawRectangleRec(tab, active ? kPanelAccent : Color{40, 36, 48, 255});
            DrawRectangleLinesEx(tab, 1.0f, kPanelBorder);

            const std::string& label = jsonTabs[i];
            const float fontSize = 12.0f;
            const Vector2 textSize = MeasureTextEx(GetFontDefault(), label.c_str(), fontSize, 1.0f);
            DrawTextEx(
                GetFontDefault(),
                label.c_str(),
                {tab.x + (tab.width - textSize.x) * 0.5f, tab.y + 8.0f},
                fontSize,
                1.0f,
                active ? kTextPrimary : kTextMuted);

            if (CheckCollisionPointRec(GetMousePosition(), tab) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                activeTabIndex = static_cast<int>(i);
                unloadThumbnails();
                loadActiveDocument();
            }

            x += tabWidth;
        }
    }

    void drawSceneList(Rectangle listBounds)
    {
        if (!scenesDoc.isLoaded())
        {
            const std::string message = loadError.empty()
                ? "Loading scenes.json..."
                : loadError;
            drawWrappedText(
                GetFontDefault(),
                message,
                {listBounds.x + 12.0f, listBounds.y + 12.0f},
                listBounds.width - 24.0f,
                13.0f,
                4.0f,
                kTextMuted);
            return;
        }

        const std::vector<std::string> ids = scenesDoc.sceneIds();
        const float contentHeight = static_cast<float>(ids.size()) * kListRowHeight;
        const float maxScroll = std::max(0.0f, contentHeight - listBounds.height);
        if (leftScroll > maxScroll)
            leftScroll = maxScroll;

        BeginScissorMode(
            static_cast<int>(listBounds.x),
            static_cast<int>(listBounds.y),
            static_cast<int>(listBounds.width),
            static_cast<int>(listBounds.height));

        float y = listBounds.y - leftScroll;
        for (const std::string& id : ids)
        {
            const Rectangle row = {listBounds.x + 4.0f, y, listBounds.width - 8.0f, kListRowHeight - 4.0f};
            const bool selected = id == selectedSceneId;
            if (selected)
                DrawRectangleRec(row, kSelection);

            const ThumbnailEntry& thumb = ensureThumbnail(id);
            const Rectangle thumbRect = {row.x + 6.0f, row.y + 4.0f, kListThumbSize, kListThumbSize};
            DrawRectangleRec(thumbRect, Color{48, 44, 58, 255});
            if (thumb.loaded)
            {
                DrawTexturePro(
                    thumb.texture,
                    {0.0f, 0.0f, static_cast<float>(thumb.texture.width), static_cast<float>(thumb.texture.height)},
                    thumbRect,
                    {0.0f, 0.0f},
                    0.0f,
                    WHITE);
            }

            DrawTextEx(GetFontDefault(), id.c_str(), {row.x + kListThumbSize + 12.0f, row.y + 10.0f},
                       14.0f, 1.0f, kTextPrimary);

            const bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
            if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                selectedSceneId = id;

            if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                dragSource = DragSource::SceneList;
                dragSceneId = id;
                dragOffset = {GetMouseX() - row.x, GetMouseY() - row.y};
            }

            y += kListRowHeight;
        }

        EndScissorMode();

        if (CheckCollisionPointRec(GetMousePosition(), listBounds))
            leftScroll -= GetMouseWheelMove() * 24.0f;
        if (leftScroll < 0.0f)
            leftScroll = 0.0f;
        if (leftScroll > maxScroll)
            leftScroll = maxScroll;
    }

    Vector2 sceneCardScreenPos(const SceneLayout& layout, Rectangle canvasBounds) const
    {
        return {
            canvasBounds.x + layout.x + canvasScroll.x,
            canvasBounds.y + layout.y + canvasScroll.y
        };
    }

    Rectangle sceneCardBounds(const std::string& sceneId, Rectangle canvasBounds) const
    {
        const SceneLayout layout = scenesDoc.getLayout(sceneId);
        const Vector2 pos = sceneCardScreenPos(layout, canvasBounds);
        return {pos.x, pos.y, kSceneCardWidth, kSceneCardHeight};
    }

    void drawExitArrows(Rectangle canvasBounds)
    {
        if (!scenesDoc.isLoaded())
            return;

        const std::vector<std::string> ids = scenesDoc.sceneIds();
        for (const std::string& fromId : ids)
        {
            const SceneLayout fromLayout = scenesDoc.getLayout(fromId);
            if (fromLayout.level != canvasLevel)
                continue;

            const nlohmann::json* scene = scenesDoc.sceneJson(fromId);
            if (scene == nullptr || !scene->contains("exits") || !(*scene)["exits"].is_object())
                continue;

            const Vector2 fromCenter = {
                sceneCardScreenPos(fromLayout, canvasBounds).x + kSceneCardWidth * 0.5f,
                sceneCardScreenPos(fromLayout, canvasBounds).y + kSceneCardHeight * 0.5f
            };

            for (auto it = (*scene)["exits"].begin(); it != (*scene)["exits"].end(); ++it)
            {
                if (!it.value().is_string())
                    continue;

                const std::string toId = it.value().get<std::string>();
                if (!scenesDoc.hasScene(toId))
                    continue;

                const SceneLayout toLayout = scenesDoc.getLayout(toId);
                if (toLayout.level != canvasLevel)
                    continue;

                const Vector2 toCenter = {
                    sceneCardScreenPos(toLayout, canvasBounds).x + kSceneCardWidth * 0.5f,
                    sceneCardScreenPos(toLayout, canvasBounds).y + kSceneCardHeight * 0.5f
                };

                DrawLineEx(fromCenter, toCenter, 2.0f, kExitArrow);

                const Vector2 direction = Vector2Normalize(Vector2Subtract(toCenter, fromCenter));
                const Vector2 arrowTip = Vector2Subtract(toCenter, Vector2Scale(direction, 24.0f));
                const Vector2 ortho = {-direction.y, direction.x};
                const Vector2 p1 = Vector2Add(arrowTip, Vector2Scale(ortho, 6.0f));
                const Vector2 p2 = Vector2Subtract(arrowTip, Vector2Scale(ortho, 6.0f));
                DrawTriangle(p1, toCenter, p2, kExitArrow);
            }
        }
    }

    void drawStairIcons(Rectangle canvasBounds)
    {
        if (!scenesDoc.isLoaded())
            return;

        const std::vector<std::string> ids = scenesDoc.sceneIds();
        for (const std::string& id : ids)
        {
            const SceneLayout layout = scenesDoc.getLayout(id);
            if (layout.level != canvasLevel)
                continue;

            const nlohmann::json* scene = scenesDoc.sceneJson(id);
            if (scene == nullptr || !scene->contains("exits") || !(*scene)["exits"].is_object())
                continue;

            bool hasUp = scene->at("exits").contains("up");
            bool hasDown = scene->at("exits").contains("down");
            if (!hasUp && !hasDown)
                continue;

            const Rectangle card = sceneCardBounds(id, canvasBounds);
            const char* icon = hasUp ? "^" : "v";
            DrawTextEx(GetFontDefault(), icon, {card.x + card.width - 18.0f, card.y + 4.0f},
                       16.0f, 1.0f, kTextPrimary);
        }
    }

    void drawCanvas(Rectangle canvasBounds)
    {
        DrawRectangleRec(canvasBounds, kCanvasBg);

        DrawTextEx(GetFontDefault(), TextFormat("Level %d", canvasLevel),
                   {canvasBounds.x + 12.0f, canvasBounds.y + 8.0f}, 14.0f, 1.0f, kTextMuted);

        const Rectangle levelDownBtn = {canvasBounds.x + canvasBounds.width - 72.0f, canvasBounds.y + 6.0f, 28.0f, 22.0f};
        const Rectangle levelUpBtn = {canvasBounds.x + canvasBounds.width - 38.0f, canvasBounds.y + 6.0f, 28.0f, 22.0f};
        DrawRectangleRec(levelDownBtn, kPanelAccent);
        DrawRectangleRec(levelUpBtn, kPanelAccent);
        DrawTextEx(GetFontDefault(), "-", {levelDownBtn.x + 10.0f, levelDownBtn.y + 2.0f}, 16.0f, 1.0f, kTextPrimary);
        DrawTextEx(GetFontDefault(), "+", {levelUpBtn.x + 8.0f, levelUpBtn.y + 2.0f}, 16.0f, 1.0f, kTextPrimary);

        if (CheckCollisionPointRec(GetMousePosition(), levelDownBtn) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            canvasLevel -= 1;
        if (CheckCollisionPointRec(GetMousePosition(), levelUpBtn) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            canvasLevel += 1;

        if (!scenesDoc.isLoaded())
        {
            const std::string message = loadError.empty()
                ? "Select the scenes.json tab to edit the scene map."
                : loadError;
            drawWrappedText(
                GetFontDefault(),
                message,
                {canvasBounds.x + 20.0f, canvasBounds.y + 40.0f},
                canvasBounds.width - 40.0f,
                15.0f,
                5.0f,
                kTextMuted);
            return;
        }

        drawExitArrows(canvasBounds);

        const std::vector<std::string> ids = scenesDoc.sceneIds();
        for (const std::string& id : ids)
        {
            const SceneLayout layout = scenesDoc.getLayout(id);
            if (layout.level != canvasLevel)
                continue;

            const Rectangle card = sceneCardBounds(id, canvasBounds);
            const bool selected = id == selectedSceneId;
            DrawRectangleRec(card, selected ? Color{52, 46, 62, 255} : Color{36, 32, 44, 255});
            DrawRectangleLinesEx(card, selected ? 2.0f : 1.0f, selected ? kPanelBorder : kPanelAccent);

            const ThumbnailEntry& thumb = ensureThumbnail(id);
            const Rectangle thumbRect = {card.x + 6.0f, card.y + 6.0f, card.width - 12.0f, card.height - 34.0f};
            DrawRectangleRec(thumbRect, Color{24, 22, 30, 255});
            if (thumb.loaded)
            {
                DrawTexturePro(
                    thumb.texture,
                    {0.0f, 0.0f, static_cast<float>(thumb.texture.width), static_cast<float>(thumb.texture.height)},
                    thumbRect,
                    {0.0f, 0.0f},
                    0.0f,
                    WHITE);
            }

            DrawTextEx(GetFontDefault(), id.c_str(), {card.x + 6.0f, card.y + card.height - 22.0f},
                       12.0f, 1.0f, kTextPrimary);

            const bool hovered = CheckCollisionPointRec(GetMousePosition(), card);
            if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                selectedSceneId = id;
                dragSource = DragSource::Canvas;
                dragSceneId = id;
                dragOffset = {GetMouseX() - card.x, GetMouseY() - card.y};
            }
        }

        drawStairIcons(canvasBounds);

        if (dragSource != DragSource::None && !dragSceneId.empty() && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            const Rectangle ghost = {static_cast<float>(GetMouseX()) - dragOffset.x,
                                     static_cast<float>(GetMouseY()) - dragOffset.y,
                                     kSceneCardWidth, kSceneCardHeight};
            DrawRectangleRec(ghost, Color{80, 70, 50, 120});
            DrawRectangleLinesEx(ghost, 1.0f, kPanelBorder);
        }

        if (dragSource != DragSource::None && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            if (CheckCollisionPointRec(GetMousePosition(), canvasBounds) && scenesDoc.hasScene(dragSceneId))
            {
                SceneLayout layout = scenesDoc.getLayout(dragSceneId);
                layout.x = static_cast<float>(GetMouseX()) - canvasBounds.x - dragOffset.x - canvasScroll.x;
                layout.y = static_cast<float>(GetMouseY()) - canvasBounds.y - dragOffset.y - canvasScroll.y;
                layout.level = canvasLevel;
                scenesDoc.setLayout(dragSceneId, layout);
                selectedSceneId = dragSceneId;
                markDirty();
            }

            dragSource = DragSource::None;
            dragSceneId.clear();
        }

        if (CheckCollisionPointRec(GetMousePosition(), canvasBounds))
        {
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
                canvasScroll.x += GetMouseWheelMove() * 24.0f;
            else if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                canvasScroll.y += GetMouseWheelMove() * 24.0f;
            else
                canvasScroll.y -= GetMouseWheelMove() * 24.0f;
        }
    }

    std::string truncate(const std::string& text, size_t maxLen) const
    {
        if (text.size() <= maxLen)
            return text;
        return text.substr(0, maxLen - 3) + "...";
    }

    void drawVariablesPane(Rectangle paneBounds)
    {
        DrawTextEx(GetFontDefault(), "Scene Variables", {paneBounds.x + 12.0f, paneBounds.y + 8.0f},
                   15.0f, 1.0f, kTextMuted);

        if (selectedSceneId.empty() || !scenesDoc.hasScene(selectedSceneId))
        {
            DrawTextEx(GetFontDefault(), "Select a scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
                       14.0f, 1.0f, kTextMuted);
            return;
        }

        const std::vector<std::pair<std::string, std::string>> rows =
            scenesDoc.sceneVariableRows(selectedSceneId);
        const float rowHeight = 22.0f;
        const float contentHeight = static_cast<float>(rows.size()) * rowHeight + 36.0f;
        const float maxScroll = std::max(0.0f, contentHeight - paneBounds.height);
        if (variablesScroll > maxScroll)
            variablesScroll = maxScroll;

        BeginScissorMode(
            static_cast<int>(paneBounds.x),
            static_cast<int>(paneBounds.y + 28.0f),
            static_cast<int>(paneBounds.width),
            static_cast<int>(paneBounds.height - 28.0f));

        float y = paneBounds.y + 36.0f - variablesScroll;
        for (const std::pair<std::string, std::string>& row : rows)
        {
            const std::string line = row.first + ": " + truncate(row.second, 80);
            DrawTextEx(GetFontDefault(), line.c_str(), {paneBounds.x + 12.0f, y}, 13.0f, 1.0f, kTextPrimary);
            y += rowHeight;
        }

        EndScissorMode();

        if (CheckCollisionPointRec(GetMousePosition(), paneBounds))
            variablesScroll -= GetMouseWheelMove() * 18.0f;
        if (variablesScroll < 0.0f)
            variablesScroll = 0.0f;
        if (variablesScroll > maxScroll)
            variablesScroll = maxScroll;
    }

    void drawActorsPane(Rectangle paneBounds)
    {
        DrawTextEx(GetFontDefault(), "Actors", {paneBounds.x + 12.0f, paneBounds.y + 8.0f},
                   15.0f, 1.0f, kTextMuted);

        if (selectedSceneId.empty() || !scenesDoc.hasScene(selectedSceneId))
        {
            DrawTextEx(GetFontDefault(), "Select a scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
                       14.0f, 1.0f, kTextMuted);
            return;
        }

        const std::vector<SceneActor> actors = scenesDoc.getActors(selectedSceneId);
        const float rowHeight = 24.0f;
        const float contentHeight = static_cast<float>(actors.size() + 1) * rowHeight + 36.0f;
        const float maxScroll = std::max(0.0f, contentHeight - paneBounds.height);
        if (actorsScroll > maxScroll)
            actorsScroll = maxScroll;

        BeginScissorMode(
            static_cast<int>(paneBounds.x),
            static_cast<int>(paneBounds.y + 28.0f),
            static_cast<int>(paneBounds.width),
            static_cast<int>(paneBounds.height - 28.0f));

        float y = paneBounds.y + 36.0f - actorsScroll;
        if (actors.empty())
        {
            DrawTextEx(GetFontDefault(), "(no actors)", {paneBounds.x + 12.0f, y},
                       13.0f, 1.0f, kTextMuted);
            y += rowHeight;
        }
        else
        {
            for (const SceneActor& actor : actors)
            {
                const std::string line = actor.id + " — " + actor.name +
                    (actor.role.empty() ? "" : " (" + actor.role + ")");
                DrawTextEx(GetFontDefault(), line.c_str(), {paneBounds.x + 12.0f, y},
                           13.0f, 1.0f, kTextPrimary);
                y += rowHeight;
            }
        }

        EndScissorMode();

        if (CheckCollisionPointRec(GetMousePosition(), paneBounds))
            actorsScroll -= GetMouseWheelMove() * 18.0f;
        if (actorsScroll < 0.0f)
            actorsScroll = 0.0f;
        if (actorsScroll > maxScroll)
            actorsScroll = maxScroll;
    }

    void drawBottomPane(Rectangle bottomBounds)
    {
        drawPanel(bottomBounds);

        const float splitX = bottomBounds.x + bottomBounds.width * 0.55f;
        const Rectangle variablesBounds = {bottomBounds.x, bottomBounds.y,
                                           splitX - bottomBounds.x, bottomBounds.height};
        const Rectangle actorsBounds = {splitX + 2.0f, bottomBounds.y,
                                        bottomBounds.x + bottomBounds.width - splitX - 2.0f,
                                        bottomBounds.height};

        DrawLineEx({splitX, bottomBounds.y + 8.0f}, {splitX, bottomBounds.y + bottomBounds.height - 8.0f},
                   1.0f, kPanelAccent);

        drawVariablesPane(variablesBounds);
        drawActorsPane(actorsBounds);
    }

    void handleDividers(int screenWidth, int screenHeight)
    {
        const Rectangle vDiv = verticalDividerBounds(screenWidth);
        const Rectangle hDiv = horizontalDividerBounds(screenWidth);
        const Vector2 mouse = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (CheckCollisionPointRec(mouse, vDiv))
                draggingVerticalDivider = true;
            else if (CheckCollisionPointRec(mouse, hDiv))
                draggingHorizontalDivider = true;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            draggingVerticalDivider = false;
            draggingHorizontalDivider = false;
        }

        if (draggingVerticalDivider && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            leftPaneWidth = mouse.x - vDiv.width * 0.5f;

        if (draggingHorizontalDivider && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            topAreaHeight = mouse.y - hDiv.height * 0.5f;

        clampLayout(screenWidth, screenHeight);

        DrawRectangleRec(vDiv, draggingVerticalDivider ? kPanelBorder : kPanelAccent);
        DrawRectangleRec(hDiv, draggingHorizontalDivider ? kPanelBorder : kPanelAccent);
    }

    void drawStatusBar(int screenWidth, int screenHeight)
    {
        const std::string status = dirty ? "Modified" : "Saved";
        const std::string pathLabel = scenesDoc.isLoaded()
            ? scenesDoc.path()
            : "Resources: " + resourceDir;
        DrawTextEx(GetFontDefault(), pathLabel.c_str(), {8.0f, static_cast<float>(screenHeight) - 18.0f},
                   12.0f, 1.0f, kTextMuted);
        DrawTextEx(GetFontDefault(), status.c_str(),
                   {static_cast<float>(screenWidth) - 70.0f, static_cast<float>(screenHeight) - 18.0f},
                   12.0f, 1.0f, dirty ? Color{200, 140, 80, 255} : kTextMuted);
    }

    void handleShortcuts()
    {
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        {
            if (IsKeyPressed(KEY_S))
                saveDocument();
        }
    }

    void update()
    {
        handleShortcuts();
        handleDividers(GetScreenWidth(), GetScreenHeight());
    }

    void draw()
    {
        const int screenWidth = GetScreenWidth();
        const int screenHeight = GetScreenHeight();

        BeginDrawing();
        ClearBackground(Color{14, 13, 18, 255});

        const Rectangle left = leftPaneBounds(screenWidth);
        const Rectangle main = mainPaneBounds(screenWidth);
        const Rectangle bottom = bottomPaneBounds(screenWidth, screenHeight);

        drawPanel(left);
        drawTabs(left);
        const Rectangle listBounds = {left.x, left.y + kTabHeight + 4.0f, left.width, left.height - kTabHeight - 8.0f};
        drawSceneList(listBounds);

        drawPanel(main);
        const Rectangle canvasBounds = {main.x + 4.0f, main.y + 4.0f, main.width - 8.0f, main.height - 8.0f};
        drawCanvas(canvasBounds);

        drawBottomPane(bottom);
        drawStatusBar(screenWidth, screenHeight);

        EndDrawing();
    }
};

}

int main(int argc, char** argv)
{
    const int screenWidth = 1440;
    const int screenHeight = 900;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Highline Ridge Resource Editor");
    SetTargetFPS(60);

    SceneEditorApp app;

    if (argc >= 2)
    {
        app.resourceDir = argv[1];
        app.assetRoot = (argc >= 3) ? argv[2] : parentDirectory(app.resourceDir);
    }
    else
    {
        resolveEditorPaths(app.resourceDir, app.assetRoot);
    }

    app.initLayout(screenWidth, screenHeight);
    app.refreshTabs();
    app.loadActiveDocument();

    while (!WindowShouldClose())
    {
        app.update();
        app.draw();
    }

    app.unloadThumbnails();
    app.saveDocument();
    CloseWindow();
    return 0;
}