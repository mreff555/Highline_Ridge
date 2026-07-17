#include "ImageCompression.h"
#include "PlatformPath.h"
#include "SceneDocument.h"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using highline_ridge::SceneActor;
using highline_ridge::SceneDocument;
using highline_ridge::SceneLayout;
using highline_ridge::buildAssetSearchPaths;
using highline_ridge::compressedAssetPath;
using highline_ridge::listDirectoryFileNames;
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
const Color kButtonDisabled = {48, 46, 54, 255};
const Color kTextDisabled = {90, 86, 96, 255};
const Color kModalOverlay = {0, 0, 0, 160};
const Color kModalFill = {32, 30, 40, 255};

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
    Font uiFont{};

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

    bool stackDialogOpen = false;
    std::string stackSourceId;
    std::string stackTargetId;
    float stackPendingX = 0.0f;
    float stackPendingY = 0.0f;

    std::map<std::string, ThumbnailEntry> thumbnails;
    bool dirty = false;

    Font textFont() const
    {
        if (uiFont.texture.id != 0)
            return uiFont;
        return GetFontDefault();
    }

    void loadUiFont()
    {
        if (uiFont.texture.id != 0)
        {
            UnloadFont(uiFont);
            uiFont = Font{};
        }

        const std::string candidates[] = {
            pathJoin(resourceDir, "fonts/CourierPrime-Regular.ttf"),
            pathJoin(assetRoot, "resources/fonts/CourierPrime-Regular.ttf"),
            pathJoin(assetRoot, "fonts/CourierPrime-Regular.ttf"),
            "/System/Library/Fonts/Supplemental/Courier New.ttf",
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Helvetica.ttc"
        };

        for (const std::string& path : candidates)
        {
            if (path.empty() || !FileExists(path.c_str()))
                continue;

            Font font = LoadFontEx(path.c_str(), 64, nullptr, 0);
            if (font.texture.id == 0)
                continue;

            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
            uiFont = font;
            TraceLog(LOG_INFO, "SCENE EDITOR: loaded UI font %s", path.c_str());
            return;
        }

        TraceLog(LOG_WARNING, "SCENE EDITOR: UI font not found; using default");
    }

    void unloadUiFont()
    {
        if (uiFont.texture.id != 0)
            UnloadFont(uiFont);
        uiFont = Font{};
    }

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
        if (!resourceDirectoryExists(resourceDir))
            return files;

        const std::vector<std::string> names = listDirectoryFileNames(resourceDir);
        for (const std::string& name : names)
        {
            if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".json") == 0)
                files.push_back(name);
        }

        std::sort(files.begin(), files.end());
        return files;
    }

    bool loadActiveDocument()
    {
        loadError.clear();

        if (!resourceDirectoryExists(resourceDir))
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

    std::string getExitTarget(const std::string& sceneId, const std::string& direction) const
    {
        const nlohmann::json* scene = scenesDoc.sceneJson(sceneId);
        if (scene == nullptr || !scene->contains("exits") || !(*scene)["exits"].is_object())
            return "";
        if (!(*scene)["exits"].contains(direction) || !(*scene)["exits"][direction].is_string())
            return "";
        return (*scene)["exits"][direction].get<std::string>();
    }

    void setExitTarget(const std::string& sceneId, const std::string& direction, const std::string& targetId)
    {
        nlohmann::json* scene = scenesDoc.sceneJson(sceneId);
        if (scene == nullptr)
            return;

        if (!scene->contains("exits") || !(*scene)["exits"].is_object())
            (*scene)["exits"] = nlohmann::json::object();
        (*scene)["exits"][direction] = targetId;

        if (!scene->contains("movement") || !(*scene)["movement"].is_object())
            (*scene)["movement"] = nlohmann::json::object();
        (*scene)["movement"][direction] = true;
    }

    void recomputeLevelsFromExits()
    {
        if (!scenesDoc.isLoaded())
            return;

        const std::vector<std::string> ids = scenesDoc.sceneIds();
        std::map<std::string, std::vector<std::pair<std::string, int> > > edges;

        auto addEdge = [&](const std::string& fromId, const std::string& toId, int delta)
        {
            if (toId.empty() || !scenesDoc.hasScene(toId))
                return;
            edges[fromId].push_back(std::make_pair(toId, delta));
            edges[toId].push_back(std::make_pair(fromId, -delta));
        };

        for (const std::string& id : ids)
        {
            // Vertical links change floor; horizontal links stay on the same floor.
            addEdge(id, getExitTarget(id, "up"), 1);
            addEdge(id, getExitTarget(id, "down"), -1);
            addEdge(id, getExitTarget(id, "forward"), 0);
            addEdge(id, getExitTarget(id, "backward"), 0);
            addEdge(id, getExitTarget(id, "left"), 0);
            addEdge(id, getExitTarget(id, "right"), 0);
        }

        std::map<std::string, int> levels;
        std::queue<std::string> queue;

        auto seed = [&](const std::string& id, int level)
        {
            if (levels.count(id) != 0)
                return;
            levels[id] = level;
            queue.push(id);
        };

        for (const std::string& id : ids)
        {
            const nlohmann::json* scene = scenesDoc.sceneJson(id);
            if (scene != nullptr && scene->value("start", false))
                seed(id, 0);
        }

        if (levels.empty() && !ids.empty())
            seed(ids.front(), 0);

        while (!queue.empty())
        {
            const std::string current = queue.front();
            queue.pop();
            const int currentLevel = levels[current];

            const std::vector<std::pair<std::string, int> >& links = edges[current];
            for (size_t i = 0; i < links.size(); ++i)
            {
                const std::string& nextId = links[i].first;
                const int nextLevel = currentLevel + links[i].second;
                std::map<std::string, int>::iterator existing = levels.find(nextId);
                if (existing == levels.end())
                {
                    levels[nextId] = nextLevel;
                    queue.push(nextId);
                }
            }
        }

        // Seed remaining connected components (e.g. saloon cluster without start=true).
        for (const std::string& id : ids)
        {
            if (levels.count(id) != 0)
                continue;
            if (edges.count(id) == 0 || edges[id].empty())
                continue;

            levels[id] = 0;
            queue.push(id);
            while (!queue.empty())
            {
                const std::string current = queue.front();
                queue.pop();
                const int currentLevel = levels[current];
                const std::vector<std::pair<std::string, int> >& links = edges[current];
                for (size_t i = 0; i < links.size(); ++i)
                {
                    const std::string& nextId = links[i].first;
                    if (levels.count(nextId) != 0)
                        continue;
                    levels[nextId] = currentLevel + links[i].second;
                    queue.push(nextId);
                }
            }
        }

        for (const std::string& id : ids)
        {
            if (levels.count(id) == 0)
                continue;

            SceneLayout layout = scenesDoc.getLayout(id);
            layout.level = levels[id];
            scenesDoc.setLayout(id, layout);
        }
    }

    void getLevelRange(int& outMin, int& outMax) const
    {
        outMin = 0;
        outMax = 0;
        if (!scenesDoc.isLoaded())
            return;

        bool any = false;
        const std::vector<std::string> ids = scenesDoc.sceneIds();
        for (const std::string& id : ids)
        {
            const int level = scenesDoc.getLayout(id).level;
            if (!any)
            {
                outMin = level;
                outMax = level;
                any = true;
            }
            else
            {
                if (level < outMin)
                    outMin = level;
                if (level > outMax)
                    outMax = level;
            }
        }
    }

    int countScenesOnLevel(int level) const
    {
        int count = 0;
        const std::vector<std::string> ids = scenesDoc.sceneIds();
        for (const std::string& id : ids)
        {
            if (scenesDoc.getLayout(id).level == level)
                ++count;
        }
        return count;
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
                scenesDoc.setLayout(id, layout);
                x += kSceneCardWidth + 24.0f;
                if (x > 800.0f)
                {
                    x = 40.0f;
                    y += kSceneCardHeight + 24.0f;
                }
            }
        }

        recomputeLevelsFromExits();

        int minLevel = 0;
        int maxLevel = 0;
        getLevelRange(minLevel, maxLevel);
        if (canvasLevel < minLevel || canvasLevel > maxLevel)
            canvasLevel = 0;
        if (canvasLevel < minLevel)
            canvasLevel = minLevel;
        if (canvasLevel > maxLevel)
            canvasLevel = maxLevel;
    }

    void applyStackLink(bool placeAbove)
    {
        if (!scenesDoc.hasScene(stackSourceId) || !scenesDoc.hasScene(stackTargetId))
            return;

        if (placeAbove)
        {
            setExitTarget(stackTargetId, "up", stackSourceId);
            setExitTarget(stackSourceId, "down", stackTargetId);
        }
        else
        {
            setExitTarget(stackTargetId, "down", stackSourceId);
            setExitTarget(stackSourceId, "up", stackTargetId);
        }

        SceneLayout sourceLayout = scenesDoc.getLayout(stackSourceId);
        const SceneLayout targetLayout = scenesDoc.getLayout(stackTargetId);
        sourceLayout.x = targetLayout.x + 24.0f;
        sourceLayout.y = targetLayout.y + 24.0f;
        scenesDoc.setLayout(stackSourceId, sourceLayout);

        recomputeLevelsFromExits();
        canvasLevel = scenesDoc.getLayout(stackSourceId).level;
        selectedSceneId = stackSourceId;
        markDirty();
    }

    void closeStackDialog()
    {
        stackDialogOpen = false;
        stackSourceId.clear();
        stackTargetId.clear();
    }

    std::string findStackTarget(const Rectangle& ghost, Rectangle canvasBounds, const std::string& excludeId) const
    {
        const std::vector<std::string> ids = scenesDoc.sceneIds();
        for (const std::string& id : ids)
        {
            if (id == excludeId)
                continue;
            if (scenesDoc.getLayout(id).level != canvasLevel)
                continue;

            const Rectangle card = sceneCardBounds(id, canvasBounds);
            if (CheckCollisionRecs(ghost, card))
                return id;
        }
        return "";
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

        // Match SceneLoader: prefer .png.xz (git-stored) then uncompressed paths.
        const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, imagePath);
        for (const std::string& path : paths)
        {
            const std::string compressedPath = compressedAssetPath(path);
            if (FileExists(compressedPath.c_str()) &&
                loadTextureFromAssetFile(compressedPath, entry.texture))
            {
                entry.loaded = true;
                return entry;
            }

            if (FileExists(path.c_str()) &&
                loadTextureFromAssetFile(path, entry.texture))
            {
                entry.loaded = true;
                return entry;
            }
        }

        // Also search under the resource directory itself (resourceDir may be a
        // symlink beside the binary while image paths are resources/images/...).
        const std::string underResources = pathJoin(parentDirectory(resourceDir), imagePath);
        if (!underResources.empty())
        {
            const std::string compressedPath = compressedAssetPath(underResources);
            if (FileExists(compressedPath.c_str()) &&
                loadTextureFromAssetFile(compressedPath, entry.texture))
            {
                entry.loaded = true;
                return entry;
            }

            if (FileExists(underResources.c_str()) &&
                loadTextureFromAssetFile(underResources, entry.texture))
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
            DrawTextEx(textFont(), "No resource JSON files",
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
            const Vector2 textSize = MeasureTextEx(textFont(), label.c_str(), fontSize, 1.0f);
            DrawTextEx(
                textFont(),
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
                textFont(),
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

            const int sceneLevel = scenesDoc.getLayout(id).level;
            DrawTextEx(textFont(), id.c_str(), {row.x + kListThumbSize + 12.0f, row.y + 8.0f},
                       14.0f, 1.0f, kTextPrimary);
            DrawTextEx(
                textFont(),
                TextFormat("L%d", sceneLevel),
                {row.x + kListThumbSize + 12.0f, row.y + 28.0f},
                12.0f,
                1.0f,
                kTextMuted);

            const bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
            if (!stackDialogOpen && hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                selectedSceneId = id;
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

            const bool hasUp = !getExitTarget(id, "up").empty();
            const bool hasDown = !getExitTarget(id, "down").empty();
            if (!hasUp && !hasDown)
                continue;

            const Rectangle card = sceneCardBounds(id, canvasBounds);
            float iconX = card.x + card.width - 20.0f;
            if (hasUp)
            {
                DrawTextEx(textFont(), "^", {iconX, card.y + 4.0f}, 16.0f, 1.0f, kPanelBorder);
                iconX -= 14.0f;
            }
            if (hasDown)
            {
                DrawTextEx(textFont(), "v", {iconX, card.y + 4.0f}, 16.0f, 1.0f, kPanelBorder);
            }
        }
    }

    void drawLevelChrome(Rectangle canvasBounds)
    {
        int minLevel = 0;
        int maxLevel = 0;
        getLevelRange(minLevel, maxLevel);
        const bool canGoDown = scenesDoc.isLoaded() && canvasLevel > minLevel;
        const bool canGoUp = scenesDoc.isLoaded() && canvasLevel < maxLevel;
        const int onLevel = scenesDoc.isLoaded() ? countScenesOnLevel(canvasLevel) : 0;

        const std::string levelLabel = TextFormat(
            "Floor level %d  |  range %d to %d  |  %d scene(s) here",
            canvasLevel,
            minLevel,
            maxLevel,
            onLevel);
        DrawTextEx(
            textFont(),
            levelLabel.c_str(),
            {canvasBounds.x + 12.0f, canvasBounds.y + 10.0f},
            14.0f,
            1.0f,
            kTextPrimary);

        const Rectangle levelDownBtn = {
            canvasBounds.x + canvasBounds.width - 76.0f,
            canvasBounds.y + 8.0f,
            30.0f,
            24.0f};
        const Rectangle levelUpBtn = {
            canvasBounds.x + canvasBounds.width - 40.0f,
            canvasBounds.y + 8.0f,
            30.0f,
            24.0f};

        DrawRectangleRec(levelDownBtn, canGoDown ? kPanelAccent : kButtonDisabled);
        DrawRectangleRec(levelUpBtn, canGoUp ? kPanelAccent : kButtonDisabled);
        DrawRectangleLinesEx(levelDownBtn, 1.0f, canGoDown ? kPanelBorder : kTextDisabled);
        DrawRectangleLinesEx(levelUpBtn, 1.0f, canGoUp ? kPanelBorder : kTextDisabled);
        DrawTextEx(
            textFont(),
            "-",
            {levelDownBtn.x + 10.0f, levelDownBtn.y + 3.0f},
            16.0f,
            1.0f,
            canGoDown ? kTextPrimary : kTextDisabled);
        DrawTextEx(
            textFont(),
            "+",
            {levelUpBtn.x + 9.0f, levelUpBtn.y + 3.0f},
            16.0f,
            1.0f,
            canGoUp ? kTextPrimary : kTextDisabled);

        if (!stackDialogOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            const Vector2 mouse = GetMousePosition();
            if (canGoDown && CheckCollisionPointRec(mouse, levelDownBtn))
                canvasLevel -= 1;
            if (canGoUp && CheckCollisionPointRec(mouse, levelUpBtn))
                canvasLevel += 1;
        }
    }

    void drawCanvas(Rectangle canvasBounds)
    {
        DrawRectangleRec(canvasBounds, kCanvasBg);
        drawLevelChrome(canvasBounds);

        if (!scenesDoc.isLoaded())
        {
            const std::string message = loadError.empty()
                ? "Select the scenes.json tab to edit the scene map."
                : loadError;
            drawWrappedText(
                textFont(),
                message,
                {canvasBounds.x + 20.0f, canvasBounds.y + 44.0f},
                canvasBounds.width - 40.0f,
                15.0f,
                5.0f,
                kTextMuted);
            return;
        }

        BeginScissorMode(
            static_cast<int>(canvasBounds.x),
            static_cast<int>(canvasBounds.y + 36.0f),
            static_cast<int>(canvasBounds.width),
            static_cast<int>(canvasBounds.height - 36.0f));

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

            DrawTextEx(textFont(), id.c_str(), {card.x + 6.0f, card.y + card.height - 22.0f},
                       12.0f, 1.0f, kTextPrimary);

            if (!stackDialogOpen)
            {
                const bool hovered = CheckCollisionPointRec(GetMousePosition(), card);
                if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                {
                    selectedSceneId = id;
                    dragSource = DragSource::Canvas;
                    dragSceneId = id;
                    dragOffset = {GetMouseX() - card.x, GetMouseY() - card.y};
                }
            }
        }

        drawStairIcons(canvasBounds);

        if (!stackDialogOpen &&
            dragSource != DragSource::None &&
            !dragSceneId.empty() &&
            IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            const Rectangle ghost = {
                static_cast<float>(GetMouseX()) - dragOffset.x,
                static_cast<float>(GetMouseY()) - dragOffset.y,
                kSceneCardWidth,
                kSceneCardHeight};
            DrawRectangleRec(ghost, Color{80, 70, 50, 120});
            DrawRectangleLinesEx(ghost, 1.0f, kPanelBorder);

            const std::string hoverTarget = findStackTarget(ghost, canvasBounds, dragSceneId);
            if (!hoverTarget.empty())
            {
                const Rectangle targetCard = sceneCardBounds(hoverTarget, canvasBounds);
                DrawRectangleLinesEx(targetCard, 2.0f, Color{220, 180, 80, 255});
                DrawTextEx(
                    textFont(),
                    "Drop for Up / Down / Cancel",
                    {targetCard.x, targetCard.y - 18.0f},
                    12.0f,
                    1.0f,
                    kPanelBorder);
            }
        }

        if (!stackDialogOpen &&
            dragSource != DragSource::None &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            if (CheckCollisionPointRec(GetMousePosition(), canvasBounds) &&
                scenesDoc.hasScene(dragSceneId))
            {
                const float dropX =
                    static_cast<float>(GetMouseX()) - canvasBounds.x - dragOffset.x - canvasScroll.x;
                const float dropY =
                    static_cast<float>(GetMouseY()) - canvasBounds.y - dragOffset.y - canvasScroll.y;
                const Rectangle ghost = {
                    static_cast<float>(GetMouseX()) - dragOffset.x,
                    static_cast<float>(GetMouseY()) - dragOffset.y,
                    kSceneCardWidth,
                    kSceneCardHeight};
                const std::string targetId = findStackTarget(ghost, canvasBounds, dragSceneId);

                if (!targetId.empty())
                {
                    stackDialogOpen = true;
                    stackSourceId = dragSceneId;
                    stackTargetId = targetId;
                    stackPendingX = dropX;
                    stackPendingY = dropY;
                }
                else
                {
                    SceneLayout layout = scenesDoc.getLayout(dragSceneId);
                    layout.x = dropX;
                    layout.y = dropY;
                    layout.level = canvasLevel;
                    scenesDoc.setLayout(dragSceneId, layout);
                    selectedSceneId = dragSceneId;
                    markDirty();
                }
            }

            dragSource = DragSource::None;
            dragSceneId.clear();
        }

        EndScissorMode();

        if (!stackDialogOpen && CheckCollisionPointRec(GetMousePosition(), canvasBounds))
        {
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
                canvasScroll.x += GetMouseWheelMove() * 24.0f;
            else if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                canvasScroll.y += GetMouseWheelMove() * 24.0f;
            else
                canvasScroll.y -= GetMouseWheelMove() * 24.0f;
        }
    }

    void drawStackDialog(int screenWidth, int screenHeight)
    {
        if (!stackDialogOpen)
            return;

        DrawRectangle(0, 0, screenWidth, screenHeight, kModalOverlay);

        const float dialogW = 420.0f;
        const float dialogH = 220.0f;
        const Rectangle dialog = {
            (static_cast<float>(screenWidth) - dialogW) * 0.5f,
            (static_cast<float>(screenHeight) - dialogH) * 0.5f,
            dialogW,
            dialogH};
        DrawRectangleRounded(dialog, 0.04f, 8, kModalFill);
        DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

        DrawTextEx(
            textFont(),
            "Stack scene floors",
            {dialog.x + 20.0f, dialog.y + 18.0f},
            18.0f,
            1.0f,
            kTextPrimary);

        const std::string body = TextFormat(
            "Place \"%s\" relative to \"%s\"?",
            stackSourceId.c_str(),
            stackTargetId.c_str());
        drawWrappedText(
            textFont(),
            body,
            {dialog.x + 20.0f, dialog.y + 52.0f},
            dialogW - 40.0f,
            14.0f,
            4.0f,
            kTextMuted);

        DrawTextEx(
            textFont(),
            "Up = one floor above  |  Down = one floor below",
            {dialog.x + 20.0f, dialog.y + 100.0f},
            12.0f,
            1.0f,
            kTextMuted);

        const float btnW = 110.0f;
        const float btnH = 34.0f;
        const float btnY = dialog.y + dialogH - btnH - 20.0f;
        const Rectangle upBtn = {dialog.x + 20.0f, btnY, btnW, btnH};
        const Rectangle downBtn = {dialog.x + 150.0f, btnY, btnW, btnH};
        const Rectangle cancelBtn = {dialog.x + 280.0f, btnY, btnW, btnH};

        auto drawButton = [&](Rectangle bounds, const char* label, bool accent)
        {
            DrawRectangleRec(bounds, accent ? kPanelAccent : Color{44, 42, 52, 255});
            DrawRectangleLinesEx(bounds, 1.0f, kPanelBorder);
            const Vector2 size = MeasureTextEx(textFont(), label, 14.0f, 1.0f);
            DrawTextEx(
                textFont(),
                label,
                {bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + 9.0f},
                14.0f,
                1.0f,
                kTextPrimary);
        };

        drawButton(upBtn, "Up", true);
        drawButton(downBtn, "Down", true);
        drawButton(cancelBtn, "Cancel", false);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            const Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, upBtn))
            {
                applyStackLink(true);
                closeStackDialog();
            }
            else if (CheckCollisionPointRec(mouse, downBtn))
            {
                applyStackLink(false);
                closeStackDialog();
            }
            else if (CheckCollisionPointRec(mouse, cancelBtn) || !CheckCollisionPointRec(mouse, dialog))
            {
                closeStackDialog();
            }
        }

        if (IsKeyPressed(KEY_ESCAPE))
            closeStackDialog();
    }

    std::string truncate(const std::string& text, size_t maxLen) const
    {
        if (text.size() <= maxLen)
            return text;
        return text.substr(0, maxLen - 3) + "...";
    }

    void drawVariablesPane(Rectangle paneBounds)
    {
        DrawTextEx(textFont(), "Scene Variables", {paneBounds.x + 12.0f, paneBounds.y + 8.0f},
                   15.0f, 1.0f, kTextMuted);

        if (selectedSceneId.empty() || !scenesDoc.hasScene(selectedSceneId))
        {
            DrawTextEx(textFont(), "Select a scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
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
            DrawTextEx(textFont(), line.c_str(), {paneBounds.x + 12.0f, y}, 13.0f, 1.0f, kTextPrimary);
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
        DrawTextEx(textFont(), "Actors", {paneBounds.x + 12.0f, paneBounds.y + 8.0f},
                   15.0f, 1.0f, kTextMuted);

        if (selectedSceneId.empty() || !scenesDoc.hasScene(selectedSceneId))
        {
            DrawTextEx(textFont(), "Select a scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
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
            DrawTextEx(textFont(), "(no actors)", {paneBounds.x + 12.0f, y},
                       13.0f, 1.0f, kTextMuted);
            y += rowHeight;
        }
        else
        {
            for (const SceneActor& actor : actors)
            {
                const std::string line = actor.id + " — " + actor.name +
                    (actor.role.empty() ? "" : " (" + actor.role + ")");
                DrawTextEx(textFont(), line.c_str(), {paneBounds.x + 12.0f, y},
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
        DrawTextEx(textFont(), pathLabel.c_str(), {8.0f, static_cast<float>(screenHeight) - 18.0f},
                   12.0f, 1.0f, kTextMuted);
        DrawTextEx(textFont(), status.c_str(),
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
        if (!stackDialogOpen)
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
        drawStackDialog(screenWidth, screenHeight);

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
        app.assetRoot = (argc >= 3) ? argv[2] : "";
    }

    if (!ensureValidResourcePaths(app.resourceDir, app.assetRoot))
    {
        TraceLog(
            LOG_WARNING,
            "SCENE EDITOR: scenes.json not found under resources (%s)",
            app.resourceDir.c_str());
    }
    else
    {
        TraceLog(
            LOG_INFO,
            "SCENE EDITOR: using resources at %s",
            app.resourceDir.c_str());
    }

    app.loadUiFont();
    app.initLayout(screenWidth, screenHeight);
    app.refreshTabs();
    app.loadActiveDocument();

    while (!WindowShouldClose())
    {
        app.update();
        app.draw();
    }

    app.unloadThumbnails();
    app.unloadUiFont();
    app.saveDocument();
    CloseWindow();
    return 0;
}