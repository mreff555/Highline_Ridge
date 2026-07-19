#include "ImageCompression.h"
#include "PlatformPath.h"
#include "RaylibCompat.h"
#include "SceneDocument.h"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <filesystem>
#include <map>
#include <queue>
#include <set>
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
const Color kPanelInnerEdge = {48, 42, 54, 255};
const Color kDividerTrack = {16, 14, 20, 255};
const Color kDividerGrip = {110, 92, 52, 255};
const Color kDividerGripActive = {188, 158, 88, 255};
const Color kTextPrimary = {220, 212, 196, 255};
const Color kTextMuted = {132, 122, 104, 255};
const Color kCanvasBg = {18, 17, 22, 255};
const Color kSelection = {120, 96, 48, 180};
const Color kExitArrow = {168, 138, 72, 220};
const Color kButtonDisabled = {48, 46, 54, 255};
const Color kTextDisabled = {90, 86, 96, 255};
const Color kModalOverlay = {0, 0, 0, 160};
const Color kModalFill = {32, 30, 40, 255};

const float kDividerSize = 8.0f;
const float kDividerHitPadding = 6.0f; // extra grab area beyond the visible grip
const float kPanelRoundness = 0.03f;
const float kPanelBorderThick = 2.0f;
const float kStatusBarHeight = 22.0f;
const float kTopAreaRatio = 2.0f / 3.0f;
const float kLeftPaneRatio = 0.4f; // was 0.2; doubled so scene labels stay readable
const float kMinLeftWidth = 320.0f;
const float kMinMainWidth = 280.0f;
const float kMinTopHeight = 200.0f;
const float kMinBottomHeight = 140.0f;
const float kTabHeight = 30.0f;
const float kSceneCardWidth = 140.0f;
const float kSceneCardHeight = 100.0f;
// Corridors between cards for mid-route turns (endpoints stay flush with card edges).
const float kLayoutGapX = 96.0f;
const float kLayoutGapY = 96.0f;
// How far into the corridor the perpendicular exit/enter stubs travel before turning.
const float kLinkStubLength = 28.0f;
const float kArrowHeadLength = 12.0f;
const float kArrowHeadHalfWidth = 5.0f;
const float kLinkEndCapRadius = 7.0f;
const float kLayoutOriginX = 40.0f;
const float kLayoutOriginY = 48.0f;
const float kListThumbSize = 48.0f;
const float kListRowHeight = 56.0f;
const float kCanvasChromeHeight = 36.0f;
const float kScrollBarSize = 14.0f;
const float kScrollContentPad = 48.0f;
const Color kScrollTrack = {22, 20, 28, 255};
const Color kScrollThumb = {110, 92, 52, 255};
const Color kScrollThumbActive = {168, 138, 72, 255};

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
    Font uiFontBold{};

    std::vector<std::string> jsonTabs;
    int activeTabIndex = 0;

    std::string selectedSceneId;
    int canvasLevel = 0;

    float leftPaneWidth = 0.0f;
    float topAreaHeight = 0.0f;
    bool userResizedLeftSplit = false;
    bool userResizedTopSplit = false;
    int lastScreenWidth = 0;
    int lastScreenHeight = 0;

    float leftScroll = 0.0f;
    float variablesScroll = 0.0f;
    float actorsScroll = 0.0f;
    Vector2 canvasScroll{0.0f, 0.0f};
    bool draggingHScroll = false;
    bool draggingVScroll = false;
    float hScrollGrabOffset = 0.0f;
    float vScrollGrabOffset = 0.0f;

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

    bool variableEditorOpen = false;
    std::string variableEditorSceneId;
    std::string variableEditorKey;
    std::string variableEditorBuffer;
    enum VariableValueKind
    {
        VariableKindString,
        VariableKindBool,
        VariableKindInteger,
        VariableKindFloat,
        VariableKindJson
    };
    VariableValueKind variableEditorKind = VariableKindString;
    bool variableEditorMultiline = false;
    int variableEditorCursor = 0;
    float variableEditorScrollY = 0.0f;
    std::string selectedVariableKey;
    std::string variableEditorError;
    int variableEditorIgnoreInputFrames = 0;

    std::map<std::string, ThumbnailEntry> thumbnails;
    bool dirty = false;

    Font textFont() const
    {
        if (uiFont.texture.id != 0)
            return uiFont;
        return GetFontDefault();
    }

    Font boldFont() const
    {
        if (uiFontBold.texture.id != 0)
            return uiFontBold;
        return textFont();
    }

    Font tryLoadFont(const std::string candidates[], size_t count) const
    {
        for (size_t i = 0; i < count; ++i)
        {
            const std::string& path = candidates[i];
            if (path.empty() || !FileExists(path.c_str()))
                continue;

            Font font = LoadFontEx(path.c_str(), 64, nullptr, 0);
            if (font.texture.id == 0)
                continue;

            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
            TraceLog(LOG_INFO, "SCENE EDITOR: loaded UI font %s", path.c_str());
            return font;
        }

        return Font{};
    }

    void loadUiFont()
    {
        if (uiFont.texture.id != 0)
        {
            UnloadFont(uiFont);
            uiFont = Font{};
        }
        if (uiFontBold.texture.id != 0)
        {
            UnloadFont(uiFontBold);
            uiFontBold = Font{};
        }

        const std::string regularCandidates[] = {
            pathJoin(resourceDir, "fonts/CourierPrime-Regular.ttf"),
            pathJoin(assetRoot, "resources/fonts/CourierPrime-Regular.ttf"),
            pathJoin(assetRoot, "fonts/CourierPrime-Regular.ttf"),
            "/System/Library/Fonts/Supplemental/Courier New.ttf",
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Helvetica.ttc"
        };
        const std::string boldCandidates[] = {
            pathJoin(resourceDir, "fonts/CourierPrime-Bold.ttf"),
            pathJoin(assetRoot, "resources/fonts/CourierPrime-Bold.ttf"),
            pathJoin(assetRoot, "fonts/CourierPrime-Bold.ttf"),
            "/System/Library/Fonts/Supplemental/Courier New Bold.ttf",
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
        };

        uiFont = tryLoadFont(regularCandidates, sizeof(regularCandidates) / sizeof(regularCandidates[0]));
        uiFontBold = tryLoadFont(boldCandidates, sizeof(boldCandidates) / sizeof(boldCandidates[0]));

        if (uiFont.texture.id == 0)
            TraceLog(LOG_WARNING, "SCENE EDITOR: UI font not found; using default");
        if (uiFontBold.texture.id == 0)
            TraceLog(LOG_WARNING, "SCENE EDITOR: bold UI font not found; stair icons use regular");
    }

    void unloadUiFont()
    {
        if (uiFont.texture.id != 0)
            UnloadFont(uiFont);
        if (uiFontBold.texture.id != 0 && uiFontBold.texture.id != uiFont.texture.id)
            UnloadFont(uiFontBold);
        uiFont = Font{};
        uiFontBold = Font{};
    }

    float contentHeight(int screenHeight) const
    {
        const float height = static_cast<float>(screenHeight) - kStatusBarHeight;
        return height > 1.0f ? height : 1.0f;
    }

    void applyDefaultTopSplit(int screenHeight)
    {
        // Upper browser + canvas occupy 2/3 of the content area (above the status bar).
        topAreaHeight = contentHeight(screenHeight) * kTopAreaRatio;
    }

    void initLayout(int screenWidth, int screenHeight)
    {
        leftPaneWidth = static_cast<float>(screenWidth) * kLeftPaneRatio;
        applyDefaultTopSplit(screenHeight);
        userResizedLeftSplit = false;
        userResizedTopSplit = false;
        lastScreenWidth = screenWidth;
        lastScreenHeight = screenHeight;
        clampLayout(screenWidth, screenHeight);
    }

    void syncLayoutToWindow(int screenWidth, int screenHeight)
    {
        if (screenWidth == lastScreenWidth && screenHeight == lastScreenHeight)
            return;

        if (!userResizedTopSplit || lastScreenHeight <= 0)
        {
            applyDefaultTopSplit(screenHeight);
        }
        else
        {
            const float previousContent = contentHeight(lastScreenHeight);
            const float ratio = previousContent > 1.0f ? (topAreaHeight / previousContent) : kTopAreaRatio;
            topAreaHeight = contentHeight(screenHeight) * ratio;
        }

        if (!userResizedLeftSplit || lastScreenWidth <= 0)
        {
            leftPaneWidth = static_cast<float>(screenWidth) * kLeftPaneRatio;
        }
        else
        {
            leftPaneWidth *= static_cast<float>(screenWidth) / static_cast<float>(lastScreenWidth);
        }

        lastScreenWidth = screenWidth;
        lastScreenHeight = screenHeight;
        clampLayout(screenWidth, screenHeight);
    }

    void clampLayout(int screenWidth, int screenHeight)
    {
        const float maxLeft =
            static_cast<float>(screenWidth) - kMinMainWidth - kDividerSize;
        if (leftPaneWidth < kMinLeftWidth)
            leftPaneWidth = kMinLeftWidth;
        if (leftPaneWidth > maxLeft)
            leftPaneWidth = maxLeft;

        const float contentH = contentHeight(screenHeight);
        const float maxTop = contentH - kMinBottomHeight - kDividerSize;
        if (topAreaHeight < kMinTopHeight)
            topAreaHeight = kMinTopHeight;
        if (topAreaHeight > maxTop)
            topAreaHeight = maxTop;
        if (topAreaHeight < 1.0f)
            topAreaHeight = contentH * kTopAreaRatio;
    }

    Rectangle expandHitRect(Rectangle bounds, float pad, bool vertical) const
    {
        if (vertical)
        {
            return {
                bounds.x - pad,
                bounds.y,
                bounds.width + pad * 2.0f,
                bounds.height};
        }

        return {
            bounds.x,
            bounds.y - pad,
            bounds.width,
            bounds.height + pad * 2.0f};
    }

    bool isDraggingDivider() const
    {
        return draggingVerticalDivider || draggingHorizontalDivider;
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
        const float contentH = contentHeight(screenHeight);
        return {
            0.0f,
            topAreaHeight + kDividerSize,
            static_cast<float>(screenWidth),
            contentH - topAreaHeight - kDividerSize};
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
        // Prefer vertical (non-zero) level deltas when both exist between a pair
        // (e.g. up to summit must win over a mistaken same-floor back link).
        std::map<std::string, int> directedDelta;

        auto edgeKey = [](const std::string& fromId, const std::string& toId) -> std::string
        {
            return fromId + "\n" + toId;
        };

        auto addEdge = [&](const std::string& fromId, const std::string& toId, int delta)
        {
            if (toId.empty() || !scenesDoc.hasScene(toId))
                return;

            const std::string key = edgeKey(fromId, toId);
            std::map<std::string, int>::iterator existing = directedDelta.find(key);
            if (existing == directedDelta.end())
            {
                directedDelta[key] = delta;
                return;
            }

            // Keep vertical relationships over same-floor links.
            if (existing->second == 0 && delta != 0)
                existing->second = delta;
            else if (existing->second != 0 && delta != 0 && existing->second != delta)
                existing->second = delta; // last non-zero wins; rare conflict
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

        std::map<std::string, std::vector<std::pair<std::string, int> > > edges;
        for (std::map<std::string, int>::const_iterator it = directedDelta.begin();
             it != directedDelta.end();
             ++it)
        {
            const std::string& key = it->first;
            const size_t split = key.find('\n');
            if (split == std::string::npos)
                continue;
            const std::string fromId = key.substr(0, split);
            const std::string toId = key.substr(split + 1);
            const int delta = it->second;
            edges[fromId].push_back(std::make_pair(toId, delta));

            // Bidirectional connectivity for BFS; reverse may already be explicit.
            const std::string reverseKey = edgeKey(toId, fromId);
            if (directedDelta.count(reverseKey) == 0)
                edges[toId].push_back(std::make_pair(fromId, -delta));
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

    std::vector<std::string> scenesOnLevel(int level) const
    {
        std::vector<std::string> out;
        const std::vector<std::string> ids = scenesDoc.sceneIds();
        for (const std::string& id : ids)
        {
            if (scenesDoc.getLayout(id).level == level)
                out.push_back(id);
        }
        return out;
    }

    bool isSameLevelLink(const std::string& fromId, const std::string& toId) const
    {
        if (!scenesDoc.hasScene(fromId) || !scenesDoc.hasScene(toId))
            return false;
        return scenesDoc.getLayout(fromId).level == scenesDoc.getLayout(toId).level;
    }

    bool directionDelta(const std::string& direction, int& outDCol, int& outDRow) const
    {
        if (direction == "right")
        {
            outDCol = 1;
            outDRow = 0;
            return true;
        }
        if (direction == "left")
        {
            outDCol = -1;
            outDRow = 0;
            return true;
        }
        // forward = "into" the room / up the screen; backward = toward the viewer.
        if (direction == "forward")
        {
            outDCol = 0;
            outDRow = -1;
            return true;
        }
        if (direction == "backward")
        {
            outDCol = 0;
            outDRow = 1;
            return true;
        }
        return false;
    }

    std::string cellKey(int col, int row) const
    {
        return std::to_string(col) + "," + std::to_string(row);
    }

    void autoLayoutLevel(int level)
    {
        const std::vector<std::string> levelIds = scenesOnLevel(level);
        if (levelIds.empty())
            return;

        std::map<std::string, std::vector<std::pair<std::string, std::string> > > neighbors;
        for (const std::string& id : levelIds)
        {
            const char* dirs[] = {"forward", "backward", "left", "right"};
            for (size_t i = 0; i < 4; ++i)
            {
                const std::string target = getExitTarget(id, dirs[i]);
                if (target.empty() || !isSameLevelLink(id, target))
                    continue;
                neighbors[id].push_back(std::make_pair(target, std::string(dirs[i])));
            }
        }

        std::map<std::string, std::pair<int, int> > grid; // id -> (col,row)
        std::set<std::string> occupiedCells;
        std::set<std::string> placed;

        auto placeAt = [&](const std::string& id, int col, int row)
        {
            grid[id] = std::make_pair(col, row);
            occupiedCells.insert(cellKey(col, row));
            placed.insert(id);
        };

        auto isFree = [&](int col, int row) -> bool
        {
            return occupiedCells.count(cellKey(col, row)) == 0;
        };

        auto findFree = [&](int preferredCol, int preferredRow, int& outCol, int& outRow) -> bool
        {
            if (isFree(preferredCol, preferredRow))
            {
                outCol = preferredCol;
                outRow = preferredRow;
                return true;
            }
            for (int radius = 1; radius <= 24; ++radius)
            {
                for (int dCol = -radius; dCol <= radius; ++dCol)
                {
                    for (int dRow = -radius; dRow <= radius; ++dRow)
                    {
                        if (std::abs(dCol) != radius && std::abs(dRow) != radius)
                            continue;
                        const int col = preferredCol + dCol;
                        const int row = preferredRow + dRow;
                        if (isFree(col, row))
                        {
                            outCol = col;
                            outRow = row;
                            return true;
                        }
                    }
                }
            }
            return false;
        };

        // Prefer start scene roots, then alphabetical component seeds.
        std::vector<std::string> seeds;
        for (const std::string& id : levelIds)
        {
            const nlohmann::json* scene = scenesDoc.sceneJson(id);
            if (scene != nullptr && scene->value("start", false))
                seeds.push_back(id);
        }
        for (const std::string& id : levelIds)
        {
            if (std::find(seeds.begin(), seeds.end(), id) == seeds.end())
                seeds.push_back(id);
        }

        int componentOffsetCol = 0;

        for (size_t seedIndex = 0; seedIndex < seeds.size(); ++seedIndex)
        {
            const std::string& seedId = seeds[seedIndex];
            if (placed.count(seedId) != 0)
                continue;

            int seedCol = 0;
            int seedRow = 0;
            if (!findFree(componentOffsetCol, 0, seedCol, seedRow))
                continue;
            placeAt(seedId, seedCol, seedRow);

            std::queue<std::string> queue;
            queue.push(seedId);

            while (!queue.empty())
            {
                const std::string current = queue.front();
                queue.pop();
                const std::pair<int, int> currentCell = grid[current];

                const std::vector<std::pair<std::string, std::string> >& links = neighbors[current];
                for (size_t i = 0; i < links.size(); ++i)
                {
                    const std::string& nextId = links[i].first;
                    if (placed.count(nextId) != 0)
                        continue;

                    int dCol = 0;
                    int dRow = 0;
                    int preferredCol = currentCell.first;
                    int preferredRow = currentCell.second;
                    if (directionDelta(links[i].second, dCol, dRow))
                    {
                        preferredCol += dCol;
                        preferredRow += dRow;
                    }
                    else
                    {
                        preferredCol += 1;
                    }

                    int freeCol = preferredCol;
                    int freeRow = preferredRow;
                    if (!findFree(preferredCol, preferredRow, freeCol, freeRow))
                        continue;

                    placeAt(nextId, freeCol, freeRow);
                    queue.push(nextId);
                }
            }

            // Next disconnected cluster starts to the right of this one.
            int maxCol = componentOffsetCol;
            for (std::map<std::string, std::pair<int, int> >::const_iterator it = grid.begin();
                 it != grid.end();
                 ++it)
            {
                if (it->second.first > maxCol)
                    maxCol = it->second.first;
            }
            componentOffsetCol = maxCol + 2;
        }

        // Place any remaining isolates.
        for (const std::string& id : levelIds)
        {
            if (placed.count(id) != 0)
                continue;
            int freeCol = 0;
            int freeRow = 0;
            if (!findFree(componentOffsetCol, 0, freeCol, freeRow))
                freeCol = componentOffsetCol;
            placeAt(id, freeCol, freeRow);
            componentOffsetCol = freeCol + 2;
        }

        int minCol = 0;
        int minRow = 0;
        bool any = false;
        for (std::map<std::string, std::pair<int, int> >::const_iterator it = grid.begin();
             it != grid.end();
             ++it)
        {
            if (!any)
            {
                minCol = it->second.first;
                minRow = it->second.second;
                any = true;
            }
            else
            {
                minCol = std::min(minCol, it->second.first);
                minRow = std::min(minRow, it->second.second);
            }
        }

        const float pitchX = kSceneCardWidth + kLayoutGapX;
        const float pitchY = kSceneCardHeight + kLayoutGapY;
        for (std::map<std::string, std::pair<int, int> >::const_iterator it = grid.begin();
             it != grid.end();
             ++it)
        {
            SceneLayout layout = scenesDoc.getLayout(it->first);
            layout.x = kLayoutOriginX + static_cast<float>(it->second.first - minCol) * pitchX;
            layout.y = kLayoutOriginY + static_cast<float>(it->second.second - minRow) * pitchY;
            layout.level = level;
            scenesDoc.setLayout(it->first, layout);
        }
    }

    void autoLayoutAllLevels()
    {
        int minLevel = 0;
        int maxLevel = 0;
        getLevelRange(minLevel, maxLevel);
        for (int level = minLevel; level <= maxLevel; ++level)
            autoLayoutLevel(level);
    }

    void ensureDefaultLayouts()
    {
        if (!scenesDoc.isLoaded())
            return;

        recomputeLevelsFromExits();
        autoLayoutAllLevels();

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

        recomputeLevelsFromExits();
        autoLayoutAllLevels();
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
        DrawRectangleRounded(bounds, kPanelRoundness, 10, kPanelFill);
        DrawRoundedBorder(bounds, kPanelRoundness, 10, kPanelBorderThick, kPanelBorder);
        // Subtle inner edge so pane chrome reads cleanly against dark content.
        const Rectangle inner = {
            bounds.x + kPanelBorderThick + 1.0f,
            bounds.y + kPanelBorderThick + 1.0f,
            bounds.width - (kPanelBorderThick + 1.0f) * 2.0f,
            bounds.height - (kPanelBorderThick + 1.0f) * 2.0f};
        if (inner.width > 4.0f && inner.height > 4.0f)
            DrawRoundedBorder(inner, kPanelRoundness, 10, 1.0f, kPanelInnerEdge);
    }

    void drawDivider(Rectangle bounds, bool active, bool vertical) const
    {
        DrawRectangleRec(bounds, kDividerTrack);

        if (vertical)
        {
            const float midX = bounds.x + bounds.width * 0.5f;
            DrawLineEx(
                {midX, bounds.y + 10.0f},
                {midX, bounds.y + bounds.height - 10.0f},
                active ? 2.0f : 1.5f,
                active ? kDividerGripActive : kDividerGrip);

            // Grip ticks in the middle of the vertical split.
            const float midY = bounds.y + bounds.height * 0.5f;
            for (int i = -1; i <= 1; ++i)
            {
                const float y = midY + static_cast<float>(i) * 6.0f;
                DrawLineEx(
                    {bounds.x + 1.5f, y},
                    {bounds.x + bounds.width - 1.5f, y},
                    1.5f,
                    active ? kDividerGripActive : kDividerGrip);
            }
        }
        else
        {
            const float midY = bounds.y + bounds.height * 0.5f;
            DrawLineEx(
                {bounds.x + 10.0f, midY},
                {bounds.x + bounds.width - 10.0f, midY},
                active ? 2.0f : 1.5f,
                active ? kDividerGripActive : kDividerGrip);

            const float midX = bounds.x + bounds.width * 0.5f;
            for (int i = -1; i <= 1; ++i)
            {
                const float x = midX + static_cast<float>(i) * 6.0f;
                DrawLineEx(
                    {x, bounds.y + 1.5f},
                    {x, bounds.y + bounds.height - 1.5f},
                    1.5f,
                    active ? kDividerGripActive : kDividerGrip);
            }
        }
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

            std::string label = jsonTabs[i];
            if (label.size() > 5 && label.compare(label.size() - 5, 5, ".json") == 0)
                label.resize(label.size() - 5);
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

            y += kListRowHeight;
        }

        EndScissorMode();

        // Hit-test only within the visible list (scissor-safe index math).
        const Vector2 mouse = GetMousePosition();
        if (!stackDialogOpen &&
            !variableEditorOpen &&
            !isDraggingDivider() &&
            CheckCollisionPointRec(mouse, listBounds) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            const float localY = (mouse.y - listBounds.y) + leftScroll;
            if (localY >= 0.0f)
            {
                const int index = static_cast<int>(localY / kListRowHeight);
                if (index >= 0 && index < static_cast<int>(ids.size()))
                {
                    const std::string& id = ids[static_cast<size_t>(index)];
                    if (selectedSceneId != id)
                    {
                        selectedSceneId = id;
                        selectedVariableKey.clear();
                        variablesScroll = 0.0f;
                    }
                    dragSource = DragSource::SceneList;
                    dragSceneId = id;
                    const float rowTop = listBounds.y - leftScroll + static_cast<float>(index) * kListRowHeight;
                    dragOffset = {mouse.x - listBounds.x - 4.0f, mouse.y - rowTop};
                }
            }
        }

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

    bool segmentIntersectsRect(Vector2 a, Vector2 b, Rectangle rect, float pad) const
    {
        const Rectangle inflated = {
            rect.x - pad,
            rect.y - pad,
            rect.width + pad * 2.0f,
            rect.height + pad * 2.0f};

        // Quick reject for pure orthogonal segments (our only case).
        const float minX = std::min(a.x, b.x);
        const float maxX = std::max(a.x, b.x);
        const float minY = std::min(a.y, b.y);
        const float maxY = std::max(a.y, b.y);

        if (maxX < inflated.x || minX > inflated.x + inflated.width ||
            maxY < inflated.y || minY > inflated.y + inflated.height)
        {
            return false;
        }

        // Horizontal segment
        if (std::fabs(a.y - b.y) < 0.5f)
        {
            return a.y >= inflated.y && a.y <= inflated.y + inflated.height &&
                maxX >= inflated.x && minX <= inflated.x + inflated.width;
        }

        // Vertical segment
        if (std::fabs(a.x - b.x) < 0.5f)
        {
            return a.x >= inflated.x && a.x <= inflated.x + inflated.width &&
                maxY >= inflated.y && minY <= inflated.y + inflated.height;
        }

        return true;
    }

    bool pathHitsObstacle(
        const std::vector<Vector2>& points,
        const std::vector<Rectangle>& obstacles) const
    {
        if (points.size() < 2)
            return false;

        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            for (size_t o = 0; o < obstacles.size(); ++o)
            {
                if (segmentIntersectsRect(points[i], points[i + 1], obstacles[o], 2.0f))
                    return true;
            }
        }
        return false;
    }

    Vector2 cardPort(Rectangle card, const std::string& side) const
    {
        // Flush with the card edge — no gap between image border and link end.
        if (side == "left")
            return {card.x, card.y + card.height * 0.5f};
        if (side == "right")
            return {card.x + card.width, card.y + card.height * 0.5f};
        if (side == "top")
            return {card.x + card.width * 0.5f, card.y};
        return {card.x + card.width * 0.5f, card.y + card.height};
    }

    Vector2 sideOutwardNormal(const std::string& side) const
    {
        if (side == "left")
            return {-1.0f, 0.0f};
        if (side == "right")
            return {1.0f, 0.0f};
        if (side == "top")
            return {0.0f, -1.0f};
        return {0.0f, 1.0f};
    }

    std::string facingSide(Rectangle from, Rectangle to) const
    {
        const float dx = (to.x + to.width * 0.5f) - (from.x + from.width * 0.5f);
        const float dy = (to.y + to.height * 0.5f) - (from.y + from.height * 0.5f);
        if (std::fabs(dx) >= std::fabs(dy))
            return dx >= 0.0f ? "right" : "left";
        return dy >= 0.0f ? "bottom" : "top";
    }

    std::string oppositeSide(const std::string& side) const
    {
        if (side == "left")
            return "right";
        if (side == "right")
            return "left";
        if (side == "top")
            return "bottom";
        return "top";
    }

    std::string oppositeDirection(const std::string& direction) const
    {
        if (direction == "left")
            return "right";
        if (direction == "right")
            return "left";
        if (direction == "forward")
            return "backward";
        if (direction == "backward")
            return "forward";
        return "";
    }

    bool isOppositeReciprocal(
        const std::string& fromId,
        const std::string& direction,
        const std::string& toId) const
    {
        const std::string reverseDir = oppositeDirection(direction);
        if (reverseDir.empty())
            return false;
        return getExitTarget(toId, reverseDir) == fromId;
    }

    void drawArrowHead(Vector2 tip, Vector2 fromDir) const
    {
        Vector2 direction = Vector2Normalize(fromDir);
        if (Vector2Length(direction) < 0.01f)
            direction = {1.0f, 0.0f};
        const Vector2 base = Vector2Subtract(tip, Vector2Scale(direction, kArrowHeadLength));
        const Vector2 ortho = {-direction.y, direction.x};
        const Vector2 p1 = Vector2Add(base, Vector2Scale(ortho, kArrowHeadHalfWidth));
        const Vector2 p2 = Vector2Subtract(base, Vector2Scale(ortho, kArrowHeadHalfWidth));
        DrawTriangle(p1, tip, p2, kExitArrow);
    }

    // Semicircle at the source edge: flat diameter on the scene border, curve outward.
    void drawSourceEndCap(Vector2 edgePoint, const std::string& fromSide) const
    {
        // raylib angles: 0 = east, clockwise positive.
        float startAngle = 0.0f;
        float endAngle = 180.0f;
        if (fromSide == "right")
        {
            startAngle = -90.0f;
            endAngle = 90.0f;
        }
        else if (fromSide == "left")
        {
            startAngle = 90.0f;
            endAngle = 270.0f;
        }
        else if (fromSide == "top")
        {
            startAngle = 180.0f;
            endAngle = 360.0f;
        }
        else // bottom
        {
            startAngle = 0.0f;
            endAngle = 180.0f;
        }

        DrawCircleSector(edgePoint, kLinkEndCapRadius, startAngle, endAngle, 18, kExitArrow);
        DrawCircleSectorLines(edgePoint, kLinkEndCapRadius, startAngle, endAngle, 18, kPanelBorder);

        // Flat diameter flush with the card edge.
        const Vector2 normal = sideOutwardNormal(fromSide);
        const Vector2 tangent = {-normal.y, normal.x};
        const Vector2 a = Vector2Subtract(edgePoint, Vector2Scale(tangent, kLinkEndCapRadius));
        const Vector2 b = Vector2Add(edgePoint, Vector2Scale(tangent, kLinkEndCapRadius));
        DrawLineEx(a, b, 2.0f, kPanelBorder);
    }

    void drawPolyline(
        const std::vector<Vector2>& points,
        bool arrowAtStart,
        bool arrowAtEnd,
        bool semicircleAtStart,
        const std::string& fromSide) const
    {
        if (points.size() < 2)
            return;

        for (size_t i = 0; i + 1 < points.size(); ++i)
            DrawLineEx(points[i], points[i + 1], 4.0f, Color{8, 7, 12, 220});
        for (size_t i = 0; i + 1 < points.size(); ++i)
            DrawLineEx(points[i], points[i + 1], 2.0f, kExitArrow);

        const Vector2& p0 = points[0];
        const Vector2& p1 = points[1];
        const Vector2& pN1 = points[points.size() - 2];
        const Vector2& pN = points[points.size() - 1];

        if (semicircleAtStart)
            drawSourceEndCap(p0, fromSide);
        else if (arrowAtStart)
            drawArrowHead(p0, Vector2Subtract(p0, p1));

        if (arrowAtEnd)
            drawArrowHead(pN, Vector2Subtract(pN, pN1));
    }

    std::vector<Vector2> buildOrthogonalRoute(
        Rectangle fromCard,
        Rectangle toCard,
        const std::string& exitDir,
        const std::vector<Rectangle>& obstacles) const
    {
        std::string fromSide = "right";
        std::string toSide = "left";
        int dCol = 0;
        int dRow = 0;
        if (directionDelta(exitDir, dCol, dRow))
        {
            if (dCol > 0)
            {
                fromSide = "right";
                toSide = "left";
            }
            else if (dCol < 0)
            {
                fromSide = "left";
                toSide = "right";
            }
            else if (dRow < 0)
            {
                fromSide = "top";
                toSide = "bottom";
            }
            else
            {
                fromSide = "bottom";
                toSide = "top";
            }
        }
        else
        {
            fromSide = facingSide(fromCard, toCard);
            toSide = oppositeSide(fromSide);
        }

        // Endpoints flush with the card borders.
        const Vector2 start = cardPort(fromCard, fromSide);
        const Vector2 end = cardPort(toCard, toSide);

        // Leave / enter each card on the perpendicular to that side, then turn
        // in the corridor between tiles.
        const Vector2 startNormal = sideOutwardNormal(fromSide);
        const Vector2 endNormal = sideOutwardNormal(toSide);
        const Vector2 exitStub = Vector2Add(start, Vector2Scale(startNormal, kLinkStubLength));
        const Vector2 enterStub = Vector2Add(end, Vector2Scale(endNormal, kLinkStubLength));

        std::vector<std::vector<Vector2> > midRoutes;

        if (std::fabs(exitStub.x - enterStub.x) < 1.0f ||
            std::fabs(exitStub.y - enterStub.y) < 1.0f)
        {
            std::vector<Vector2> straight;
            straight.push_back(exitStub);
            straight.push_back(enterStub);
            midRoutes.push_back(straight);
        }

        {
            std::vector<Vector2> path;
            path.push_back(exitStub);
            path.push_back({enterStub.x, exitStub.y});
            path.push_back(enterStub);
            midRoutes.push_back(path);
        }
        {
            std::vector<Vector2> path;
            path.push_back(exitStub);
            path.push_back({exitStub.x, enterStub.y});
            path.push_back(enterStub);
            midRoutes.push_back(path);
        }

        const float above = std::min(fromCard.y, toCard.y) - kLayoutGapY * 0.5f;
        const float below = std::max(fromCard.y + fromCard.height, toCard.y + toCard.height) +
            kLayoutGapY * 0.5f;
        const float left = std::min(fromCard.x, toCard.x) - kLayoutGapX * 0.5f;
        const float right = std::max(fromCard.x + fromCard.width, toCard.x + toCard.width) +
            kLayoutGapX * 0.5f;
        const float midX =
            0.5f * ((fromCard.x + fromCard.width * 0.5f) + (toCard.x + toCard.width * 0.5f));
        const float midY =
            0.5f * ((fromCard.y + fromCard.height * 0.5f) + (toCard.y + toCard.height * 0.5f));

        {
            std::vector<Vector2> path;
            path.push_back(exitStub);
            path.push_back({exitStub.x, above});
            path.push_back({enterStub.x, above});
            path.push_back(enterStub);
            midRoutes.push_back(path);
        }
        {
            std::vector<Vector2> path;
            path.push_back(exitStub);
            path.push_back({exitStub.x, below});
            path.push_back({enterStub.x, below});
            path.push_back(enterStub);
            midRoutes.push_back(path);
        }
        {
            std::vector<Vector2> path;
            path.push_back(exitStub);
            path.push_back({left, exitStub.y});
            path.push_back({left, enterStub.y});
            path.push_back(enterStub);
            midRoutes.push_back(path);
        }
        {
            std::vector<Vector2> path;
            path.push_back(exitStub);
            path.push_back({right, exitStub.y});
            path.push_back({right, enterStub.y});
            path.push_back(enterStub);
            midRoutes.push_back(path);
        }
        {
            std::vector<Vector2> path;
            path.push_back(exitStub);
            path.push_back({midX, exitStub.y});
            path.push_back({midX, enterStub.y});
            path.push_back(enterStub);
            midRoutes.push_back(path);
        }
        {
            std::vector<Vector2> path;
            path.push_back(exitStub);
            path.push_back({exitStub.x, midY});
            path.push_back({enterStub.x, midY});
            path.push_back(enterStub);
            midRoutes.push_back(path);
        }

        std::vector<Vector2> chosenMid;
        bool found = false;
        for (size_t i = 0; i < midRoutes.size(); ++i)
        {
            if (!pathHitsObstacle(midRoutes[i], obstacles))
            {
                chosenMid = midRoutes[i];
                found = true;
                break;
            }
        }
        if (!found)
        {
            chosenMid.push_back(exitStub);
            chosenMid.push_back({enterStub.x, exitStub.y});
            chosenMid.push_back(enterStub);
        }

        // edge (flush) -> perpendicular stub -> corridor -> stub -> edge (flush)
        std::vector<Vector2> full;
        full.push_back(start);
        for (size_t i = 0; i < chosenMid.size(); ++i)
            full.push_back(chosenMid[i]);
        full.push_back(end);
        return full;
    }

    void drawExitArrows(Rectangle canvasBounds)
    {
        if (!scenesDoc.isLoaded())
            return;

        const std::vector<std::string> levelIds = scenesOnLevel(canvasLevel);
        std::vector<Rectangle> allCards;
        allCards.reserve(levelIds.size());
        for (const std::string& id : levelIds)
            allCards.push_back(sceneCardBounds(id, canvasBounds));

        // Obstacles exclude the two endpoints of each link (stubs leave those cards).
        for (size_t i = 0; i < levelIds.size(); ++i)
        {
            const std::string& fromId = levelIds[i];
            const char* dirs[] = {"forward", "backward", "left", "right"};
            for (size_t d = 0; d < 4; ++d)
            {
                const std::string direction = dirs[d];
                const std::string toId = getExitTarget(fromId, direction);
                if (toId.empty() || !isSameLevelLink(fromId, toId))
                    continue;

                const bool reciprocalOpposite = isOppositeReciprocal(fromId, direction, toId);

                // Mutual opposite pair: draw once with arrows on both ends.
                if (reciprocalOpposite && fromId > toId)
                    continue;

                const Rectangle fromCard = sceneCardBounds(fromId, canvasBounds);
                const Rectangle toCard = sceneCardBounds(toId, canvasBounds);

                std::vector<Rectangle> obstacles;
                for (size_t c = 0; c < levelIds.size(); ++c)
                {
                    if (levelIds[c] == fromId || levelIds[c] == toId)
                        continue;
                    obstacles.push_back(allCards[c]);
                }

                const std::vector<Vector2> route =
                    buildOrthogonalRoute(fromCard, toCard, direction, obstacles);

                std::string fromSide = "right";
                int dCol = 0;
                int dRow = 0;
                if (directionDelta(direction, dCol, dRow))
                {
                    if (dCol > 0)
                        fromSide = "right";
                    else if (dCol < 0)
                        fromSide = "left";
                    else if (dRow < 0)
                        fromSide = "top";
                    else
                        fromSide = "bottom";
                }
                else
                {
                    fromSide = facingSide(fromCard, toCard);
                }

                if (reciprocalOpposite)
                {
                    drawPolyline(route, true, true, false, fromSide);
                }
                else
                {
                    // One-way: semicircle at source edge, arrow into destination.
                    drawPolyline(route, false, true, true, fromSide);
                }
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
            const float iconSize = 20.0f;
            const float iconSlot = 16.0f;
            const int iconCount = (hasUp ? 1 : 0) + (hasDown ? 1 : 0);
            const float badgePad = 3.0f;
            const float badgeW = iconCount * iconSlot + badgePad * 2.0f;
            const float badgeH = iconSize + badgePad;
            const Rectangle badge = {
                card.x + card.width - badgeW - 3.0f,
                card.y + 2.0f,
                badgeW,
                badgeH};
            DrawRectangleRec(badge, Color{8, 7, 12, 230});
            DrawRectangleLinesEx(badge, 1.0f, Color{20, 18, 26, 255});

            float iconX = badge.x + badge.width - badgePad - iconSlot;
            if (hasUp)
            {
                DrawTextEx(boldFont(), "^", {iconX, badge.y}, iconSize, 1.0f, kPanelBorder);
                iconX -= iconSlot;
            }
            if (hasDown)
            {
                DrawTextEx(boldFont(), "v", {iconX, badge.y}, iconSize, 1.0f, kPanelBorder);
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

    struct CanvasContentBounds
    {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        bool valid = false;

        float width() const { return maxX - minX; }
        float height() const { return maxY - minY; }
    };

    CanvasContentBounds contentBoundsForLevel(int level) const
    {
        CanvasContentBounds bounds;
        const std::vector<std::string> ids = scenesOnLevel(level);
        for (size_t i = 0; i < ids.size(); ++i)
        {
            const SceneLayout layout = scenesDoc.getLayout(ids[i]);
            const float right = layout.x + kSceneCardWidth;
            const float bottom = layout.y + kSceneCardHeight;
            if (!bounds.valid)
            {
                bounds.minX = layout.x;
                bounds.minY = layout.y;
                bounds.maxX = right;
                bounds.maxY = bottom;
                bounds.valid = true;
            }
            else
            {
                bounds.minX = std::min(bounds.minX, layout.x);
                bounds.minY = std::min(bounds.minY, layout.y);
                bounds.maxX = std::max(bounds.maxX, right);
                bounds.maxY = std::max(bounds.maxY, bottom);
            }
        }

        if (bounds.valid)
        {
            bounds.minX -= kScrollContentPad;
            bounds.minY -= kScrollContentPad;
            bounds.maxX += kScrollContentPad;
            bounds.maxY += kScrollContentPad;
        }

        return bounds;
    }

    void clampCanvasScrollForCanvas(Rectangle canvasBounds, Rectangle contentView, const CanvasContentBounds& content)
    {
        if (!content.valid)
        {
            canvasScroll = {0.0f, 0.0f};
            return;
        }

        // screenPos = canvasBounds + layout + scroll
        // Visible when screenPos is inside contentView.
        // scroll.x max (content pinned left): contentView.x = canvasBounds.x + content.minX + scroll.x
        //   => scroll.x = contentView.x - canvasBounds.x - content.minX
        // scroll.x min (content pinned right):
        //   contentView.x + contentView.width = canvasBounds.x + content.maxX + scroll.x
        //   => scroll.x = contentView.x + contentView.width - canvasBounds.x - content.maxX

        float maxScrollX = contentView.x - canvasBounds.x - content.minX;
        float minScrollX = contentView.x + contentView.width - canvasBounds.x - content.maxX;
        float maxScrollY = contentView.y - canvasBounds.y - content.minY;
        float minScrollY = contentView.y + contentView.height - canvasBounds.y - content.maxY;

        if (content.width() <= contentView.width)
            canvasScroll.x = maxScrollX;
        else
        {
            if (minScrollX > maxScrollX)
                std::swap(minScrollX, maxScrollX);
            if (canvasScroll.x < minScrollX)
                canvasScroll.x = minScrollX;
            if (canvasScroll.x > maxScrollX)
                canvasScroll.x = maxScrollX;
        }

        if (content.height() <= contentView.height)
            canvasScroll.y = maxScrollY;
        else
        {
            if (minScrollY > maxScrollY)
                std::swap(minScrollY, maxScrollY);
            if (canvasScroll.y < minScrollY)
                canvasScroll.y = minScrollY;
            if (canvasScroll.y > maxScrollY)
                canvasScroll.y = maxScrollY;
        }
    }

    void drawCanvasScrollBars(
        Rectangle canvasBounds,
        Rectangle contentView,
        const CanvasContentBounds& content,
        bool showH,
        bool showV)
    {
        const Vector2 mouse = GetMousePosition();

        if (showH)
        {
            const Rectangle track = {
                contentView.x,
                canvasBounds.y + canvasBounds.height - kScrollBarSize,
                contentView.width,
                kScrollBarSize};
            DrawRectangleRec(track, kScrollTrack);
            DrawRectangleLinesEx(track, 1.0f, kPanelInnerEdge);

            const float contentW = std::max(content.width(), 1.0f);
            const float thumbW = std::max(24.0f, track.width * (contentView.width / contentW));
            const float maxScrollX = contentView.x - canvasBounds.x - content.minX;
            const float minScrollX = contentView.x + contentView.width - canvasBounds.x - content.maxX;
            const float scrollRange = std::max(0.001f, maxScrollX - minScrollX);
            const float t = (maxScrollX - canvasScroll.x) / scrollRange;
            const float thumbX = track.x + t * (track.width - thumbW);
            const Rectangle thumb = {thumbX, track.y + 2.0f, thumbW, track.height - 4.0f};
            DrawRectangleRec(thumb, draggingHScroll ? kScrollThumbActive : kScrollThumb);

            if (!stackDialogOpen && !isDraggingDivider())
            {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, thumb))
                {
                    draggingHScroll = true;
                    hScrollGrabOffset = mouse.x - thumb.x;
                    dragSource = DragSource::None;
                    dragSceneId.clear();
                }
                else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, track))
                {
                    const float center = mouse.x - thumbW * 0.5f;
                    const float ratio = (center - track.x) / std::max(1.0f, track.width - thumbW);
                    canvasScroll.x = maxScrollX - ratio * scrollRange;
                    draggingHScroll = true;
                    hScrollGrabOffset = thumbW * 0.5f;
                }
            }

            if (draggingHScroll && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            {
                const float thumbPos = mouse.x - hScrollGrabOffset;
                const float ratio = (thumbPos - track.x) / std::max(1.0f, track.width - thumbW);
                const float clampedRatio = std::max(0.0f, std::min(1.0f, ratio));
                canvasScroll.x = maxScrollX - clampedRatio * scrollRange;
            }
        }

        if (showV)
        {
            const Rectangle track = {
                canvasBounds.x + canvasBounds.width - kScrollBarSize,
                contentView.y,
                kScrollBarSize,
                contentView.height};
            DrawRectangleRec(track, kScrollTrack);
            DrawRectangleLinesEx(track, 1.0f, kPanelInnerEdge);

            const float contentH = std::max(content.height(), 1.0f);
            const float thumbH = std::max(24.0f, track.height * (contentView.height / contentH));
            const float maxScrollY = contentView.y - canvasBounds.y - content.minY;
            const float minScrollY = contentView.y + contentView.height - canvasBounds.y - content.maxY;
            const float scrollRange = std::max(0.001f, maxScrollY - minScrollY);
            const float t = (maxScrollY - canvasScroll.y) / scrollRange;
            const float thumbY = track.y + t * (track.height - thumbH);
            const Rectangle thumb = {track.x + 2.0f, thumbY, track.width - 4.0f, thumbH};
            DrawRectangleRec(thumb, draggingVScroll ? kScrollThumbActive : kScrollThumb);

            if (!stackDialogOpen && !isDraggingDivider())
            {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, thumb))
                {
                    draggingVScroll = true;
                    vScrollGrabOffset = mouse.y - thumb.y;
                    dragSource = DragSource::None;
                    dragSceneId.clear();
                }
                else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, track))
                {
                    const float center = mouse.y - thumbH * 0.5f;
                    const float ratio = (center - track.y) / std::max(1.0f, track.height - thumbH);
                    canvasScroll.y = maxScrollY - ratio * scrollRange;
                    draggingVScroll = true;
                    vScrollGrabOffset = thumbH * 0.5f;
                }
            }

            if (draggingVScroll && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            {
                const float thumbPos = mouse.y - vScrollGrabOffset;
                const float ratio = (thumbPos - track.y) / std::max(1.0f, track.height - thumbH);
                const float clampedRatio = std::max(0.0f, std::min(1.0f, ratio));
                canvasScroll.y = maxScrollY - clampedRatio * scrollRange;
            }
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            draggingHScroll = false;
            draggingVScroll = false;
        }

        // Corner filler where bars meet.
        if (showH && showV)
        {
            DrawRectangleRec(
                {canvasBounds.x + canvasBounds.width - kScrollBarSize,
                 canvasBounds.y + canvasBounds.height - kScrollBarSize,
                 kScrollBarSize,
                 kScrollBarSize},
                kScrollTrack);
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

        const CanvasContentBounds content = contentBoundsForLevel(canvasLevel);
        const float fullViewW = canvasBounds.width;
        const float fullViewH = canvasBounds.height - kCanvasChromeHeight;
        bool showV = content.valid && content.height() > fullViewH + 0.5f;
        bool showH = content.valid && content.width() > (fullViewW - (showV ? kScrollBarSize : 0.0f)) + 0.5f;
        // Re-evaluate vertical once horizontal bar may steal height.
        showV = content.valid &&
            content.height() > (fullViewH - (showH ? kScrollBarSize : 0.0f)) + 0.5f;
        showH = content.valid &&
            content.width() > (fullViewW - (showV ? kScrollBarSize : 0.0f)) + 0.5f;

        const Rectangle contentView = {
            canvasBounds.x,
            canvasBounds.y + kCanvasChromeHeight,
            canvasBounds.width - (showV ? kScrollBarSize : 0.0f),
            canvasBounds.height - kCanvasChromeHeight - (showH ? kScrollBarSize : 0.0f)};

        clampCanvasScrollForCanvas(canvasBounds, contentView, content);

        BeginScissorMode(
            static_cast<int>(contentView.x),
            static_cast<int>(contentView.y),
            static_cast<int>(contentView.width),
            static_cast<int>(contentView.height));

        // Draw cards first, then links on top so arrows are never half-hidden
        // under thumbnails.
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

            if (!stackDialogOpen &&
                !variableEditorOpen &&
                !isDraggingDivider() &&
                !draggingHScroll &&
                !draggingVScroll)
            {
                const bool hovered = CheckCollisionPointRec(GetMousePosition(), card);
                if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                {
                    if (selectedSceneId != id)
                    {
                        selectedSceneId = id;
                        selectedVariableKey.clear();
                        variablesScroll = 0.0f;
                    }
                    dragSource = DragSource::Canvas;
                    dragSceneId = id;
                    dragOffset = {GetMouseX() - card.x, GetMouseY() - card.y};
                }
            }
        }

        drawExitArrows(canvasBounds);
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
            if (CheckCollisionPointRec(GetMousePosition(), contentView) &&
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

        drawCanvasScrollBars(canvasBounds, contentView, content, showH, showV);
        clampCanvasScrollForCanvas(canvasBounds, contentView, content);

        if (!stackDialogOpen &&
            !draggingHScroll &&
            !draggingVScroll &&
            CheckCollisionPointRec(GetMousePosition(), contentView))
        {
            const float wheel = GetMouseWheelMove() * 32.0f;
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
            {
                canvasScroll.x += wheel;
            }
            else
            {
                canvasScroll.y -= wheel;
            }
            clampCanvasScrollForCanvas(canvasBounds, contentView, content);
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

    void closeVariableEditor()
    {
        variableEditorOpen = false;
        variableEditorSceneId.clear();
        variableEditorKey.clear();
        variableEditorBuffer.clear();
        variableEditorCursor = 0;
        variableEditorScrollY = 0.0f;
        variableEditorError.clear();
    }

    void openVariableEditor(const std::string& sceneId, const std::string& key)
    {
        const nlohmann::json* scene = scenesDoc.sceneJson(sceneId);
        if (scene == nullptr || !scene->contains(key))
        {
            TraceLog(LOG_WARNING, "SCENE EDITOR: cannot edit missing key %s", key.c_str());
            return;
        }

        const nlohmann::json& value = (*scene)[key];
        variableEditorSceneId = sceneId;
        variableEditorKey = key;
        variableEditorScrollY = 0.0f;
        variableEditorError.clear();
        selectedVariableKey = key;
        TraceLog(LOG_INFO, "SCENE EDITOR: editing %s.%s", sceneId.c_str(), key.c_str());

        // Copy by value immediately so we never hold a dangling json reference.
        if (value.is_string())
        {
            variableEditorKind = VariableKindString;
            variableEditorBuffer = value.get<std::string>();
        }
        else if (value.is_boolean())
        {
            variableEditorKind = VariableKindBool;
            variableEditorBuffer = value.get<bool>() ? "true" : "false";
        }
        else if (value.is_number_integer())
        {
            variableEditorKind = VariableKindInteger;
            variableEditorBuffer = std::to_string(value.get<long long>());
        }
        else if (value.is_number_float())
        {
            variableEditorKind = VariableKindFloat;
            std::ostringstream stream;
            stream << value.get<double>();
            variableEditorBuffer = stream.str();
        }
        else if (value.is_null())
        {
            variableEditorKind = VariableKindString;
            variableEditorBuffer.clear();
        }
        else
        {
            variableEditorKind = VariableKindJson;
            variableEditorBuffer = value.dump(2);
        }

        variableEditorMultiline =
            variableEditorKind == VariableKindJson ||
            variableEditorBuffer.find('\n') != std::string::npos ||
            variableEditorBuffer.size() > 80;
        variableEditorCursor = static_cast<int>(variableEditorBuffer.size());
        variableEditorOpen = true;
        // Ignore mouse/key "press" still held from the click that opened us.
        variableEditorIgnoreInputFrames = 3;
    }

    bool saveVariableEditor()
    {
        nlohmann::json* scene = scenesDoc.sceneJson(variableEditorSceneId);
        if (scene == nullptr)
            return false;

        nlohmann::json& value = (*scene)[variableEditorKey];
        try
        {
            if (variableEditorKind == VariableKindBool)
            {
                std::string lowered = variableEditorBuffer;
                for (size_t i = 0; i < lowered.size(); ++i)
                    lowered[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[i])));
                if (lowered == "true" || lowered == "1" || lowered == "yes")
                    value = true;
                else if (lowered == "false" || lowered == "0" || lowered == "no")
                    value = false;
                else
                    return false;
            }
            else if (variableEditorKind == VariableKindInteger)
            {
                value = std::stoll(variableEditorBuffer);
            }
            else if (variableEditorKind == VariableKindFloat)
            {
                value = std::stod(variableEditorBuffer);
            }
            else if (variableEditorKind == VariableKindJson)
            {
                value = nlohmann::json::parse(variableEditorBuffer);
            }
            else
            {
                value = variableEditorBuffer;
            }
        }
        catch (...)
        {
            return false;
        }

        markDirty();
        // Persist immediately so Save in the popup has an obvious effect.
        if (!scenesDoc.save())
        {
            variableEditorError = "Applied in memory, but failed to write scenes.json";
            return false;
        }
        dirty = false;
        closeVariableEditor();
        return true;
    }

    void handleVariableEditorTextInput()
    {
        if (!variableEditorOpen)
            return;

        if (variableEditorIgnoreInputFrames > 0)
        {
            // Drain any char events from the activating click/key.
            while (GetCharPressed() > 0)
            {
            }
            return;
        }

        // Codepoint input
        int codepoint = GetCharPressed();
        while (codepoint > 0)
        {
            if (codepoint >= 32 && codepoint != 127)
            {
                // Encode UTF-8 roughly for BMP-ish chars used in game text
                std::string encoded;
                if (codepoint < 0x80)
                {
                    encoded.push_back(static_cast<char>(codepoint));
                }
                else if (codepoint < 0x800)
                {
                    encoded.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                    encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                }
                else
                {
                    encoded.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                    encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                    encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                }

                if (variableEditorCursor < 0)
                    variableEditorCursor = 0;
                if (variableEditorCursor > static_cast<int>(variableEditorBuffer.size()))
                    variableEditorCursor = static_cast<int>(variableEditorBuffer.size());
                variableEditorBuffer.insert(
                    static_cast<size_t>(variableEditorCursor),
                    encoded);
                variableEditorCursor += static_cast<int>(encoded.size());
            }
            codepoint = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        {
            if (variableEditorCursor > 0 && !variableEditorBuffer.empty())
            {
                // Delete one UTF-8 leading byte / simple previous char
                int eraseAt = variableEditorCursor - 1;
                while (eraseAt > 0 &&
                       (static_cast<unsigned char>(variableEditorBuffer[static_cast<size_t>(eraseAt)]) & 0xC0) == 0x80)
                {
                    --eraseAt;
                }
                variableEditorBuffer.erase(
                    static_cast<size_t>(eraseAt),
                    static_cast<size_t>(variableEditorCursor - eraseAt));
                variableEditorCursor = eraseAt;
            }
        }

        if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE))
        {
            if (variableEditorCursor < static_cast<int>(variableEditorBuffer.size()))
            {
                int eraseEnd = variableEditorCursor + 1;
                while (eraseEnd < static_cast<int>(variableEditorBuffer.size()) &&
                       (static_cast<unsigned char>(variableEditorBuffer[static_cast<size_t>(eraseEnd)]) & 0xC0) == 0x80)
                {
                    ++eraseEnd;
                }
                variableEditorBuffer.erase(
                    static_cast<size_t>(variableEditorCursor),
                    static_cast<size_t>(eraseEnd - variableEditorCursor));
            }
        }

        if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT))
        {
            if (variableEditorCursor > 0)
            {
                --variableEditorCursor;
                while (variableEditorCursor > 0 &&
                       (static_cast<unsigned char>(
                            variableEditorBuffer[static_cast<size_t>(variableEditorCursor)]) &
                        0xC0) == 0x80)
                {
                    --variableEditorCursor;
                }
            }
        }

        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT))
        {
            if (variableEditorCursor < static_cast<int>(variableEditorBuffer.size()))
            {
                ++variableEditorCursor;
                while (variableEditorCursor < static_cast<int>(variableEditorBuffer.size()) &&
                       (static_cast<unsigned char>(
                            variableEditorBuffer[static_cast<size_t>(variableEditorCursor)]) &
                        0xC0) == 0x80)
                {
                    ++variableEditorCursor;
                }
            }
        }

        if (IsKeyPressed(KEY_HOME))
            variableEditorCursor = 0;
        if (IsKeyPressed(KEY_END))
            variableEditorCursor = static_cast<int>(variableEditorBuffer.size());

        if (variableEditorMultiline &&
            (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)))
        {
            if (variableEditorCursor < 0)
                variableEditorCursor = 0;
            if (variableEditorCursor > static_cast<int>(variableEditorBuffer.size()))
                variableEditorCursor = static_cast<int>(variableEditorBuffer.size());
            variableEditorBuffer.insert(static_cast<size_t>(variableEditorCursor), "\n");
            ++variableEditorCursor;
        }

        if (IsKeyPressed(KEY_ESCAPE))
            closeVariableEditor();
    }

    void drawVariableEditor(int screenWidth, int screenHeight)
    {
        if (!variableEditorOpen)
            return;

        if (variableEditorIgnoreInputFrames > 0)
            --variableEditorIgnoreInputFrames;

        handleVariableEditorTextInput();

        DrawRectangle(0, 0, screenWidth, screenHeight, Color{0, 0, 0, 200});

        const float dialogW = variableEditorMultiline ? 760.0f : 520.0f;
        const float dialogH = variableEditorMultiline ? 520.0f : 190.0f;
        const Rectangle dialog = {
            (static_cast<float>(screenWidth) - dialogW) * 0.5f,
            (static_cast<float>(screenHeight) - dialogH) * 0.5f,
            dialogW,
            dialogH};
        DrawRectangleRounded(dialog, 0.03f, 8, kModalFill);
        DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

        const std::string title =
            "Edit \"" + variableEditorKey + "\"  —  scene: " + variableEditorSceneId;
        DrawTextEx(
            textFont(),
            title.c_str(),
            {dialog.x + 18.0f, dialog.y + 14.0f},
            16.0f,
            1.0f,
            kTextPrimary);

        const float btnH = 34.0f;
        const float btnW = 110.0f;
        const float btnY = dialog.y + dialogH - btnH - 16.0f;
        const Rectangle field = {
            dialog.x + 18.0f,
            dialog.y + 44.0f,
            dialogW - 36.0f,
            btnY - (dialog.y + 44.0f) - 14.0f};

        DrawRectangleRec(field, Color{18, 16, 24, 255});
        DrawRectangleLinesEx(field, 1.0f, kPanelBorder);

        const float fontSize = 14.0f;
        const float lineHeight = fontSize + 4.0f;
        const float pad = 8.0f;

        // Cursor blink
        const bool caretOn = (static_cast<int>(GetTime() * 2.0) % 2) == 0;
        const int caret = std::max(0, std::min(variableEditorCursor, static_cast<int>(variableEditorBuffer.size())));

        if (!variableEditorMultiline)
        {
            const std::string display = variableEditorBuffer;
            DrawTextEx(
                textFont(),
                display.c_str(),
                {field.x + pad, field.y + (field.height - fontSize) * 0.5f},
                fontSize,
                1.0f,
                kTextPrimary);

            if (caretOn)
            {
                const std::string before = display.substr(0, static_cast<size_t>(caret));
                const float caretX = field.x + pad + MeasureTextEx(textFont(), before.c_str(), fontSize, 1.0f).x;
                const float caretY = field.y + (field.height - fontSize) * 0.5f;
                DrawLineEx({caretX, caretY}, {caretX, caretY + fontSize}, 1.5f, kTextPrimary);
            }
        }
        else
        {
            // Split into lines for wrapped display (character wrap by measuring).
            std::vector<std::string> lines;
            std::string currentLine;
            const float maxTextW = field.width - pad * 2.0f;
            for (size_t i = 0; i < variableEditorBuffer.size(); ++i)
            {
                const char ch = variableEditorBuffer[i];
                if (ch == '\n')
                {
                    lines.push_back(currentLine);
                    currentLine.clear();
                    continue;
                }
                const std::string trial = currentLine + ch;
                if (MeasureTextEx(textFont(), trial.c_str(), fontSize, 1.0f).x > maxTextW && !currentLine.empty())
                {
                    lines.push_back(currentLine);
                    currentLine = std::string(1, ch);
                }
                else
                {
                    currentLine.push_back(ch);
                }
            }
            lines.push_back(currentLine);

            const float contentH = static_cast<float>(lines.size()) * lineHeight;
            const float maxScroll = std::max(0.0f, contentH - (field.height - pad * 2.0f));
            if (CheckCollisionPointRec(GetMousePosition(), field))
                variableEditorScrollY -= GetMouseWheelMove() * lineHeight;
            if (variableEditorScrollY < 0.0f)
                variableEditorScrollY = 0.0f;
            if (variableEditorScrollY > maxScroll)
                variableEditorScrollY = maxScroll;

            BeginScissorMode(
                static_cast<int>(field.x),
                static_cast<int>(field.y),
                static_cast<int>(field.width),
                static_cast<int>(field.height));

            float y = field.y + pad - variableEditorScrollY;
            for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
            {
                DrawTextEx(
                    textFont(),
                    lines[lineIndex].c_str(),
                    {field.x + pad, y},
                    fontSize,
                    1.0f,
                    kTextPrimary);
                y += lineHeight;
            }

            // Approximate caret at end of buffer for multiline (stable and readable).
            if (caretOn)
            {
                const float caretY =
                    field.y + pad - variableEditorScrollY +
                    static_cast<float>(lines.empty() ? 0 : lines.size() - 1) * lineHeight;
                const std::string lastLine = lines.empty() ? "" : lines.back();
                const float caretX =
                    field.x + pad + MeasureTextEx(textFont(), lastLine.c_str(), fontSize, 1.0f).x;
                DrawLineEx({caretX, caretY}, {caretX, caretY + fontSize}, 1.5f, kTextPrimary);
            }

            EndScissorMode();
        }

        const Rectangle saveBtn = {dialog.x + dialogW - btnW * 2.0f - 28.0f, btnY, btnW, btnH};
        const Rectangle cancelBtn = {dialog.x + dialogW - btnW - 18.0f, btnY, btnW, btnH};

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

        drawButton(saveBtn, "Save", true);
        drawButton(cancelBtn, "Cancel", false);

        if (!variableEditorError.empty())
        {
            DrawTextEx(
                textFont(),
                variableEditorError.c_str(),
                {dialog.x + 18.0f, btnY - 22.0f},
                12.0f,
                1.0f,
                Color{220, 120, 100, 255});
        }

        if (variableEditorIgnoreInputFrames <= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            const Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, saveBtn))
            {
                if (!saveVariableEditor())
                {
                    if (variableEditorError.empty())
                        variableEditorError = "Could not parse value — check type and try again";
                }
            }
            else if (CheckCollisionPointRec(mouse, cancelBtn))
            {
                closeVariableEditor();
            }
        }

        // Enter saves single-line fields; multiline uses Enter for newlines.
        if (variableEditorIgnoreInputFrames <= 0 &&
            !variableEditorMultiline &&
            (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)))
        {
            if (!saveVariableEditor() && variableEditorError.empty())
                variableEditorError = "Could not parse value — check type and try again";
        }
    }

    void drawVariablesPane(Rectangle paneBounds)
    {
        // Capture once so list + editor always use the same scene for this frame.
        const std::string sceneId = selectedSceneId;

        DrawTextEx(textFont(), "Scene Variables", {paneBounds.x + 12.0f, paneBounds.y + 8.0f},
                   15.0f, 1.0f, kTextMuted);
        if (!sceneId.empty())
        {
            DrawTextEx(
                textFont(),
                sceneId.c_str(),
                {paneBounds.x + 150.0f, paneBounds.y + 10.0f},
                12.0f,
                1.0f,
                kPanelBorder);
        }
        DrawTextEx(
            textFont(),
            "Click a row to edit",
            {paneBounds.x + 12.0f, paneBounds.y + paneBounds.height - 18.0f},
            11.0f,
            1.0f,
            kTextMuted);

        const Rectangle editBtn = {
            paneBounds.x + paneBounds.width - 72.0f,
            paneBounds.y + 6.0f,
            60.0f,
            20.0f};
        DrawRectangleRec(editBtn, kPanelAccent);
        DrawRectangleLinesEx(editBtn, 1.0f, kPanelBorder);
        DrawTextEx(textFont(), "Edit", {editBtn.x + 16.0f, editBtn.y + 3.0f}, 12.0f, 1.0f, kTextPrimary);

        if (sceneId.empty() || !scenesDoc.hasScene(sceneId))
        {
            DrawTextEx(textFont(), "Select a scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
                       14.0f, 1.0f, kTextMuted);
            return;
        }

        const std::vector<std::pair<std::string, std::string>> rows =
            scenesDoc.sceneVariableRows(sceneId);
        if (rows.empty())
        {
            DrawTextEx(textFont(), "No variables on this scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
                       14.0f, 1.0f, kTextMuted);
            return;
        }

        if (selectedVariableKey.empty() ||
            std::find_if(rows.begin(), rows.end(), [&](const std::pair<std::string, std::string>& row)
                         { return row.first == selectedVariableKey; }) == rows.end())
        {
            selectedVariableKey = rows.front().first;
        }

        const float rowHeight = 24.0f;
        const float listTop = paneBounds.y + 28.0f;
        const float listHeight = paneBounds.height - 36.0f;
        const float contentHeight = static_cast<float>(rows.size()) * rowHeight + 8.0f;
        const float maxScroll = std::max(0.0f, contentHeight - listHeight);
        if (variablesScroll > maxScroll)
            variablesScroll = maxScroll;

        const Rectangle listBounds = {paneBounds.x, listTop, paneBounds.width, listHeight};
        const Vector2 mouse = GetMousePosition();
        const bool canInteract = !variableEditorOpen && !stackDialogOpen;

        if (canInteract && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (CheckCollisionPointRec(mouse, editBtn) && !selectedVariableKey.empty())
            {
                openVariableEditor(sceneId, selectedVariableKey);
            }
            else if (CheckCollisionPointRec(mouse, listBounds))
            {
                const float localY = (mouse.y - listTop - 8.0f) + variablesScroll;
                if (localY >= 0.0f)
                {
                    const int index = static_cast<int>(localY / rowHeight);
                    if (index >= 0 && index < static_cast<int>(rows.size()))
                    {
                        selectedVariableKey = rows[static_cast<size_t>(index)].first;
                        openVariableEditor(sceneId, selectedVariableKey);
                    }
                }
            }
        }

        if (canInteract &&
            !selectedVariableKey.empty() &&
            (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_F2)))
        {
            openVariableEditor(sceneId, selectedVariableKey);
        }

        BeginScissorMode(
            static_cast<int>(listBounds.x),
            static_cast<int>(listBounds.y),
            static_cast<int>(listBounds.width),
            static_cast<int>(listBounds.height));

        float y = listTop + 8.0f - variablesScroll;
        for (const std::pair<std::string, std::string>& row : rows)
        {
            const Rectangle rowBounds = {
                paneBounds.x + 8.0f,
                y,
                paneBounds.width - 16.0f,
                rowHeight - 2.0f};
            const bool hovered =
                canInteract &&
                CheckCollisionPointRec(mouse, listBounds) &&
                CheckCollisionPointRec(mouse, rowBounds);
            const bool selected = row.first == selectedVariableKey;

            if (selected)
                DrawRectangleRec(rowBounds, kSelection);
            else if (hovered)
                DrawRectangleRec(rowBounds, Color{60, 54, 72, 180});

            const std::string line = row.first + ": " + truncate(row.second, 80);
            DrawTextEx(textFont(), line.c_str(), {rowBounds.x + 4.0f, rowBounds.y + 4.0f},
                       13.0f, 1.0f, kTextPrimary);

            y += rowHeight;
        }

        EndScissorMode();

        if (canInteract && CheckCollisionPointRec(mouse, listBounds))
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

        DrawLineEx(
            {splitX, bottomBounds.y + 12.0f},
            {splitX, bottomBounds.y + bottomBounds.height - 12.0f},
            1.5f,
            kDividerGrip);

        drawVariablesPane(variablesBounds);
        drawActorsPane(actorsBounds);
    }

    void handleDividerInput(int screenWidth, int screenHeight)
    {
        const Rectangle vDiv = verticalDividerBounds(screenWidth);
        const Rectangle hDiv = horizontalDividerBounds(screenWidth);
        const Rectangle vHit = expandHitRect(vDiv, kDividerHitPadding, true);
        const Rectangle hHit = expandHitRect(hDiv, kDividerHitPadding, false);
        const Vector2 mouse = GetMousePosition();

        const bool overVertical = CheckCollisionPointRec(mouse, vHit);
        const bool overHorizontal = CheckCollisionPointRec(mouse, hHit);

        if (draggingVerticalDivider || overVertical)
            SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
        else if (draggingHorizontalDivider || overHorizontal)
            SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
        else
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            // Prefer the split under the cursor; vertical first if both overlap.
            if (overVertical)
                draggingVerticalDivider = true;
            else if (overHorizontal)
                draggingHorizontalDivider = true;
        }

        if (draggingVerticalDivider && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            leftPaneWidth = mouse.x - vDiv.width * 0.5f;
            userResizedLeftSplit = true;
        }

        if (draggingHorizontalDivider && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            topAreaHeight = mouse.y - hDiv.height * 0.5f;
            userResizedTopSplit = true;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            draggingVerticalDivider = false;
            draggingHorizontalDivider = false;
        }

        clampLayout(screenWidth, screenHeight);
    }

    void drawDividers(int screenWidth, int screenHeight) const
    {
        drawDivider(verticalDividerBounds(screenWidth), draggingVerticalDivider, true);
        drawDivider(horizontalDividerBounds(screenWidth), draggingHorizontalDivider, false);
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
        if (variableEditorOpen || stackDialogOpen)
            return;

        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        {
            if (IsKeyPressed(KEY_S))
                saveDocument();
        }
    }

    void update()
    {
        const int screenWidth = GetScreenWidth();
        const int screenHeight = GetScreenHeight();
        syncLayoutToWindow(screenWidth, screenHeight);

        handleShortcuts();

        // Always clear divider drag when the mouse is up (avoids stuck drag blocking selection).
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            draggingVerticalDivider = false;
            draggingHorizontalDivider = false;
        }

        if (!stackDialogOpen && !variableEditorOpen)
            handleDividerInput(screenWidth, screenHeight);
        else
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        // Don't start scene drags while resizing panes or editing variables.
        if (isDraggingDivider() || variableEditorOpen)
        {
            dragSource = DragSource::None;
            dragSceneId.clear();
        }
    }

    void draw()
    {
        const int screenWidth = GetScreenWidth();
        const int screenHeight = GetScreenHeight();
        syncLayoutToWindow(screenWidth, screenHeight);

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
        drawDividers(screenWidth, screenHeight);
        drawStatusBar(screenWidth, screenHeight);
        drawStackDialog(screenWidth, screenHeight);
        drawVariableEditor(screenWidth, screenHeight);

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
    app.initLayout(GetScreenWidth(), GetScreenHeight());
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