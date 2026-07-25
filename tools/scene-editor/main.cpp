#include "ImageCompression.h"
#include "PlatformPath.h"
#include "RaylibCompat.h"
#include "SceneDocument.h"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
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
const float kTabHeight = 34.0f;
const float kSceneCardWidth = 160.0f;
const float kSceneCardMinHeight = 110.0f;
const float kSceneCardThumbHeight = 68.0f;
const float kSceneCardTitleFont = 14.0f; // was 12
const float kSceneCardTitleLineHeight = 17.0f;
const int kSceneCardMaxTitleLines = 4;
// Corridors between cards for mid-route turns (endpoints stay flush with card edges).
const float kLayoutGapX = 96.0f;
const float kLayoutGapY = 96.0f;
// UI body fonts are ~2pt larger than the original defaults.
const float kFontTiny = 14.0f;
const float kFontSmall = 15.0f;
const float kFontBody = 16.0f;
const float kFontLabel = 17.0f;
const float kFontTitle = 18.0f;
const float kFontHeading = 20.0f;
// How far into the corridor the perpendicular exit/enter stubs travel before turning.
const float kLinkStubLength = 28.0f;
const float kArrowHeadLength = 12.0f;
const float kArrowHeadHalfWidth = 5.0f;
const float kLinkEndCapRadius = 7.0f;
const float kWireHopRadius = 8.0f; // schematic-style jump at wire crossings
const float kLayoutOriginX = 40.0f;
const float kLayoutOriginY = 48.0f;
const float kListThumbSize = 96.0f; // doubled for the left scene browser
const float kListRowHeight = 108.0f;
const float kListNameFont = 18.0f;   // +2 from prior list body size
const float kListMetaFont = 16.0f;
const float kListTabFont = 16.0f;
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

enum class ConversationNodeKind
{
    Section,   // virtual group (e.g. Main Character, Actors)
    Actor,
    Milestone,
    Dialog,
    Narrative  // scene description / examine / speak text for the PC
};

enum class ConversationEditDoc
{
    None,
    Conversations,
    Scenes
};

struct ConversationTreeNode
{
    ConversationNodeKind kind = ConversationNodeKind::Actor;
    std::string key;          // stable expand/select id
    std::string label;
    std::string detail;       // secondary text (scene, type, etc.)
    ConversationEditDoc editDoc = ConversationEditDoc::None;
    std::string jsonPointer;  // conversations root pointer, or path under a scene object
    std::string editSceneId;  // when editDoc == Scenes
    std::vector<ConversationTreeNode> children;
};

struct ConversationVisibleRow
{
    const ConversationTreeNode* node = nullptr;
    int depth = 0;
    bool isLastChild = false;
    std::vector<bool> ancestorContinues; // true = draw vertical line past this depth
};

struct EditorVisualLine
{
    int start = 0; // buffer index of first char on this visual line
    int end = 0;   // buffer index one past last drawn char (may point at '\n')
    std::string text;
};

struct ThumbnailEntry
{
    Texture2D texture{};
    bool loaded = false;
    bool missing = false;
};

const float kTreeRowHeight = 24.0f;
const float kTreeIndent = 18.0f;
const float kTreeToggleSize = 14.0f;
const float kTreeTogglePad = 4.0f;

std::string conversationJsonEscape(const std::string& key)
{
    std::string out;
    out.reserve(key.size());
    for (char ch : key)
    {
        if (ch == '~')
            out += "~0";
        else if (ch == '/')
            out += "~1";
        else
            out += ch;
    }
    return out;
}

std::string conversationPointerJoin(const std::string& parent, const std::string& token)
{
    return parent + "/" + conversationJsonEscape(token);
}

std::string conversationPointerIndex(const std::string& parent, size_t index)
{
    return parent + "/" + std::to_string(index);
}

std::string phaseActorId(const nlohmann::json& phase)
{
    if (phase.contains("actorId") && phase["actorId"].is_string())
        return phase["actorId"].get<std::string>();

    if (phase.contains("actor"))
    {
        const nlohmann::json& actor = phase["actor"];
        if (actor.is_string())
            return actor.get<std::string>();
        if (actor.is_object() && actor.contains("id") && actor["id"].is_string())
            return actor["id"].get<std::string>();
    }

    if (phase.contains("id") && phase["id"].is_string())
        return phase["id"].get<std::string>();

    return "(unknown)";
}

std::string phaseActorName(const nlohmann::json& phase, const std::string& actorId)
{
    if (phase.contains("actorName") && phase["actorName"].is_string())
    {
        const std::string name = phase["actorName"].get<std::string>();
        if (!name.empty())
            return name;
    }

    if (phase.contains("actor") && phase["actor"].is_object() &&
        phase["actor"].contains("name") && phase["actor"]["name"].is_string())
    {
        const std::string name = phase["actor"]["name"].get<std::string>();
        if (!name.empty())
            return name;
    }

    return actorId;
}

std::string choiceTreeLabel(const nlohmann::json& choice)
{
    if (choice.contains("label") && choice["label"].is_string())
    {
        const std::string label = choice["label"].get<std::string>();
        if (!label.empty())
            return label;
    }
    if (choice.contains("id") && choice["id"].is_string())
    {
        const std::string id = choice["id"].get<std::string>();
        if (!id.empty())
            return id;
    }
    if (choice.contains("text") && choice["text"].is_string())
    {
        const std::string text = choice["text"].get<std::string>();
        if (!text.empty())
            return text;
    }
    return "(dialog)";
}

ConversationTreeNode buildChoiceTreeNode(const nlohmann::json& choice, const std::string& pointer)
{
    ConversationTreeNode node;
    node.kind = ConversationNodeKind::Dialog;
    node.key = "choice:" + pointer;
    node.editDoc = ConversationEditDoc::Conversations;
    node.jsonPointer = pointer;
    node.label = choiceTreeLabel(choice);

    if (choice.contains("id") && choice["id"].is_string())
        node.detail = choice["id"].get<std::string>();

    if (choice.contains("choices") && choice["choices"].is_array())
    {
        const nlohmann::json& nested = choice["choices"];
        for (size_t i = 0; i < nested.size(); ++i)
        {
            if (!nested[i].is_object())
                continue;
            node.children.push_back(
                buildChoiceTreeNode(nested[i], conversationPointerIndex(pointer + "/choices", i)));
        }
    }

    return node;
}

struct SceneEditorApp
{
    std::string resourceDir = "../../../resources";
    std::string assetRoot = "../../..";
    std::string loadError;
    SceneDocument scenesDoc;
    nlohmann::json conversationsRoot = nlohmann::json::object();
    std::string conversationsPath;
    bool conversationsLoaded = false;
    std::vector<ConversationTreeNode> conversationTree;
    std::set<std::string> conversationExpanded;
    std::string selectedConversationKey;
    // Cached visible tree rows (invalidated on expand/rebuild).
    mutable std::vector<ConversationVisibleRow> conversationVisibleRowsCache;
    mutable bool conversationVisibleRowsDirty = true;
    // Last left-pane list bounds for input in update() (before draw).
    Rectangle conversationListBounds{0, 0, 0, 0};
    bool conversationListBoundsValid = false;
    Font uiFont{};
    Font uiFontBold{};

    // Text measurement cache — MeasureTextEx is expensive in hot loops.
    mutable float measureCacheFontSize = -1.0f;
    mutable float measureCacheAscii[128]{};
    mutable bool measureCacheAsciiReady = false;
    mutable std::map<std::string, float> measureCacheStrings;
    // Editor visual-line layout cache.
    mutable std::string visualLinesCacheBuffer;
    mutable float visualLinesCacheMaxW = -1.0f;
    mutable float visualLinesCacheFontSize = -1.0f;
    mutable std::vector<EditorVisualLine> visualLinesCache;

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
    // Reuse the variable editor popup for conversation JSON and scene narrative text.
    ConversationEditDoc editorDocTarget = ConversationEditDoc::None;
    std::string editorJsonPointer;
    std::string variableEditorSceneId;
    std::string variableEditorKey;
    std::string variableEditorBuffer;
    // Dual-pane text / TTS editing (conversation dialog only).
    bool editorTextTtsEnabled = false;
    bool editorShowTts = false; // false = text side, true = TTS side
    std::string editorTextSideBuffer;
    std::string editorTtsSideBuffer;
    std::string editorGlobalDefaultVoice = "leo";
    bool editorGlobalDefaultVoiceLoaded = false;

    // TTS syntax highlighting theme (resources/editor_tts_theme.json).
    struct TtsSyntaxTheme
    {
        Color defaultColor = {220, 212, 196, 255};
        Color command = {230, 140, 50, 255};      // [pause], [long-pause], …
        Color voiceMarkup = {235, 210, 70, 255};  // {{voice:eve}} / {{/voice}}
        Color voiceDialog = {140, 195, 235, 255}; // speech inside voice markup
    };
    TtsSyntaxTheme ttsSyntaxTheme{};
    bool ttsSyntaxThemeLoaded = false;
    std::string ttsHighlightCacheSource;
    std::vector<Color> ttsHighlightColors; // per-byte colors for variableEditorBuffer

    enum class TextTtsPairMode
    {
        None,
        StringWithSiblingTtsText,   // text field + parent.ttsText
        StringWithTtsObject,        // description + descriptionTts.ttsText
        ObjectSplit                 // object split into non-TTS JSON vs TTS JSON
    };
    TextTtsPairMode editorTextTtsMode = TextTtsPairMode::None;
    std::string editorTtsObjectKey; // e.g. descriptionTts when using StringWithTtsObject
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
    int variableEditorSelectAnchor = -1; // -1 = no selection; else selection is [min,max) with cursor
    bool variableEditorMouseSelecting = false;
    float variableEditorScrollY = 0.0f;
    std::string selectedVariableKey;
    std::string variableEditorError;
    int variableEditorIgnoreInputFrames = 0;
    Rectangle variableEditorField{0, 0, 0, 0};
    Rectangle variableEditorSaveBtn{0, 0, 0, 0};
    Rectangle variableEditorCancelBtn{0, 0, 0, 0};
    Rectangle variableEditorTextTtsToggle{0, 0, 0, 0};
    float variableEditorFontSize = 16.0f;
    float variableEditorLineHeight = 20.0f;
    float variableEditorPad = 8.0f;
    float variableEditorPreferX = 0.0f; // visual column for up/down
    float variableKeyRepeatTimer = 0.0f;
    int variableKeyRepeatKey = 0;

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

    std::string activeTabFilename() const
    {
        if (activeTabIndex < 0 || activeTabIndex >= static_cast<int>(jsonTabs.size()))
            return "";
        return jsonTabs[static_cast<size_t>(activeTabIndex)];
    }

    bool isConversationsTab() const
    {
        return activeTabFilename() == "conversations.json";
    }

    bool isScenesTab() const
    {
        return activeTabFilename() == "scenes.json";
    }

    bool loadConversationsDocument()
    {
        conversationsLoaded = false;
        conversationsRoot = nlohmann::json::object();
        conversationTree.clear();
        conversationsPath = pathJoin(resourceDir, "conversations.json");

        std::ifstream file(conversationsPath.c_str());
        if (!file.is_open())
        {
            loadError = "Failed to open conversations.json:\n" + conversationsPath;
            return false;
        }

        nlohmann::json parsed;
        try
        {
            file >> parsed;
        }
        catch (const nlohmann::json::exception& ex)
        {
            loadError = std::string("Failed to parse conversations.json:\n") + ex.what();
            return false;
        }

        if (!parsed.is_object())
        {
            loadError = "conversations.json root must be an object.";
            return false;
        }

        conversationsRoot = std::move(parsed);
        conversationsLoaded = true;
        rebuildConversationTree();
        return true;
    }

    bool saveConversationsDocument()
    {
        if (!conversationsLoaded || conversationsPath.empty())
            return false;

        std::ofstream out(conversationsPath.c_str());
        if (!out.is_open())
            return false;

        out << conversationsRoot.dump(2) << '\n';
        return out.good();
    }

    nlohmann::json* conversationJsonAt(const std::string& pointer)
    {
        if (!conversationsLoaded || pointer.empty())
            return nullptr;
        try
        {
            return &conversationsRoot.at(nlohmann::json::json_pointer(pointer));
        }
        catch (const nlohmann::json::exception&)
        {
            return nullptr;
        }
    }

    const nlohmann::json* conversationJsonAt(const std::string& pointer) const
    {
        if (!conversationsLoaded || pointer.empty())
            return nullptr;
        try
        {
            return &conversationsRoot.at(nlohmann::json::json_pointer(pointer));
        }
        catch (const nlohmann::json::exception&)
        {
            return nullptr;
        }
    }

    static std::string truncateForTree(const std::string& text, size_t maxLen)
    {
        std::string compact;
        compact.reserve(text.size());
        bool lastSpace = false;
        for (char ch : text)
        {
            if (ch == '\n' || ch == '\r' || ch == '\t')
            {
                if (!lastSpace && !compact.empty())
                {
                    compact.push_back(' ');
                    lastSpace = true;
                }
                continue;
            }
            compact.push_back(ch);
            lastSpace = (ch == ' ');
        }
        if (compact.size() <= maxLen)
            return compact;
        return compact.substr(0, maxLen - 1) + "…";
    }

    ConversationTreeNode makeNarrativeFieldNode(
        const std::string& sceneId,
        const std::string& label,
        const std::string& pointerUnderScene,
        const nlohmann::json& value) const
    {
        ConversationTreeNode node;
        node.kind = ConversationNodeKind::Narrative;
        node.key = "narrative:" + sceneId + ":" + pointerUnderScene;
        node.label = label;
        node.editDoc = ConversationEditDoc::Scenes;
        node.editSceneId = sceneId;
        node.jsonPointer = pointerUnderScene;
        if (value.is_string())
        {
            const std::string text = value.get<std::string>();
            node.detail = text.empty() ? "(empty)" : truncateForTree(text, 40);
        }
        else if (value.is_null())
            node.detail = "(null)";
        else
            node.detail = "{…}";
        return node;
    }

    void appendNarrativeFieldsFromObject(
        ConversationTreeNode& parent,
        const std::string& sceneId,
        const nlohmann::json& object,
        const std::string& pointerPrefix) const
    {
        static const char* kNarrativeKeys[] = {
            "description",
            "examineDetails",
            "speakDetails",
            "useDetails"
        };
        static const char* kNarrativeLabels[] = {
            "Description",
            "Examine",
            "Speak",
            "Use"
        };

        for (size_t i = 0; i < sizeof(kNarrativeKeys) / sizeof(kNarrativeKeys[0]); ++i)
        {
            const char* key = kNarrativeKeys[i];
            if (!object.contains(key))
                continue;
            const nlohmann::json& value = object[key];
            if (!value.is_string() && !value.is_null())
                continue;
            const std::string pointer = pointerPrefix.empty()
                ? std::string("/") + key
                : conversationPointerJoin(pointerPrefix, key);
            parent.children.push_back(makeNarrativeFieldNode(
                sceneId,
                kNarrativeLabels[i],
                pointer,
                value));
        }
    }

    void invalidateConversationVisibleRows()
    {
        conversationVisibleRowsDirty = true;
    }

    float measureUiTextWidth(const std::string& text, float fontSize) const
    {
        if (text.empty())
            return 0.0f;

        if (fontSize != measureCacheFontSize)
        {
            measureCacheFontSize = fontSize;
            measureCacheAsciiReady = false;
            measureCacheStrings.clear();
        }

        // Short strings (labels, glyphs) get a full-string cache.
        if (text.size() <= 96)
        {
            std::map<std::string, float>::const_iterator it = measureCacheStrings.find(text);
            if (it != measureCacheStrings.end())
                return it->second;
            const float w = MeasureTextEx(textFont(), text.c_str(), fontSize, 1.0f).x;
            if (measureCacheStrings.size() < 4096)
                measureCacheStrings[text] = w;
            return w;
        }

        return MeasureTextEx(textFont(), text.c_str(), fontSize, 1.0f).x;
    }

    float measureUiCharWidth(unsigned char ch, float fontSize) const
    {
        if (fontSize != measureCacheFontSize)
        {
            measureCacheFontSize = fontSize;
            measureCacheAsciiReady = false;
            measureCacheStrings.clear();
        }

        if (ch < 128)
        {
            if (!measureCacheAsciiReady)
            {
                char sample[2] = {0, 0};
                for (int i = 0; i < 128; ++i)
                {
                    sample[0] = static_cast<char>(i);
                    if (i < 32)
                        measureCacheAscii[i] = 0.0f;
                    else
                        measureCacheAscii[i] =
                            MeasureTextEx(textFont(), sample, fontSize, 1.0f).x;
                }
                measureCacheAsciiReady = true;
            }
            return measureCacheAscii[ch];
        }

        char sample[5] = {0, 0, 0, 0, 0};
        sample[0] = static_cast<char>(ch);
        return MeasureTextEx(textFont(), sample, fontSize, 1.0f).x;
    }

    void rebuildConversationTree()
    {
        conversationTree.clear();
        invalidateConversationVisibleRows();
        if (selectedSceneId.empty())
            return;

        // --- Main character (scene narrative from scenes.json) ---
        ConversationTreeNode mainCharacter;
        mainCharacter.kind = ConversationNodeKind::Section;
        mainCharacter.key = "section:main_character:" + selectedSceneId;
        mainCharacter.label = "Main Character";
        mainCharacter.detail = selectedSceneId;

        if (scenesDoc.isLoaded())
        {
            const nlohmann::json* scene = scenesDoc.sceneJson(selectedSceneId);
            if (scene != nullptr && scene->is_object())
            {
                appendNarrativeFieldsFromObject(mainCharacter, selectedSceneId, *scene, "");

                if (scene->contains("subScenes") && (*scene)["subScenes"].is_object())
                {
                    const nlohmann::json& subScenes = (*scene)["subScenes"];
                    std::vector<std::string> subIds;
                    for (auto it = subScenes.begin(); it != subScenes.end(); ++it)
                        subIds.push_back(it.key());
                    std::sort(subIds.begin(), subIds.end());

                    for (const std::string& subId : subIds)
                    {
                        if (!subScenes[subId].is_object())
                            continue;
                        ConversationTreeNode subNode;
                        subNode.kind = ConversationNodeKind::Section;
                        subNode.key = "section:subscene:" + selectedSceneId + ":" + subId;
                        subNode.label = "Sub-scene: " + subId;
                        subNode.detail = "focus / variant";
                        appendNarrativeFieldsFromObject(
                            subNode,
                            selectedSceneId,
                            subScenes[subId],
                            conversationPointerJoin("/subScenes", subId));
                        if (!subNode.children.empty())
                            mainCharacter.children.push_back(std::move(subNode));
                    }
                }
            }
        }

        if (!mainCharacter.children.empty())
            conversationTree.push_back(std::move(mainCharacter));

        // --- Actor conversations for this scene (conversations.json) ---
        if (conversationsLoaded && conversationsRoot.is_object() &&
            conversationsRoot.contains(selectedSceneId) &&
            conversationsRoot[selectedSceneId].is_object())
        {
            const nlohmann::json& sceneNode = conversationsRoot[selectedSceneId];
            const std::string scenePointer = conversationPointerJoin("", selectedSceneId);
            if (sceneNode.contains("speakPhases") && sceneNode["speakPhases"].is_array())
            {
                std::map<std::string, ConversationTreeNode> actorsById;
                const nlohmann::json& phases = sceneNode["speakPhases"];
                for (size_t phaseIndex = 0; phaseIndex < phases.size(); ++phaseIndex)
                {
                    const nlohmann::json& phase = phases[phaseIndex];
                    if (!phase.is_object())
                        continue;

                    const std::string actorId = phaseActorId(phase);
                    const std::string actorName = phaseActorName(phase, actorId);
                    const std::string phasePointer = conversationPointerIndex(
                        conversationPointerJoin(scenePointer, "speakPhases"),
                        phaseIndex);

                    if (actorsById.find(actorId) == actorsById.end())
                    {
                        ConversationTreeNode actor;
                        actor.kind = ConversationNodeKind::Actor;
                        actor.key = "actor:" + selectedSceneId + ":" + actorId;
                        actor.label = actorName;
                        actor.detail = actorId;
                        actorsById[actorId] = actor;
                    }
                    else if (actorsById[actorId].label == actorsById[actorId].detail &&
                             actorName != actorId)
                    {
                        actorsById[actorId].label = actorName;
                    }

                    ConversationTreeNode milestone;
                    milestone.kind = ConversationNodeKind::Milestone;
                    milestone.key = "phase:" + phasePointer;
                    milestone.editDoc = ConversationEditDoc::Conversations;
                    milestone.jsonPointer = phasePointer;
                    if (phase.contains("id") && phase["id"].is_string())
                        milestone.label = phase["id"].get<std::string>();
                    else
                        milestone.label = "(unnamed phase)";

                    milestone.detail = phase.value("type", "once");

                    // Phase-level narrative for quick text edit (also full phase via milestone row)
                    {
                        const char* phaseKeys[] = {"intro", "resumeIntro", "text"};
                        const char* phaseLabels[] = {"Intro", "Resume intro", "Text"};
                        for (size_t ki = 0; ki < 3; ++ki)
                        {
                            if (!phase.contains(phaseKeys[ki]) || !phase[phaseKeys[ki]].is_string())
                                continue;
                            ConversationTreeNode n;
                            n.kind = ConversationNodeKind::Narrative;
                            n.key = "narrative-conv:" + phasePointer + "/" + phaseKeys[ki];
                            n.label = phaseLabels[ki];
                            n.editDoc = ConversationEditDoc::Conversations;
                            n.jsonPointer = conversationPointerJoin(phasePointer, phaseKeys[ki]);
                            const std::string text = phase[phaseKeys[ki]].get<std::string>();
                            n.detail = text.empty() ? "(empty)" : truncateForTree(text, 40);
                            milestone.children.push_back(std::move(n));
                        }
                    }

                    if (phase.contains("choices") && phase["choices"].is_array())
                    {
                        const nlohmann::json& choices = phase["choices"];
                        for (size_t i = 0; i < choices.size(); ++i)
                        {
                            if (!choices[i].is_object())
                                continue;
                            ConversationTreeNode choiceNode = buildChoiceTreeNode(
                                choices[i],
                                conversationPointerIndex(
                                    conversationPointerJoin(phasePointer, "choices"), i));
                            stampConversationEditDoc(choiceNode);
                            milestone.children.push_back(std::move(choiceNode));
                        }
                    }

                    if (phase.contains("lines") && phase["lines"].is_array())
                    {
                        const nlohmann::json& lines = phase["lines"];
                        for (size_t i = 0; i < lines.size(); ++i)
                        {
                            if (!lines[i].is_object())
                                continue;
                            ConversationTreeNode lineNode = buildChoiceTreeNode(
                                lines[i],
                                conversationPointerIndex(
                                    conversationPointerJoin(phasePointer, "lines"), i));
                            if (lineNode.label == "(dialog)")
                            {
                                if (lines[i].contains("id") && lines[i]["id"].is_string())
                                    lineNode.label = lines[i]["id"].get<std::string>();
                                else
                                    lineNode.label = "line " + std::to_string(i);
                            }
                            stampConversationEditDoc(lineNode);
                            milestone.children.push_back(std::move(lineNode));
                        }
                    }

                    actorsById[actorId].children.push_back(std::move(milestone));
                }

                ConversationTreeNode actorsSection;
                actorsSection.kind = ConversationNodeKind::Section;
                actorsSection.key = "section:actors:" + selectedSceneId;
                actorsSection.label = "Actors";
                actorsSection.detail = "conversations";

                for (auto& entry : actorsById)
                {
                    std::sort(
                        entry.second.children.begin(),
                        entry.second.children.end(),
                        [](const ConversationTreeNode& a, const ConversationTreeNode& b)
                        {
                            if (a.label != b.label)
                                return a.label < b.label;
                            return a.detail < b.detail;
                        });
                    actorsSection.children.push_back(std::move(entry.second));
                }

                std::sort(
                    actorsSection.children.begin(),
                    actorsSection.children.end(),
                    [](const ConversationTreeNode& a, const ConversationTreeNode& b)
                    {
                        if (a.label != b.label)
                            return a.label < b.label;
                        return a.detail < b.detail;
                    });

                if (!actorsSection.children.empty())
                    conversationTree.push_back(std::move(actorsSection));
            }
        }
    }

    static void stampConversationEditDoc(ConversationTreeNode& node)
    {
        node.editDoc = ConversationEditDoc::Conversations;
        for (ConversationTreeNode& child : node.children)
            stampConversationEditDoc(child);
    }

    bool isConversationExpanded(const std::string& key) const
    {
        return conversationExpanded.count(key) > 0;
    }

    void toggleConversationExpanded(const std::string& key)
    {
        if (conversationExpanded.count(key) > 0)
            conversationExpanded.erase(key);
        else
            conversationExpanded.insert(key);
        invalidateConversationVisibleRows();
    }

    void collectVisibleConversationRows(
        const ConversationTreeNode& node,
        int depth,
        bool isLastChild,
        std::vector<bool> ancestorContinues,
        std::vector<ConversationVisibleRow>& out) const
    {
        ConversationVisibleRow row;
        row.node = &node;
        row.depth = depth;
        row.isLastChild = isLastChild;
        row.ancestorContinues = ancestorContinues;
        out.push_back(row);

        if (!node.children.empty() && isConversationExpanded(node.key))
        {
            std::vector<bool> childAncestors = ancestorContinues;
            childAncestors.push_back(!isLastChild);
            for (size_t i = 0; i < node.children.size(); ++i)
            {
                const bool childLast = (i + 1 == node.children.size());
                collectVisibleConversationRows(
                    node.children[i],
                    depth + 1,
                    childLast,
                    childAncestors,
                    out);
            }
        }
    }

    const std::vector<ConversationVisibleRow>& visibleConversationRows() const
    {
        if (!conversationVisibleRowsDirty)
            return conversationVisibleRowsCache;

        conversationVisibleRowsCache.clear();
        for (size_t i = 0; i < conversationTree.size(); ++i)
        {
            const bool last = (i + 1 == conversationTree.size());
            collectVisibleConversationRows(
                conversationTree[i],
                0,
                last,
                {},
                conversationVisibleRowsCache);
        }
        conversationVisibleRowsDirty = false;
        return conversationVisibleRowsCache;
    }

    bool loadActiveDocument()
    {
        loadError.clear();
        closeVariableEditor();

        if (!resourceDirectoryExists(resourceDir))
        {
            loadError = "Resources folder not found:\n" + resourceDir +
                "\n\nLaunch with:\n./scene-editor /path/to/resources";
            scenesDoc = SceneDocument{};
            conversationsLoaded = false;
            conversationsRoot = nlohmann::json::object();
            conversationTree.clear();
            selectedSceneId.clear();
            return false;
        }

        if (jsonTabs.empty())
        {
            loadError = "No .json files found in:\n" + resourceDir;
            scenesDoc = SceneDocument{};
            conversationsLoaded = false;
            conversationsRoot = nlohmann::json::object();
            conversationTree.clear();
            selectedSceneId.clear();
            return false;
        }

        const std::string filename = activeTabFilename();
        if (filename != "scenes.json" && filename != "conversations.json")
        {
            loadError = filename + " editing is not implemented yet.\nSelect the scenes.json or conversations.json tab.";
            return false;
        }

        // Both tabs need the scene map; conversations also needs conversations.json.
        const std::string scenesPath = pathJoin(resourceDir, "scenes.json");
        if (!scenesDoc.load(scenesPath))
        {
            loadError = "Failed to load scenes.json:\n" + scenesPath;
            scenesDoc = SceneDocument{};
            selectedSceneId.clear();
            conversationTree.clear();
            return false;
        }

        dirty = false;
        if (selectedSceneId.empty() || !scenesDoc.hasScene(selectedSceneId))
        {
            const std::vector<std::string> ids = scenesDoc.sceneIds();
            selectedSceneId = ids.empty() ? "" : ids.front();
        }

        ensureDefaultLayouts();

        if (filename == "conversations.json")
        {
            if (!loadConversationsDocument())
            {
                conversationTree.clear();
                return false;
            }
            dirty = false;
            leftScroll = 0.0f;
            rebuildConversationTree();
            // Expand top-level sections by default for the selected scene.
            for (const ConversationTreeNode& root : conversationTree)
                conversationExpanded.insert(root.key);
            return true;
        }

        return true;
    }

    void selectSceneForEditor(const std::string& id)
    {
        if (id.empty() || selectedSceneId == id)
            return;
        selectedSceneId = id;
        selectedVariableKey.clear();
        variablesScroll = 0.0f;
        if (isConversationsTab())
        {
            leftScroll = 0.0f;
            selectedConversationKey.clear();
            rebuildConversationTree();
            for (const ConversationTreeNode& root : conversationTree)
                conversationExpanded.insert(root.key);
        }
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

        float cellHeight = kSceneCardMinHeight;
        for (std::map<std::string, std::pair<int, int> >::const_iterator it = grid.begin();
             it != grid.end();
             ++it)
        {
            const float h = measureSceneCard(it->first).height;
            if (h > cellHeight)
                cellHeight = h;
        }

        const float pitchX = kSceneCardWidth + kLayoutGapX;
        const float pitchY = cellHeight + kLayoutGapY;
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
        bool ok = true;
        if (conversationsLoaded)
            ok = saveConversationsDocument() && ok;
        if (scenesDoc.isLoaded())
            ok = scenesDoc.save() && ok;
        if (ok)
            dirty = false;
        return ok && (scenesDoc.isLoaded() || conversationsLoaded);
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
                       {leftBounds.x + 8.0f, leftBounds.y + 8.0f}, kListTabFont, 1.0f, kTextMuted);
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
            const float fontSize = kListTabFont;
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
                kListMetaFont,
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
            const Rectangle thumbRect = {row.x + 6.0f, row.y + 6.0f, kListThumbSize, kListThumbSize};
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
            const float textX = row.x + kListThumbSize + 14.0f;
            const float textY = row.y + (kListRowHeight - kListNameFont - kListMetaFont - 8.0f) * 0.5f;
            DrawTextEx(textFont(), id.c_str(), {textX, textY},
                       kListNameFont, 1.0f, kTextPrimary);
            DrawTextEx(
                textFont(),
                TextFormat("L%d", sceneLevel),
                {textX, textY + kListNameFont + 6.0f},
                kListMetaFont,
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
                    selectSceneForEditor(id);
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

    void handleConversationTreeInput(Rectangle listBounds)
    {
        conversationListBounds = listBounds;
        conversationListBoundsValid = true;

        if (!scenesDoc.isLoaded() || selectedSceneId.empty() || conversationTree.empty())
            return;
        if (stackDialogOpen || variableEditorOpen || isDraggingDivider())
            return;

        const std::vector<ConversationVisibleRow>& rows = visibleConversationRows();
        const float contentHeight = static_cast<float>(rows.size()) * kTreeRowHeight + 8.0f;
        const float maxScroll = std::max(0.0f, contentHeight - listBounds.height);
        if (leftScroll > maxScroll)
            leftScroll = maxScroll;

        const Rectangle treeBounds = {
            listBounds.x,
            listBounds.y + 20.0f,
            listBounds.width,
            listBounds.height - 20.0f};
        const Vector2 mouse = GetMousePosition();

        if (CheckCollisionPointRec(mouse, treeBounds) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            const float localY = (mouse.y - treeBounds.y - 4.0f) + leftScroll;
            if (localY >= 0.0f)
            {
                const int index = static_cast<int>(localY / kTreeRowHeight);
                if (index >= 0 && index < static_cast<int>(rows.size()) &&
                    rows[static_cast<size_t>(index)].node != nullptr)
                {
                    const ConversationVisibleRow& hit = rows[static_cast<size_t>(index)];
                    const ConversationTreeNode& node = *hit.node;
                    selectedConversationKey = node.key;

                    const float rowTop =
                        treeBounds.y + 4.0f - leftScroll + static_cast<float>(index) * kTreeRowHeight;
                    const float toggleX =
                        treeBounds.x + 8.0f + static_cast<float>(hit.depth) * kTreeIndent;
                    const float midY = rowTop + kTreeRowHeight * 0.5f;
                    const Rectangle toggleBounds = {
                        toggleX,
                        midY - kTreeToggleSize * 0.5f,
                        kTreeToggleSize,
                        kTreeToggleSize};

                    if (!node.children.empty() && CheckCollisionPointRec(mouse, toggleBounds))
                        toggleConversationExpanded(node.key);
                    else if (node.editDoc != ConversationEditDoc::None && !node.jsonPointer.empty())
                        openConversationNodeEditor(node);
                    else if (!node.children.empty())
                        toggleConversationExpanded(node.key);
                }
            }
        }

        if (CheckCollisionPointRec(mouse, treeBounds))
            leftScroll -= GetMouseWheelMove() * 24.0f;
        if (leftScroll < 0.0f)
            leftScroll = 0.0f;
        if (leftScroll > maxScroll)
            leftScroll = maxScroll;
    }

    void drawConversationTree(Rectangle listBounds)
    {
        conversationListBounds = listBounds;
        conversationListBoundsValid = true;

        if (!scenesDoc.isLoaded())
        {
            const std::string message = loadError.empty()
                ? "Loading..."
                : loadError;
            drawWrappedText(
                textFont(),
                message,
                {listBounds.x + 12.0f, listBounds.y + 12.0f},
                listBounds.width - 24.0f,
                kListMetaFont,
                4.0f,
                kTextMuted);
            return;
        }

        if (selectedSceneId.empty())
        {
            DrawTextEx(
                textFont(),
                "Select a scene on the map",
                {listBounds.x + 12.0f, listBounds.y + 12.0f},
                kFontBody,
                1.0f,
                kTextMuted);
            return;
        }

        if (conversationTree.empty())
        {
            DrawTextEx(
                textFont(),
                "No narrative or conversations for this scene",
                {listBounds.x + 12.0f, listBounds.y + 36.0f},
                kFontBody,
                1.0f,
                kTextMuted);
            DrawTextEx(
                textFont(),
                selectedSceneId.c_str(),
                {listBounds.x + 12.0f, listBounds.y + 12.0f},
                kFontSmall,
                1.0f,
                kPanelBorder);
            return;
        }

        const std::vector<ConversationVisibleRow>& rows = visibleConversationRows();
        const float contentHeight = static_cast<float>(rows.size()) * kTreeRowHeight + 8.0f;
        const float maxScroll = std::max(0.0f, contentHeight - listBounds.height);
        if (leftScroll > maxScroll)
            leftScroll = maxScroll;

        const Vector2 mouse = GetMousePosition();
        const bool canInteract =
            !stackDialogOpen &&
            !variableEditorOpen &&
            !isDraggingDivider();

        // Header: selected scene
        DrawTextEx(
            textFont(),
            truncate(selectedSceneId, 42).c_str(),
            {listBounds.x + 10.0f, listBounds.y + 4.0f},
            kFontTiny,
            1.0f,
            kPanelBorder);

        const Rectangle treeBounds = {
            listBounds.x,
            listBounds.y + 20.0f,
            listBounds.width,
            listBounds.height - 20.0f};

        BeginScissorMode(
            static_cast<int>(treeBounds.x),
            static_cast<int>(treeBounds.y),
            static_cast<int>(treeBounds.width),
            static_cast<int>(treeBounds.height));

        float y = treeBounds.y + 4.0f - leftScroll;
        for (const ConversationVisibleRow& row : rows)
        {
            if (row.node == nullptr)
                continue;

            const ConversationTreeNode& node = *row.node;
            const float rowTop = y;
            const Rectangle rowBounds = {
                treeBounds.x + 2.0f,
                rowTop,
                treeBounds.width - 4.0f,
                kTreeRowHeight - 1.0f};

            const bool selected = node.key == selectedConversationKey;
            const bool hovered =
                canInteract &&
                CheckCollisionPointRec(mouse, treeBounds) &&
                CheckCollisionPointRec(mouse, rowBounds);

            if (selected)
                DrawRectangleRec(rowBounds, kSelection);
            else if (hovered)
                DrawRectangleRec(rowBounds, Color{60, 54, 72, 180});

            // Indent guides / tree lines
            const float baseX = treeBounds.x + 8.0f;
            const Color lineColor = {96, 86, 72, 220};
            for (int d = 0; d < row.depth; ++d)
            {
                if (d >= static_cast<int>(row.ancestorContinues.size()))
                    break;
                if (!row.ancestorContinues[static_cast<size_t>(d)])
                    continue;
                const float guideX = baseX + static_cast<float>(d) * kTreeIndent + kTreeToggleSize * 0.5f;
                DrawLineEx(
                    {guideX, rowTop},
                    {guideX, rowTop + kTreeRowHeight},
                    1.0f,
                    lineColor);
            }

            const float toggleX = baseX + static_cast<float>(row.depth) * kTreeIndent;
            const float midY = rowTop + kTreeRowHeight * 0.5f;
            if (row.depth > 0)
            {
                const float parentGuideX =
                    baseX + static_cast<float>(row.depth - 1) * kTreeIndent + kTreeToggleSize * 0.5f;
                const float elbowY = midY;
                DrawLineEx(
                    {parentGuideX, rowTop},
                    {parentGuideX, elbowY},
                    1.0f,
                    lineColor);
                DrawLineEx(
                    {parentGuideX, elbowY},
                    {toggleX + kTreeToggleSize * 0.5f, elbowY},
                    1.0f,
                    lineColor);
            }

            const bool hasChildren = !node.children.empty();
            const Rectangle toggleBounds = {
                toggleX,
                midY - kTreeToggleSize * 0.5f,
                kTreeToggleSize,
                kTreeToggleSize};

            if (hasChildren)
            {
                DrawRectangleRec(toggleBounds, Color{40, 36, 48, 255});
                DrawRectangleLinesEx(toggleBounds, 1.0f, kPanelBorder);
                const bool expanded = isConversationExpanded(node.key);
                const char* glyph = expanded ? "-" : "+";
                const float glyphW = measureUiTextWidth(glyph, kFontSmall);
                const float glyphH = kFontSmall;
                DrawTextEx(
                    textFont(),
                    glyph,
                    {
                        toggleBounds.x + (toggleBounds.width - glyphW) * 0.5f,
                        toggleBounds.y + (toggleBounds.height - glyphH) * 0.5f - 1.0f
                    },
                    kFontSmall,
                    1.0f,
                    kTextPrimary);
            }
            else
            {
                DrawCircleV({toggleX + kTreeToggleSize * 0.5f, midY}, 2.0f, lineColor);
            }

            const float textX = toggleX + kTreeToggleSize + kTreeTogglePad + 2.0f;
            Color labelColor = kTextPrimary;
            if (node.kind == ConversationNodeKind::Section)
                labelColor = kPanelBorder;
            else if (node.kind == ConversationNodeKind::Actor)
                labelColor = Color{200, 180, 120, 255};
            else if (node.kind == ConversationNodeKind::Milestone)
                labelColor = kTextPrimary;
            else if (node.kind == ConversationNodeKind::Narrative)
                labelColor = Color{180, 200, 190, 255};
            else
                labelColor = kTextMuted;

            const std::string display = truncate(node.label, 48);
            DrawTextEx(
                textFont(),
                display.c_str(),
                {textX, rowTop + 4.0f},
                kFontSmall,
                1.0f,
                labelColor);

            if (!node.detail.empty() && node.kind != ConversationNodeKind::Dialog)
            {
                const float labelW = measureUiTextWidth(display, kFontSmall);
                const std::string detail = truncate(node.detail, 36);
                DrawTextEx(
                    textFont(),
                    detail.c_str(),
                    {textX + labelW + 8.0f, rowTop + 5.0f},
                    kFontTiny,
                    1.0f,
                    kTextMuted);
            }

            y += kTreeRowHeight;
        }

        EndScissorMode();
    }

    Vector2 sceneCardScreenPos(const SceneLayout& layout, Rectangle canvasBounds) const
    {
        return {
            canvasBounds.x + layout.x + canvasScroll.x,
            canvasBounds.y + layout.y + canvasScroll.y
        };
    }

    std::vector<std::string> wrapTextToWidth(
        const std::string& text,
        float maxWidth,
        float fontSize) const
    {
        std::vector<std::string> lines;
        if (text.empty())
        {
            lines.push_back("");
            return lines;
        }

        // Prefer wrapping on '_' and word boundaries for scene ids.
        std::string current;
        std::string token;
        auto flushToken = [&]()
        {
            if (token.empty())
                return;
            const std::string candidate = current.empty() ? token : current + token;
            if (!current.empty() &&
                MeasureTextEx(textFont(), candidate.c_str(), fontSize, 1.0f).x > maxWidth)
            {
                lines.push_back(current);
                current = token;
            }
            else
            {
                current = candidate;
            }
            token.clear();
        };

        for (size_t i = 0; i < text.size(); ++i)
        {
            const char ch = text[i];
            token.push_back(ch);
            const bool breakAfter =
                ch == '_' || ch == '-' || ch == ' ' || ch == '/' || ch == '.';
            if (breakAfter)
                flushToken();
        }
        flushToken();
        if (!current.empty())
            lines.push_back(current);

        // Hard-break any leftover token longer than the width.
        std::vector<std::string> fitted;
        for (size_t li = 0; li < lines.size(); ++li)
        {
            std::string line = lines[li];
            while (MeasureTextEx(textFont(), line.c_str(), fontSize, 1.0f).x > maxWidth &&
                   line.size() > 1)
            {
                size_t cut = line.size();
                while (cut > 1 &&
                       MeasureTextEx(textFont(), line.substr(0, cut).c_str(), fontSize, 1.0f).x >
                           maxWidth)
                {
                    --cut;
                }
                if (cut < 1)
                    cut = 1;
                fitted.push_back(line.substr(0, cut));
                line = line.substr(cut);
            }
            if (!line.empty())
                fitted.push_back(line);
        }
        if (fitted.empty())
            fitted.push_back(text);
        return fitted;
    }

    struct SceneCardMetrics
    {
        float width = kSceneCardWidth;
        float height = kSceneCardMinHeight;
        float thumbHeight = kSceneCardThumbHeight;
        std::vector<std::string> titleLines;
    };

    SceneCardMetrics measureSceneCard(const std::string& sceneId) const
    {
        SceneCardMetrics metrics;
        metrics.width = kSceneCardWidth;
        const float titleWidth = metrics.width - 12.0f;
        metrics.titleLines = wrapTextToWidth(sceneId, titleWidth, kSceneCardTitleFont);
        if (static_cast<int>(metrics.titleLines.size()) > kSceneCardMaxTitleLines)
            metrics.titleLines.resize(static_cast<size_t>(kSceneCardMaxTitleLines));

        const float titleBlock =
            static_cast<float>(metrics.titleLines.size()) * kSceneCardTitleLineHeight;
        metrics.thumbHeight = kSceneCardThumbHeight;
        metrics.height = 6.0f + metrics.thumbHeight + 6.0f + titleBlock + 6.0f;
        if (metrics.height < kSceneCardMinHeight)
            metrics.height = kSceneCardMinHeight;
        return metrics;
    }

    float maxSceneCardHeightOnLevel(int level) const
    {
        float maxH = kSceneCardMinHeight;
        const std::vector<std::string> ids = scenesOnLevel(level);
        for (size_t i = 0; i < ids.size(); ++i)
        {
            const float h = measureSceneCard(ids[i]).height;
            if (h > maxH)
                maxH = h;
        }
        return maxH;
    }

    Rectangle sceneCardBounds(const std::string& sceneId, Rectangle canvasBounds) const
    {
        const SceneLayout layout = scenesDoc.getLayout(sceneId);
        const Vector2 pos = sceneCardScreenPos(layout, canvasBounds);
        const SceneCardMetrics metrics = measureSceneCard(sceneId);
        return {pos.x, pos.y, metrics.width, metrics.height};
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

    bool isHorizontalSeg(Vector2 a, Vector2 b) const
    {
        return std::fabs(a.y - b.y) < 0.75f && std::fabs(a.x - b.x) > 0.75f;
    }

    bool isVerticalSeg(Vector2 a, Vector2 b) const
    {
        return std::fabs(a.x - b.x) < 0.75f && std::fabs(a.y - b.y) > 0.75f;
    }

    // Orthogonal H×V interior crossing (not at endpoints).
    bool findOrthogonalCrossing(
        Vector2 a1,
        Vector2 a2,
        Vector2 b1,
        Vector2 b2,
        Vector2& outCross) const
    {
        const bool aH = isHorizontalSeg(a1, a2);
        const bool aV = isVerticalSeg(a1, a2);
        const bool bH = isHorizontalSeg(b1, b2);
        const bool bV = isVerticalSeg(b1, b2);
        if (!(aH && bV) && !(aV && bH))
            return false;

        Vector2 h1, h2, v1, v2;
        if (aH && bV)
        {
            h1 = a1;
            h2 = a2;
            v1 = b1;
            v2 = b2;
        }
        else
        {
            h1 = b1;
            h2 = b2;
            v1 = a1;
            v2 = a2;
        }

        const float y = h1.y;
        const float x = v1.x;
        const float hMinX = std::min(h1.x, h2.x);
        const float hMaxX = std::max(h1.x, h2.x);
        const float vMinY = std::min(v1.y, v2.y);
        const float vMaxY = std::max(v1.y, v2.y);
        const float margin = kWireHopRadius + 2.0f;
        if (x <= hMinX + margin || x >= hMaxX - margin)
            return false;
        if (y <= vMinY + margin || y >= vMaxY - margin)
            return false;

        outCross = {x, y};
        return true;
    }

    void drawWireLine(Vector2 a, Vector2 b, float thick, Color color) const
    {
        DrawLineEx(a, b, thick, color);
    }

    // Semicircle hop so one wire jumps over another (P&ID / electrical style).
    void drawWireHop(Vector2 center, bool hopIsOnHorizontal, Vector2 travelDir) const
    {
        const float r = kWireHopRadius;
        const int segments = 12;
        Vector2 prev{};
        for (int i = 0; i <= segments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            Vector2 p;
            if (hopIsOnHorizontal)
            {
                // Upper semicircle (bulges toward smaller y). Traverse with travel direction.
                // Standard angle 0=right, CCW; y-down screen uses y = center.y - r*sin.
                const float a = (travelDir.x >= 0.0f)
                    ? (PI - t * PI)   // left -> up -> right
                    : (t * PI);       // right -> up -> left
                p = {center.x + r * std::cos(a), center.y - r * std::sin(a)};
            }
            else
            {
                // Right semicircle (bulges toward larger x).
                const float a = (travelDir.y >= 0.0f)
                    ? (-PI * 0.5f + t * PI)  // up -> right -> down
                    : (PI * 0.5f + t * PI);  // down -> right -> up
                p = {center.x + r * std::cos(a), center.y - r * std::sin(a)};
            }

            if (i > 0)
            {
                drawWireLine(prev, p, 4.0f, Color{8, 7, 12, 220});
                drawWireLine(prev, p, 2.0f, kExitArrow);
            }
            prev = p;
        }
    }

    void drawOrthogonalSegWithHops(
        Vector2 a,
        Vector2 b,
        std::vector<Vector2> hops) const
    {
        if (std::fabs(a.x - b.x) < 0.5f && std::fabs(a.y - b.y) < 0.5f)
            return;

        const bool horizontal = isHorizontalSeg(a, b);
        const bool vertical = isVerticalSeg(a, b);
        if (!horizontal && !vertical)
        {
            drawWireLine(a, b, 4.0f, Color{8, 7, 12, 220});
            drawWireLine(a, b, 2.0f, kExitArrow);
            return;
        }

        // Sort hops along travel from a -> b.
        std::sort(hops.begin(), hops.end(), [&](const Vector2& p, const Vector2& q)
        {
            if (horizontal)
                return (a.x <= b.x) ? (p.x < q.x) : (p.x > q.x);
            return (a.y <= b.y) ? (p.y < q.y) : (p.y > q.y);
        });

        Vector2 dir = Vector2Subtract(b, a);
        const float len = Vector2Length(dir);
        if (len < 1.0f)
            return;
        dir = Vector2Scale(dir, 1.0f / len);

        Vector2 prev = a;
        for (size_t i = 0; i < hops.size(); ++i)
        {
            const Vector2& hop = hops[i];
            // Skip hops too close to ends or to previous hop.
            const float distA = Vector2Distance(hop, a);
            const float distB = Vector2Distance(hop, b);
            if (distA < kWireHopRadius * 2.0f || distB < kWireHopRadius * 2.0f)
                continue;

            const Vector2 before = Vector2Subtract(hop, Vector2Scale(dir, kWireHopRadius));
            const Vector2 after = Vector2Add(hop, Vector2Scale(dir, kWireHopRadius));

            // Ensure before is still ahead of prev along the path.
            if (Vector2DotProduct(Vector2Subtract(before, prev), dir) < 0.0f)
                continue;

            drawWireLine(prev, before, 4.0f, Color{8, 7, 12, 220});
            drawWireLine(prev, before, 2.0f, kExitArrow);
            drawWireHop(hop, horizontal, dir);
            prev = after;
        }

        drawWireLine(prev, b, 4.0f, Color{8, 7, 12, 220});
        drawWireLine(prev, b, 2.0f, kExitArrow);
    }

    void drawPolyline(
        const std::vector<Vector2>& points,
        bool arrowAtStart,
        bool arrowAtEnd,
        bool semicircleAtStart,
        const std::string& fromSide,
        const std::vector<std::vector<Vector2> >& hopsPerSegment) const
    {
        if (points.size() < 2)
            return;

        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            std::vector<Vector2> hops;
            if (i < hopsPerSegment.size())
                hops = hopsPerSegment[i];
            drawOrthogonalSegWithHops(points[i], points[i + 1], hops);
        }

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

        struct LinkRoute
        {
            std::vector<Vector2> points;
            bool arrowAtStart = false;
            bool arrowAtEnd = true;
            bool semicircleAtStart = false;
            std::string fromSide;
        };

        std::vector<LinkRoute> routes;

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

                LinkRoute route;
                route.points = buildOrthogonalRoute(fromCard, toCard, direction, obstacles);
                route.arrowAtStart = reciprocalOpposite;
                route.arrowAtEnd = true;
                route.semicircleAtStart = !reciprocalOpposite;

                int dCol = 0;
                int dRow = 0;
                if (directionDelta(direction, dCol, dRow))
                {
                    if (dCol > 0)
                        route.fromSide = "right";
                    else if (dCol < 0)
                        route.fromSide = "left";
                    else if (dRow < 0)
                        route.fromSide = "top";
                    else
                        route.fromSide = "bottom";
                }
                else
                {
                    route.fromSide = facingSide(fromCard, toCard);
                }

                routes.push_back(route);
            }
        }

        // hopsPerRoute[route][seg] = hop centers on that segment
        std::vector<std::vector<std::vector<Vector2> > > hopsPerRoute(routes.size());
        for (size_t r = 0; r < routes.size(); ++r)
            hopsPerRoute[r].assign(
                routes[r].points.empty() ? 0 : routes[r].points.size() - 1,
                std::vector<Vector2>());

        // Detect H×V crossings between different routes; hop the later route's segment
        // (schematic jump) so crossings stay readable.
        for (size_t r0 = 0; r0 < routes.size(); ++r0)
        {
            const std::vector<Vector2>& p0 = routes[r0].points;
            for (size_t s0 = 0; s0 + 1 < p0.size(); ++s0)
            {
                for (size_t r1 = r0 + 1; r1 < routes.size(); ++r1)
                {
                    const std::vector<Vector2>& p1 = routes[r1].points;
                    for (size_t s1 = 0; s1 + 1 < p1.size(); ++s1)
                    {
                        Vector2 cross;
                        if (!findOrthogonalCrossing(p0[s0], p0[s0 + 1], p1[s1], p1[s1 + 1], cross))
                            continue;

                        // Prefer hopping the horizontal segment (classic P&ID style).
                        // If neither or both, hop the higher-index route's segment.
                        const bool h0 = isHorizontalSeg(p0[s0], p0[s0 + 1]);
                        const bool h1 = isHorizontalSeg(p1[s1], p1[s1 + 1]);
                        size_t hopRoute = r1;
                        size_t hopSeg = s1;
                        if (h0 && !h1)
                        {
                            hopRoute = r0;
                            hopSeg = s0;
                        }
                        else if (h1 && !h0)
                        {
                            hopRoute = r1;
                            hopSeg = s1;
                        }

                        hopsPerRoute[hopRoute][hopSeg].push_back(cross);
                    }
                }
            }
        }

        for (size_t r = 0; r < routes.size(); ++r)
        {
            drawPolyline(
                routes[r].points,
                routes[r].arrowAtStart,
                routes[r].arrowAtEnd,
                routes[r].semicircleAtStart,
                routes[r].fromSide,
                hopsPerRoute[r]);
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
            kFontBody,
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
            kFontTitle,
            1.0f,
            canGoDown ? kTextPrimary : kTextDisabled);
        DrawTextEx(
            textFont(),
            "+",
            {levelUpBtn.x + 9.0f, levelUpBtn.y + 3.0f},
            kFontTitle,
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
            const SceneCardMetrics metrics = measureSceneCard(ids[i]);
            const float right = layout.x + metrics.width;
            const float bottom = layout.y + metrics.height;
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
                ? "Select the scenes.json or conversations.json tab."
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

            const SceneCardMetrics metrics = measureSceneCard(id);
            const Rectangle card = sceneCardBounds(id, canvasBounds);
            const bool selected = id == selectedSceneId;
            DrawRectangleRec(card, selected ? Color{52, 46, 62, 255} : Color{36, 32, 44, 255});
            DrawRectangleLinesEx(card, selected ? 2.0f : 1.0f, selected ? kPanelBorder : kPanelAccent);

            const ThumbnailEntry& thumb = ensureThumbnail(id);
            const Rectangle thumbRect = {
                card.x + 6.0f,
                card.y + 6.0f,
                card.width - 12.0f,
                metrics.thumbHeight};
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

            float titleY = thumbRect.y + thumbRect.height + 4.0f;
            for (size_t lineIndex = 0; lineIndex < metrics.titleLines.size(); ++lineIndex)
            {
                DrawTextEx(
                    textFont(),
                    metrics.titleLines[lineIndex].c_str(),
                    {card.x + 6.0f, titleY},
                    kSceneCardTitleFont,
                    1.0f,
                    kTextPrimary);
                titleY += kSceneCardTitleLineHeight;
            }

            if (!stackDialogOpen &&
                !variableEditorOpen &&
                !isDraggingDivider() &&
                !draggingHScroll &&
                !draggingVScroll)
            {
                const bool hovered = CheckCollisionPointRec(GetMousePosition(), card);
                if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                {
                    selectSceneForEditor(id);
                    // Allow layout drag on scenes tab; conversations tab is selection-only.
                    if (!isConversationsTab())
                    {
                        dragSource = DragSource::Canvas;
                        dragSceneId = id;
                        dragOffset = {GetMouseX() - card.x, GetMouseY() - card.y};
                    }
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
            const SceneCardMetrics dragMetrics = measureSceneCard(dragSceneId);
            const Rectangle ghost = {
                static_cast<float>(GetMouseX()) - dragOffset.x,
                static_cast<float>(GetMouseY()) - dragOffset.y,
                dragMetrics.width,
                dragMetrics.height};
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
                    kFontTiny,
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
                const SceneCardMetrics dropMetrics = measureSceneCard(dragSceneId);
                const Rectangle ghost = {
                    static_cast<float>(GetMouseX()) - dragOffset.x,
                    static_cast<float>(GetMouseY()) - dragOffset.y,
                    dropMetrics.width,
                    dropMetrics.height};
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
            kFontHeading,
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
            kFontBody,
            4.0f,
            kTextMuted);

        DrawTextEx(
            textFont(),
            "Up = one floor above  |  Down = one floor below",
            {dialog.x + 20.0f, dialog.y + 100.0f},
            kFontTiny,
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
            const Vector2 size = MeasureTextEx(textFont(), label, kFontBody, 1.0f);
            DrawTextEx(
                textFont(),
                label,
                {bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + 9.0f},
                kFontBody,
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
        editorDocTarget = ConversationEditDoc::None;
        editorJsonPointer.clear();
        variableEditorSceneId.clear();
        variableEditorKey.clear();
        variableEditorBuffer.clear();
        editorTextTtsEnabled = false;
        editorShowTts = false;
        editorTextSideBuffer.clear();
        editorTtsSideBuffer.clear();
        editorTextTtsMode = TextTtsPairMode::None;
        editorTtsObjectKey.clear();
        variableEditorCursor = 0;
        variableEditorSelectAnchor = -1;
        variableEditorMouseSelecting = false;
        variableEditorScrollY = 0.0f;
        variableEditorError.clear();
        variableKeyRepeatKey = 0;
        variableKeyRepeatTimer = 0.0f;
        variableEditorTextTtsToggle = {0, 0, 0, 0};
    }

    static std::string unescapeJsonPointerToken(const std::string& token)
    {
        std::string out;
        out.reserve(token.size());
        for (size_t i = 0; i < token.size(); ++i)
        {
            if (token[i] == '~' && i + 1 < token.size())
            {
                if (token[i + 1] == '0')
                {
                    out.push_back('~');
                    ++i;
                    continue;
                }
                if (token[i + 1] == '1')
                {
                    out.push_back('/');
                    ++i;
                    continue;
                }
            }
            out.push_back(token[i]);
        }
        return out;
    }

    static bool splitJsonPointer(
        const std::string& pointer,
        std::string& parentOut,
        std::string& leafOut)
    {
        if (pointer.empty() || pointer[0] != '/')
            return false;
        const size_t last = pointer.find_last_of('/');
        if (last == std::string::npos)
            return false;
        parentOut = pointer.substr(0, last);
        leafOut = unescapeJsonPointerToken(pointer.substr(last + 1));
        return !leafOut.empty();
    }

    static bool isTtsJsonKey(const std::string& key)
    {
        if (key == "tts" || key == "ttsVoice" || key == "ttsText" || key == "ttsAudio" ||
            key == "ttsAudioSegments" || key == "ttsTextSha256" ||
            key == "ttsAfter" || key == "ttsAfterVoice" || key == "ttsAfterText" ||
            key == "ttsAfterAudio" || key == "ttsAfterAudioSegments" ||
            key == "resumeTts" || key == "resumeTtsVoice" || key == "resumeTtsText" ||
            key == "resumeTtsAudio")
            return true;
        // Nested TTS bags on scene narrative fields
        if (key.size() >= 3 && key.compare(key.size() - 3, 3, "Tts") == 0)
            return true;
        return false;
    }

    static nlohmann::json stripTtsKeys(const nlohmann::json& object)
    {
        nlohmann::json out = nlohmann::json::object();
        if (!object.is_object())
            return out;
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            if (!isTtsJsonKey(it.key()))
                out[it.key()] = it.value();
        }
        return out;
    }

    static nlohmann::json onlyTtsKeys(const nlohmann::json& object)
    {
        nlohmann::json out = nlohmann::json::object();
        if (!object.is_object())
            return out;
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            if (isTtsJsonKey(it.key()))
                out[it.key()] = it.value();
        }
        return out;
    }

    static std::string narrativeTtsObjectKey(const std::string& fieldLeaf)
    {
        if (fieldLeaf == "description")
            return "descriptionTts";
        if (fieldLeaf == "examineDetails")
            return "examineTts";
        if (fieldLeaf == "speakDetails")
            return "speakTts";
        if (fieldLeaf == "useDetails")
            return "useTts";
        return "";
    }

    nlohmann::json* resolveEditorParentObject(const std::string& parentPointer)
    {
        if (editorDocTarget == ConversationEditDoc::Conversations)
        {
            if (parentPointer.empty())
                return conversationsLoaded ? &conversationsRoot : nullptr;
            return conversationJsonAt(parentPointer);
        }
        if (editorDocTarget == ConversationEditDoc::Scenes)
        {
            if (parentPointer.empty())
                return scenesDoc.sceneJson(variableEditorSceneId);
            return sceneFieldAt(variableEditorSceneId, parentPointer);
        }
        return nullptr;
    }

    void syncActiveBufferFromSide()
    {
        variableEditorBuffer = editorShowTts ? editorTtsSideBuffer : editorTextSideBuffer;
        if (editorTextTtsMode == TextTtsPairMode::ObjectSplit)
            variableEditorKind = VariableKindJson;
        else
            variableEditorKind = VariableKindString;
        variableEditorMultiline = true;
        variableEditorCursor = static_cast<int>(variableEditorBuffer.size());
        variableEditorSelectAnchor = -1;
        variableEditorMouseSelecting = false;
        variableEditorScrollY = 0.0f;
        variableEditorError.clear();
    }

    void stashActiveBufferToSide()
    {
        if (editorShowTts)
            editorTtsSideBuffer = variableEditorBuffer;
        else
            editorTextSideBuffer = variableEditorBuffer;
    }

    void ensureGlobalDefaultVoiceLoaded()
    {
        if (editorGlobalDefaultVoiceLoaded)
            return;
        editorGlobalDefaultVoiceLoaded = true;
        editorGlobalDefaultVoice = "leo";

        const std::string configPath = pathJoin(resourceDir, "game_config.json");
        std::ifstream file(configPath.c_str());
        if (!file.is_open())
            return;

        try
        {
            nlohmann::json config;
            file >> config;
            if (config.is_object() && config.contains("tts") && config["tts"].is_object())
            {
                const std::string voice = config["tts"].value("voice", editorGlobalDefaultVoice);
                if (!voice.empty())
                    editorGlobalDefaultVoice = voice;
            }
        }
        catch (const nlohmann::json::exception&)
        {
        }
    }

    static Color colorFromJsonRgba(const nlohmann::json& node, Color fallback)
    {
        if (!node.is_array() || node.size() < 3)
            return fallback;
        Color c = fallback;
        try
        {
            c.r = static_cast<unsigned char>(std::max(0, std::min(255, node[0].get<int>())));
            c.g = static_cast<unsigned char>(std::max(0, std::min(255, node[1].get<int>())));
            c.b = static_cast<unsigned char>(std::max(0, std::min(255, node[2].get<int>())));
            if (node.size() >= 4)
                c.a = static_cast<unsigned char>(std::max(0, std::min(255, node[3].get<int>())));
            else
                c.a = 255;
        }
        catch (...)
        {
            return fallback;
        }
        return c;
    }

    void ensureTtsSyntaxThemeLoaded()
    {
        if (ttsSyntaxThemeLoaded)
            return;
        ttsSyntaxThemeLoaded = true;

        // Built-in defaults match editor_tts_theme.json.
        ttsSyntaxTheme = TtsSyntaxTheme{};

        const std::string themePath = pathJoin(resourceDir, "editor_tts_theme.json");
        std::ifstream file(themePath.c_str());
        if (!file.is_open())
        {
            TraceLog(LOG_INFO, "SCENE EDITOR: TTS theme not found (%s); using defaults", themePath.c_str());
            return;
        }

        try
        {
            nlohmann::json root;
            file >> root;
            const nlohmann::json& syntax = root.contains("ttsSyntax") && root["ttsSyntax"].is_object()
                ? root["ttsSyntax"]
                : root;
            if (!syntax.is_object())
                return;

            if (syntax.contains("default"))
                ttsSyntaxTheme.defaultColor = colorFromJsonRgba(syntax["default"], ttsSyntaxTheme.defaultColor);
            if (syntax.contains("command"))
                ttsSyntaxTheme.command = colorFromJsonRgba(syntax["command"], ttsSyntaxTheme.command);
            if (syntax.contains("voiceMarkup"))
                ttsSyntaxTheme.voiceMarkup =
                    colorFromJsonRgba(syntax["voiceMarkup"], ttsSyntaxTheme.voiceMarkup);
            if (syntax.contains("voiceDialog"))
                ttsSyntaxTheme.voiceDialog =
                    colorFromJsonRgba(syntax["voiceDialog"], ttsSyntaxTheme.voiceDialog);

            TraceLog(LOG_INFO, "SCENE EDITOR: loaded TTS syntax theme %s", themePath.c_str());
        }
        catch (const nlohmann::json::exception& ex)
        {
            TraceLog(LOG_WARNING, "SCENE EDITOR: failed to parse TTS theme: %s", ex.what());
        }
    }

    static bool isTtsCommandBodyChar(unsigned char ch)
    {
        return std::isalnum(ch) || ch == '-' || ch == '_' || ch == ' ' || ch == '.';
    }

    static bool looksLikeTtsCommandBody(const std::string& body)
    {
        if (body.empty())
            return false;
        // xAI style: pause, long-pause, sigh, …
        bool hasLetter = false;
        for (char ch : body)
        {
            const unsigned char c = static_cast<unsigned char>(ch);
            if (!isTtsCommandBodyChar(c))
                return false;
            if (std::isalpha(c))
                hasLetter = true;
        }
        return hasLetter;
    }

    static bool isVoiceOpenTagBody(const std::string& body)
    {
        std::string trimmed = body;
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
            trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
            trimmed.pop_back();
        if (trimmed.empty() || trimmed[0] == '/')
            return false;

        std::string lower;
        lower.reserve(trimmed.size());
        for (char ch : trimmed)
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

        if (lower.rfind("voice:", 0) == 0)
            return lower.size() > 6;
        // Short form: {{eve}}, {{leo}}, …
        return lower == "eve" || lower == "ara" || lower == "rex" || lower == "sal" || lower == "leo";
    }

    static bool isVoiceCloseTagBody(const std::string& body)
    {
        std::string trimmed = body;
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
            trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
            trimmed.pop_back();
        if (trimmed.empty() || trimmed[0] != '/')
            return false;

        std::string name = trimmed.substr(1);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
            name.erase(name.begin());
        for (char& ch : name)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return name == "voice" || name == "eve" || name == "ara" || name == "rex" ||
            name == "sal" || name == "leo";
    }

    void rebuildTtsHighlightColors()
    {
        ensureTtsSyntaxThemeLoaded();
        const std::string& text = variableEditorBuffer;
        if (ttsHighlightCacheSource == text &&
            ttsHighlightColors.size() == text.size())
            return;

        ttsHighlightCacheSource = text;
        ttsHighlightColors.assign(text.size(), ttsSyntaxTheme.defaultColor);
        if (text.empty())
            return;

        bool inVoiceDialog = false;
        size_t i = 0;
        while (i < text.size())
        {
            // Voice markup: {{…}}
            if (i + 1 < text.size() && text[i] == '{' && text[i + 1] == '{')
            {
                const size_t close = text.find("}}", i + 2);
                const size_t end = (close == std::string::npos) ? text.size() : close + 2;
                const size_t bodyStart = i + 2;
                const size_t bodyEnd = (close == std::string::npos) ? text.size() : close;
                const std::string body = text.substr(bodyStart, bodyEnd - bodyStart);

                for (size_t j = i; j < end; ++j)
                    ttsHighlightColors[j] = ttsSyntaxTheme.voiceMarkup;

                if (close != std::string::npos)
                {
                    if (isVoiceOpenTagBody(body))
                        inVoiceDialog = true;
                    else if (isVoiceCloseTagBody(body))
                        inVoiceDialog = false;
                }
                i = end;
                continue;
            }

            // Bracket commands: [pause], [long-pause], [sigh], …
            if (text[i] == '[')
            {
                const size_t close = text.find(']', i + 1);
                if (close != std::string::npos)
                {
                    const std::string body = text.substr(i + 1, close - (i + 1));
                    if (looksLikeTtsCommandBody(body))
                    {
                        for (size_t j = i; j <= close; ++j)
                            ttsHighlightColors[j] = ttsSyntaxTheme.command;
                        i = close + 1;
                        continue;
                    }
                }
            }

            ttsHighlightColors[i] = inVoiceDialog
                ? ttsSyntaxTheme.voiceDialog
                : ttsSyntaxTheme.defaultColor;
            ++i;
        }
    }

    Color ttsColorAtBufferIndex(int index) const
    {
        if (index < 0 || index >= static_cast<int>(ttsHighlightColors.size()))
            return ttsSyntaxTheme.defaultColor;
        return ttsHighlightColors[static_cast<size_t>(index)];
    }

    static bool colorsEqual(Color a, Color b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    std::string readTtsVoiceFromObject(const nlohmann::json& object) const
    {
        if (!object.is_object())
            return "";
        if (object.contains("ttsVoice") && object["ttsVoice"].is_string())
        {
            const std::string voice = object["ttsVoice"].get<std::string>();
            if (!voice.empty())
                return voice;
        }
        return "";
    }

    std::string editorDefaultVoiceLabel() const
    {
        // Prefer the voice on the object being edited, then parent / TTS bag, then game config.
        if (editorTextTtsEnabled && editorTextTtsMode == TextTtsPairMode::ObjectSplit)
        {
            try
            {
                const nlohmann::json ttsPart = editorShowTts
                    ? (variableEditorBuffer.empty()
                           ? nlohmann::json::object()
                           : nlohmann::json::parse(variableEditorBuffer))
                    : (editorTtsSideBuffer.empty()
                           ? nlohmann::json::object()
                           : nlohmann::json::parse(editorTtsSideBuffer));
                const std::string fromTts = readTtsVoiceFromObject(ttsPart);
                if (!fromTts.empty())
                    return fromTts;
            }
            catch (const nlohmann::json::exception&)
            {
            }
        }

        if (editorDocTarget == ConversationEditDoc::Conversations ||
            editorDocTarget == ConversationEditDoc::Scenes)
        {
            std::string parentPointer;
            std::string leaf;
            if (splitJsonPointer(editorJsonPointer, parentPointer, leaf))
            {
                // Mutable helper used as const via const_cast pattern avoided —
                // resolve parent through document reads only.
                const nlohmann::json* parent = nullptr;
                if (editorDocTarget == ConversationEditDoc::Conversations)
                {
                    if (parentPointer.empty())
                        parent = conversationsLoaded ? &conversationsRoot : nullptr;
                    else
                        parent = conversationJsonAt(parentPointer);
                }
                else
                {
                    if (parentPointer.empty())
                        parent = scenesDoc.sceneJson(variableEditorSceneId);
                    else
                        parent = sceneFieldAt(variableEditorSceneId, parentPointer);
                }

                if (parent != nullptr && parent->is_object())
                {
                    if (editorTextTtsMode == TextTtsPairMode::StringWithTtsObject &&
                        !editorTtsObjectKey.empty() &&
                        parent->contains(editorTtsObjectKey) &&
                        (*parent)[editorTtsObjectKey].is_object())
                    {
                        const std::string fromBag =
                            readTtsVoiceFromObject((*parent)[editorTtsObjectKey]);
                        if (!fromBag.empty())
                            return fromBag;
                    }

                    const std::string fromParent = readTtsVoiceFromObject(*parent);
                    if (!fromParent.empty())
                        return fromParent;
                }
            }

            // Direct object at pointer (milestone / choice).
            const nlohmann::json* direct = nullptr;
            if (editorDocTarget == ConversationEditDoc::Conversations)
                direct = conversationJsonAt(editorJsonPointer);
            else
                direct = sceneFieldAt(variableEditorSceneId, editorJsonPointer);
            if (direct != nullptr && direct->is_object())
            {
                const std::string fromDirect = readTtsVoiceFromObject(*direct);
                if (!fromDirect.empty())
                    return fromDirect;
            }
        }

        return editorGlobalDefaultVoice;
    }

    void toggleTextTtsSide()
    {
        if (!editorTextTtsEnabled)
            return;
        stashActiveBufferToSide();
        editorShowTts = !editorShowTts;
        syncActiveBufferFromSide();
    }

    void setupTextTtsForOpenedValue(const nlohmann::json& value)
    {
        editorTextTtsEnabled = false;
        editorShowTts = false;
        editorTextSideBuffer.clear();
        editorTtsSideBuffer.clear();
        editorTextTtsMode = TextTtsPairMode::None;
        editorTtsObjectKey.clear();

        // Only conversation-tab editors get the text/TTS switch.
        if (editorDocTarget != ConversationEditDoc::Conversations &&
            editorDocTarget != ConversationEditDoc::Scenes)
            return;

        if (value.is_object())
        {
            editorTextTtsMode = TextTtsPairMode::ObjectSplit;
            editorTextSideBuffer = stripTtsKeys(value).dump(2);
            editorTtsSideBuffer = onlyTtsKeys(value).dump(2);
            editorTextTtsEnabled = true;
            editorShowTts = false;
            syncActiveBufferFromSide();
            return;
        }

        if (!value.is_string() && !value.is_null())
            return;

        std::string parentPointer;
        std::string leaf;
        if (!splitJsonPointer(editorJsonPointer, parentPointer, leaf))
            return;

        editorTextSideBuffer = value.is_string() ? value.get<std::string>() : "";

        const std::string ttsObjKey = narrativeTtsObjectKey(leaf);
        nlohmann::json* parent = resolveEditorParentObject(parentPointer);

        if (!ttsObjKey.empty() && parent != nullptr && parent->is_object())
        {
            editorTextTtsMode = TextTtsPairMode::StringWithTtsObject;
            editorTtsObjectKey = ttsObjKey;
            if (parent->contains(ttsObjKey) && (*parent)[ttsObjKey].is_object() &&
                (*parent)[ttsObjKey].contains("ttsText") &&
                (*parent)[ttsObjKey]["ttsText"].is_string())
            {
                editorTtsSideBuffer = (*parent)[ttsObjKey]["ttsText"].get<std::string>();
            }
            else if (parent->contains("ttsText") && (*parent)["ttsText"].is_string())
            {
                // Fall back to flat ttsText on same object if nested bag missing.
                editorTtsSideBuffer = (*parent)["ttsText"].get<std::string>();
            }
            editorTextTtsEnabled = true;
            editorShowTts = false;
            syncActiveBufferFromSide();
            return;
        }

        // Sibling ttsText on parent (conversation intro/response/text, etc.)
        if (parent != nullptr && parent->is_object())
        {
            editorTextTtsMode = TextTtsPairMode::StringWithSiblingTtsText;
            if (parent->contains("ttsText") && (*parent)["ttsText"].is_string())
                editorTtsSideBuffer = (*parent)["ttsText"].get<std::string>();
            editorTextTtsEnabled = true;
            editorShowTts = false;
            syncActiveBufferFromSide();
        }
    }

    bool applyTextTtsSidesToDocument()
    {
        stashActiveBufferToSide();

        if (editorTextTtsMode == TextTtsPairMode::ObjectSplit)
        {
            nlohmann::json* target = nullptr;
            if (editorDocTarget == ConversationEditDoc::Conversations)
                target = conversationJsonAt(editorJsonPointer);
            else
                target = sceneFieldAt(variableEditorSceneId, editorJsonPointer);
            if (target == nullptr || !target->is_object())
                return false;

            nlohmann::json textPart;
            nlohmann::json ttsPart;
            try
            {
                textPart = editorTextSideBuffer.empty()
                    ? nlohmann::json::object()
                    : nlohmann::json::parse(editorTextSideBuffer);
                ttsPart = editorTtsSideBuffer.empty()
                    ? nlohmann::json::object()
                    : nlohmann::json::parse(editorTtsSideBuffer);
            }
            catch (const nlohmann::json::exception&)
            {
                return false;
            }
            if (!textPart.is_object() || !ttsPart.is_object())
                return false;

            // Drop previous TTS keys, then merge both sides.
            nlohmann::json merged = stripTtsKeys(*target);
            for (auto it = textPart.begin(); it != textPart.end(); ++it)
            {
                if (!isTtsJsonKey(it.key()))
                    merged[it.key()] = it.value();
            }
            // Remove TTS keys not present in the TTS side (allows clearing).
            for (auto it = merged.begin(); it != merged.end();)
            {
                if (isTtsJsonKey(it.key()))
                    it = merged.erase(it);
                else
                    ++it;
            }
            for (auto it = ttsPart.begin(); it != ttsPart.end(); ++it)
                merged[it.key()] = it.value();

            *target = merged;
            return true;
        }

        if (editorTextTtsMode == TextTtsPairMode::StringWithTtsObject ||
            editorTextTtsMode == TextTtsPairMode::StringWithSiblingTtsText)
        {
            nlohmann::json* textTarget = nullptr;
            if (editorDocTarget == ConversationEditDoc::Conversations)
                textTarget = conversationJsonAt(editorJsonPointer);
            else
                textTarget = sceneFieldAt(variableEditorSceneId, editorJsonPointer);
            if (textTarget == nullptr)
                return false;
            *textTarget = editorTextSideBuffer;

            std::string parentPointer;
            std::string leaf;
            if (!splitJsonPointer(editorJsonPointer, parentPointer, leaf))
                return false;
            nlohmann::json* parent = resolveEditorParentObject(parentPointer);
            if (parent == nullptr || !parent->is_object())
                return false;

            if (editorTextTtsMode == TextTtsPairMode::StringWithTtsObject &&
                !editorTtsObjectKey.empty())
            {
                if (!parent->contains(editorTtsObjectKey) || !(*parent)[editorTtsObjectKey].is_object())
                    (*parent)[editorTtsObjectKey] = nlohmann::json::object();
                nlohmann::json& bag = (*parent)[editorTtsObjectKey];
                bag["ttsText"] = editorTtsSideBuffer;
                bag["tts"] = !editorTtsSideBuffer.empty();
                if (!bag.contains("ttsVoice") || !bag["ttsVoice"].is_string())
                    bag["ttsVoice"] = "";
            }
            else
            {
                (*parent)["ttsText"] = editorTtsSideBuffer;
                (*parent)["tts"] = !editorTtsSideBuffer.empty();
            }
            return true;
        }

        return false;
    }

    nlohmann::json* sceneFieldAt(const std::string& sceneId, const std::string& pointerUnderScene)
    {
        nlohmann::json* scene = scenesDoc.sceneJson(sceneId);
        if (scene == nullptr || pointerUnderScene.empty())
            return nullptr;
        try
        {
            return &scene->at(nlohmann::json::json_pointer(pointerUnderScene));
        }
        catch (const nlohmann::json::exception&)
        {
            return nullptr;
        }
    }

    const nlohmann::json* sceneFieldAt(const std::string& sceneId, const std::string& pointerUnderScene) const
    {
        const nlohmann::json* scene = scenesDoc.sceneJson(sceneId);
        if (scene == nullptr || pointerUnderScene.empty())
            return nullptr;
        try
        {
            return &scene->at(nlohmann::json::json_pointer(pointerUnderScene));
        }
        catch (const nlohmann::json::exception&)
        {
            return nullptr;
        }
    }

    void openConversationNodeEditor(const ConversationTreeNode& node)
    {
        if (node.editDoc == ConversationEditDoc::None || node.jsonPointer.empty())
            return;

        const nlohmann::json* value = nullptr;
        if (node.editDoc == ConversationEditDoc::Conversations)
            value = conversationJsonAt(node.jsonPointer);
        else if (node.editDoc == ConversationEditDoc::Scenes)
            value = sceneFieldAt(node.editSceneId, node.jsonPointer);

        if (value == nullptr)
        {
            TraceLog(
                LOG_WARNING,
                "SCENE EDITOR: edit path missing %s",
                node.jsonPointer.c_str());
            return;
        }

        editorDocTarget = node.editDoc;
        editorJsonPointer = node.jsonPointer;
        variableEditorSceneId = node.editSceneId;
        variableEditorKey = node.label;
        variableEditorScrollY = 0.0f;
        variableEditorError.clear();
        selectedConversationKey = node.key;
        TraceLog(LOG_INFO, "SCENE EDITOR: editing %s", node.jsonPointer.c_str());

        if (value->is_string())
        {
            variableEditorKind = VariableKindString;
            variableEditorBuffer = value->get<std::string>();
            variableEditorMultiline = true;
        }
        else if (value->is_boolean())
        {
            variableEditorKind = VariableKindBool;
            variableEditorBuffer = value->get<bool>() ? "true" : "false";
            variableEditorMultiline = false;
        }
        else if (value->is_number_integer())
        {
            variableEditorKind = VariableKindInteger;
            variableEditorBuffer = std::to_string(value->get<long long>());
            variableEditorMultiline = false;
        }
        else if (value->is_number_float())
        {
            variableEditorKind = VariableKindFloat;
            std::ostringstream stream;
            stream << value->get<double>();
            variableEditorBuffer = stream.str();
            variableEditorMultiline = false;
        }
        else if (value->is_null())
        {
            variableEditorKind = VariableKindString;
            variableEditorBuffer.clear();
            variableEditorMultiline = true;
        }
        else
        {
            variableEditorKind = VariableKindJson;
            variableEditorBuffer = value->dump(2);
            variableEditorMultiline = true;
        }

        ensureGlobalDefaultVoiceLoaded();
        ensureTtsSyntaxThemeLoaded();
        setupTextTtsForOpenedValue(*value);

        if (!editorTextTtsEnabled)
        {
            variableEditorCursor = static_cast<int>(variableEditorBuffer.size());
            variableEditorSelectAnchor = -1;
            variableEditorMouseSelecting = false;
        }
        variableEditorOpen = true;
        variableEditorIgnoreInputFrames = 1;
    }

    bool variableHasSelection() const
    {
        return variableEditorSelectAnchor >= 0 &&
            variableEditorSelectAnchor != variableEditorCursor;
    }

    void variableSelectionRange(int& outStart, int& outEnd) const
    {
        outStart = std::min(variableEditorSelectAnchor, variableEditorCursor);
        outEnd = std::max(variableEditorSelectAnchor, variableEditorCursor);
        if (outStart < 0)
            outStart = 0;
        if (outEnd < 0)
            outEnd = 0;
        if (outEnd > static_cast<int>(variableEditorBuffer.size()))
            outEnd = static_cast<int>(variableEditorBuffer.size());
        if (outStart > outEnd)
            outStart = outEnd;
    }

    void clearVariableSelection()
    {
        variableEditorSelectAnchor = -1;
    }

    bool deleteVariableSelection()
    {
        if (!variableHasSelection())
            return false;
        int start = 0;
        int end = 0;
        variableSelectionRange(start, end);
        variableEditorBuffer.erase(static_cast<size_t>(start), static_cast<size_t>(end - start));
        variableEditorCursor = start;
        clearVariableSelection();
        return true;
    }

    void setVariableCursor(int pos, bool extendSelection)
    {
        clampVariableCursor();
        if (pos < 0)
            pos = 0;
        if (pos > static_cast<int>(variableEditorBuffer.size()))
            pos = static_cast<int>(variableEditorBuffer.size());

        if (extendSelection)
        {
            if (variableEditorSelectAnchor < 0)
                variableEditorSelectAnchor = variableEditorCursor;
        }
        else
        {
            clearVariableSelection();
        }
        variableEditorCursor = pos;
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
        editorDocTarget = ConversationEditDoc::None;
        editorJsonPointer.clear();
        editorTextTtsEnabled = false;
        editorShowTts = false;
        editorTextTtsMode = TextTtsPairMode::None;
        editorTextSideBuffer.clear();
        editorTtsSideBuffer.clear();
        editorTtsObjectKey.clear();
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
        variableEditorSelectAnchor = -1;
        variableEditorMouseSelecting = false;
        variableEditorOpen = true;
        // One frame only — long enough to ignore the activating click, not laggy.
        variableEditorIgnoreInputFrames = 1;
    }

    bool applyEditorBufferToJson(nlohmann::json& value)
    {
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
        return true;
    }

    bool saveVariableEditor()
    {
        if (editorDocTarget == ConversationEditDoc::Conversations)
        {
            if (editorTextTtsEnabled)
            {
                if (!applyTextTtsSidesToDocument())
                    return false;
            }
            else
            {
                nlohmann::json* target = conversationJsonAt(editorJsonPointer);
                if (target == nullptr)
                    return false;
                if (!applyEditorBufferToJson(*target))
                    return false;
            }

            markDirty();
            if (!saveConversationsDocument())
            {
                variableEditorError = "Applied in memory, but failed to write conversations.json";
                return false;
            }
            dirty = false;
            rebuildConversationTree();
            closeVariableEditor();
            return true;
        }

        if (editorDocTarget == ConversationEditDoc::Scenes)
        {
            if (editorTextTtsEnabled)
            {
                if (!applyTextTtsSidesToDocument())
                    return false;
            }
            else
            {
                nlohmann::json* target = sceneFieldAt(variableEditorSceneId, editorJsonPointer);
                if (target == nullptr)
                    return false;
                if (!applyEditorBufferToJson(*target))
                    return false;
            }

            markDirty();
            if (!scenesDoc.save())
            {
                variableEditorError = "Applied in memory, but failed to write scenes.json";
                return false;
            }
            dirty = false;
            rebuildConversationTree();
            closeVariableEditor();
            return true;
        }

        nlohmann::json* scene = scenesDoc.sceneJson(variableEditorSceneId);
        if (scene == nullptr)
            return false;

        nlohmann::json& value = (*scene)[variableEditorKey];
        if (!applyEditorBufferToJson(value))
            return false;

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

    void drawEditorLineText(
        const EditorVisualLine& line,
        float x,
        float y,
        float fontSize,
        bool highlightTts) const
    {
        if (line.text.empty())
            return;

        if (!highlightTts)
        {
            DrawTextEx(
                textFont(),
                line.text.c_str(),
                {x, y},
                fontSize,
                1.0f,
                kTextPrimary);
            return;
        }

        float drawX = x;
        size_t i = 0;
        while (i < line.text.size())
        {
            const int bufIdx = line.start + static_cast<int>(i);
            const Color runColor = ttsColorAtBufferIndex(bufIdx);
            size_t j = i + 1;
            while (j < line.text.size() &&
                   colorsEqual(ttsColorAtBufferIndex(line.start + static_cast<int>(j)), runColor))
            {
                ++j;
            }

            const std::string run = line.text.substr(i, j - i);
            DrawTextEx(
                textFont(),
                run.c_str(),
                {drawX, y},
                fontSize,
                1.0f,
                runColor);
            drawX += measureUiTextWidth(run, fontSize);
            i = j;
        }
    }

    void clampVariableCursor()
    {
        if (variableEditorCursor < 0)
            variableEditorCursor = 0;
        if (variableEditorCursor > static_cast<int>(variableEditorBuffer.size()))
            variableEditorCursor = static_cast<int>(variableEditorBuffer.size());
    }

    int utf8PrevIndex(int cursor) const
    {
        if (cursor <= 0)
            return 0;
        int at = cursor - 1;
        while (at > 0 &&
               (static_cast<unsigned char>(variableEditorBuffer[static_cast<size_t>(at)]) & 0xC0) == 0x80)
        {
            --at;
        }
        return at;
    }

    int utf8NextIndex(int cursor) const
    {
        if (cursor >= static_cast<int>(variableEditorBuffer.size()))
            return static_cast<int>(variableEditorBuffer.size());
        int at = cursor + 1;
        while (at < static_cast<int>(variableEditorBuffer.size()) &&
               (static_cast<unsigned char>(variableEditorBuffer[static_cast<size_t>(at)]) & 0xC0) == 0x80)
        {
            ++at;
        }
        return at;
    }

    const std::vector<EditorVisualLine>& buildEditorVisualLines(float maxTextWidth, float fontSize) const
    {
        if (visualLinesCacheBuffer == variableEditorBuffer &&
            visualLinesCacheMaxW == maxTextWidth &&
            visualLinesCacheFontSize == fontSize &&
            !visualLinesCache.empty())
        {
            return visualLinesCache;
        }

        visualLinesCache.clear();
        visualLinesCacheBuffer = variableEditorBuffer;
        visualLinesCacheMaxW = maxTextWidth;
        visualLinesCacheFontSize = fontSize;

        const std::string& buffer = variableEditorBuffer;
        int i = 0;
        const int n = static_cast<int>(buffer.size());

        if (n == 0)
        {
            EditorVisualLine empty;
            empty.start = 0;
            empty.end = 0;
            visualLinesCache.push_back(empty);
            return visualLinesCache;
        }

        while (i < n)
        {
            EditorVisualLine line;
            line.start = i;
            std::string text;
            float lineWidth = 0.0f;

            if (buffer[static_cast<size_t>(i)] == '\n')
            {
                line.end = i; // caret sits before the newline
                line.text.clear();
                visualLinesCache.push_back(line);
                ++i;
                continue;
            }

            while (i < n && buffer[static_cast<size_t>(i)] != '\n')
            {
                const unsigned char ch =
                    static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
                const float cw = measureUiCharWidth(ch, fontSize);
                if (!text.empty() && lineWidth + cw > maxTextWidth)
                    break;
                text.push_back(buffer[static_cast<size_t>(i)]);
                lineWidth += cw;
                ++i;
            }

            line.end = i;
            line.text = text;
            visualLinesCache.push_back(line);

            if (i < n && buffer[static_cast<size_t>(i)] == '\n')
                ++i;
        }

        // Trailing newline produces an extra empty line for caret placement.
        if (!buffer.empty() && buffer[buffer.size() - 1] == '\n')
        {
            EditorVisualLine empty;
            empty.start = n;
            empty.end = n;
            visualLinesCache.push_back(empty);
        }

        return visualLinesCache;
    }

    int editorLineIndexForCursor(const std::vector<EditorVisualLine>& lines, int cursor) const
    {
        if (lines.empty())
            return 0;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const int nextStart = (i + 1 < lines.size())
                ? lines[i + 1].start
                : (static_cast<int>(variableEditorBuffer.size()) + 1);
            if (cursor >= lines[i].start && cursor < nextStart)
                return static_cast<int>(i);
        }
        return static_cast<int>(lines.size()) - 1;
    }

    float editorCaretXOnLine(
        const EditorVisualLine& line,
        int cursor,
        float fontSize) const
    {
        const int local = std::max(0, std::min(cursor, line.end) - line.start);
        const std::string before = line.text.substr(
            0,
            static_cast<size_t>(std::min(local, static_cast<int>(line.text.size()))));
        return measureUiTextWidth(before, fontSize);
    }

    int editorCursorFromClick(
        const std::vector<EditorVisualLine>& lines,
        Rectangle field,
        float pad,
        float fontSize,
        float lineHeight,
        Vector2 mouse) const
    {
        if (lines.empty())
            return 0;

        const float relY = (mouse.y - (field.y + pad) + variableEditorScrollY) / lineHeight;
        int lineIndex = static_cast<int>(std::floor(relY));
        if (lineIndex < 0)
            lineIndex = 0;
        if (lineIndex >= static_cast<int>(lines.size()))
            lineIndex = static_cast<int>(lines.size()) - 1;

        const EditorVisualLine& line = lines[static_cast<size_t>(lineIndex)];
        const float relX = mouse.x - (field.x + pad);
        if (relX <= 0.0f)
            return line.start;

        int best = line.start;
        float bestDist = relX;
        for (int pos = line.start; pos <= line.end; ++pos)
        {
            // Skip placing mid-UTF-8 sequence.
            if (pos > line.start && pos < line.end &&
                (static_cast<unsigned char>(variableEditorBuffer[static_cast<size_t>(pos)]) & 0xC0) == 0x80)
            {
                continue;
            }
            const float x = editorCaretXOnLine(line, pos, fontSize);
            const float dist = std::fabs(x - relX);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = pos;
            }
        }
        return best;
    }

    void ensureCursorVisible(
        const std::vector<EditorVisualLine>& lines,
        float fieldHeight,
        float pad,
        float lineHeight)
    {
        if (lines.empty())
            return;
        const int lineIndex = editorLineIndexForCursor(lines, variableEditorCursor);
        const float caretTop = static_cast<float>(lineIndex) * lineHeight;
        const float caretBottom = caretTop + lineHeight;
        const float viewH = fieldHeight - pad * 2.0f;
        if (caretTop < variableEditorScrollY)
            variableEditorScrollY = caretTop;
        if (caretBottom > variableEditorScrollY + viewH)
            variableEditorScrollY = caretBottom - viewH;
        if (variableEditorScrollY < 0.0f)
            variableEditorScrollY = 0.0f;
    }

    bool editorNavKeyTriggered(int key)
    {
        // Use IsKeyDown so arrows work on macOS. Fire immediately, then repeat quickly.
        if (!IsKeyDown(key))
        {
            if (variableKeyRepeatKey == key)
            {
                variableKeyRepeatKey = 0;
                variableKeyRepeatTimer = 0.0f;
            }
            return false;
        }

        if (variableKeyRepeatKey != key)
        {
            variableKeyRepeatKey = key;
            variableKeyRepeatTimer = 0.0f;
            return true;
        }

        variableKeyRepeatTimer += GetFrameTime();
        // Short initial delay, then fast repeat.
        const float initialDelay = 0.18f;
        const float repeatEvery = 0.03f;
        if (variableKeyRepeatTimer < initialDelay)
            return false;
        if (variableKeyRepeatTimer >= initialDelay + repeatEvery)
        {
            variableKeyRepeatTimer = initialDelay;
            return true;
        }
        return false;
    }

    int cursorOnLineAtPreferX(const EditorVisualLine& line, float preferX, float fontSize) const
    {
        int best = line.start;
        float bestDist = 1.0e9f;
        for (int pos = line.start; pos <= line.end; ++pos)
        {
            if (pos > line.start && pos < line.end &&
                (static_cast<unsigned char>(variableEditorBuffer[static_cast<size_t>(pos)]) & 0xC0) == 0x80)
            {
                continue;
            }
            const float x = editorCaretXOnLine(line, pos, fontSize);
            const float dist = std::fabs(x - preferX);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = pos;
            }
        }
        return best;
    }

    void moveVariableCursorVertical(int direction, const std::vector<EditorVisualLine>& lines, float fontSize)
    {
        if (lines.empty())
            return;

        const int lineIndex = editorLineIndexForCursor(lines, variableEditorCursor);
        const EditorVisualLine& cur = lines[static_cast<size_t>(lineIndex)];
        variableEditorPreferX = editorCaretXOnLine(cur, variableEditorCursor, fontSize);

        const int targetLine = lineIndex + direction;
        if (targetLine < 0)
        {
            variableEditorCursor = 0;
            return;
        }
        if (targetLine >= static_cast<int>(lines.size()))
        {
            variableEditorCursor = static_cast<int>(variableEditorBuffer.size());
            return;
        }

        variableEditorCursor = cursorOnLineAtPreferX(
            lines[static_cast<size_t>(targetLine)],
            variableEditorPreferX,
            fontSize);
    }

    void handleVariableEditorTextInput()
    {
        if (!variableEditorOpen)
            return;

        if (variableEditorIgnoreInputFrames > 0)
        {
            while (GetCharPressed() > 0)
            {
            }
            // Drain key queue so the activating key cannot act later.
            while (GetKeyPressed() != 0)
            {
            }
            return;
        }

        if (variableEditorField.width <= 1.0f || variableEditorField.height <= 1.0f)
            return;

        const Rectangle field = variableEditorField;
        const float pad = variableEditorPad;
        const float fontSize = variableEditorFontSize;
        const float lineHeight = variableEditorLineHeight;
        const float maxTextW = field.width - pad * 2.0f;
        auto lines = [&]() -> const std::vector<EditorVisualLine>&
        {
            return buildEditorVisualLines(maxTextW, fontSize);
        };
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
            IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
        const Vector2 mouse = GetMousePosition();

        // Buttons take priority over the text field (handled here in update).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (CheckCollisionPointRec(mouse, variableEditorSaveBtn))
            {
                variableEditorMouseSelecting = false;
                if (!saveVariableEditor())
                {
                    if (variableEditorError.empty())
                        variableEditorError = "Could not parse value — check type and try again";
                }
                return;
            }
            if (CheckCollisionPointRec(mouse, variableEditorCancelBtn))
            {
                variableEditorMouseSelecting = false;
                closeVariableEditor();
                return;
            }
            if (editorTextTtsEnabled &&
                variableEditorTextTtsToggle.width > 1.0f &&
                CheckCollisionPointRec(mouse, variableEditorTextTtsToggle))
            {
                variableEditorMouseSelecting = false;
                toggleTextTtsSide();
                return;
            }
        }

        // Click to place caret; drag to extend selection (field only).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, field))
        {
            const int pos = editorCursorFromClick(
                lines(), field, pad, fontSize, lineHeight, mouse);
            setVariableCursor(pos, shift);
            variableEditorMouseSelecting = !shift;
            if (!shift)
                variableEditorSelectAnchor = variableEditorCursor;
            const int lineIndex = editorLineIndexForCursor(lines(), variableEditorCursor);
            variableEditorPreferX = editorCaretXOnLine(
                lines()[static_cast<size_t>(lineIndex)],
                variableEditorCursor,
                fontSize);
            ensureCursorVisible(lines(), field.height, pad, lineHeight);
        }
        else if (variableEditorMouseSelecting && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            // Only extend selection while the pointer is over the field.
            if (CheckCollisionPointRec(mouse, field))
            {
                if (variableEditorSelectAnchor < 0)
                    variableEditorSelectAnchor = variableEditorCursor;
                variableEditorCursor = editorCursorFromClick(
                    lines(), field, pad, fontSize, lineHeight, mouse);
                clampVariableCursor();
                const int lineIndex = editorLineIndexForCursor(lines(), variableEditorCursor);
                variableEditorPreferX = editorCaretXOnLine(
                    lines()[static_cast<size_t>(lineIndex)],
                    variableEditorCursor,
                    fontSize);
                ensureCursorVisible(lines(), field.height, pad, lineHeight);
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            variableEditorMouseSelecting = false;

        // Copy / cut / paste / select-all
        if (ctrl && IsKeyPressed(KEY_A))
        {
            variableEditorSelectAnchor = 0;
            variableEditorCursor = static_cast<int>(variableEditorBuffer.size());
        }
        if (ctrl && IsKeyPressed(KEY_C) && variableHasSelection())
        {
            int start = 0;
            int end = 0;
            variableSelectionRange(start, end);
            SetClipboardText(variableEditorBuffer.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)).c_str());
        }
        if (ctrl && IsKeyPressed(KEY_X) && variableHasSelection())
        {
            int start = 0;
            int end = 0;
            variableSelectionRange(start, end);
            SetClipboardText(variableEditorBuffer.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)).c_str());
            deleteVariableSelection();
        }
        if (ctrl && IsKeyPressed(KEY_V))
        {
            const char* clip = GetClipboardText();
            if (clip != nullptr && clip[0] != '\0')
            {
                deleteVariableSelection();
                clampVariableCursor();
                const std::string paste(clip);
                variableEditorBuffer.insert(static_cast<size_t>(variableEditorCursor), paste);
                variableEditorCursor += static_cast<int>(paste.size());
                clearVariableSelection();
            }
        }

        // Text insertion (replaces selection if present)
        int codepoint = GetCharPressed();
        while (codepoint > 0)
        {
            if (codepoint >= 32 && codepoint != 127)
            {
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

                deleteVariableSelection();
                clampVariableCursor();
                variableEditorBuffer.insert(static_cast<size_t>(variableEditorCursor), encoded);
                variableEditorCursor += static_cast<int>(encoded.size());
                clearVariableSelection();
            }
            codepoint = GetCharPressed();
        }

        // Arrow navigation (shift extends selection)
        if (editorNavKeyTriggered(KEY_LEFT))
            setVariableCursor(utf8PrevIndex(variableEditorCursor), shift);
        if (editorNavKeyTriggered(KEY_RIGHT))
            setVariableCursor(utf8NextIndex(variableEditorCursor), shift);
        if (editorNavKeyTriggered(KEY_UP))
        {
            if (shift && variableEditorSelectAnchor < 0)
                variableEditorSelectAnchor = variableEditorCursor;
            moveVariableCursorVertical(-1, lines(), fontSize);
            if (!shift)
                clearVariableSelection();
        }
        if (editorNavKeyTriggered(KEY_DOWN))
        {
            if (shift && variableEditorSelectAnchor < 0)
                variableEditorSelectAnchor = variableEditorCursor;
            moveVariableCursorVertical(1, lines(), fontSize);
            if (!shift)
                clearVariableSelection();
        }

        if (IsKeyPressed(KEY_HOME))
        {
            if (ctrl || !variableEditorMultiline)
                setVariableCursor(0, shift);
            else
            {
                const int lineIndex = editorLineIndexForCursor(lines(), variableEditorCursor);
                setVariableCursor(lines()[static_cast<size_t>(lineIndex)].start, shift);
            }
        }

        if (IsKeyPressed(KEY_END))
        {
            if (ctrl || !variableEditorMultiline)
                setVariableCursor(static_cast<int>(variableEditorBuffer.size()), shift);
            else
            {
                const int lineIndex = editorLineIndexForCursor(lines(), variableEditorCursor);
                setVariableCursor(lines()[static_cast<size_t>(lineIndex)].end, shift);
            }
        }

        if (editorNavKeyTriggered(KEY_BACKSPACE))
        {
            if (!deleteVariableSelection() &&
                variableEditorCursor > 0 && !variableEditorBuffer.empty())
            {
                const int eraseAt = utf8PrevIndex(variableEditorCursor);
                variableEditorBuffer.erase(
                    static_cast<size_t>(eraseAt),
                    static_cast<size_t>(variableEditorCursor - eraseAt));
                variableEditorCursor = eraseAt;
                clearVariableSelection();
            }
        }

        if (editorNavKeyTriggered(KEY_DELETE))
        {
            if (!deleteVariableSelection() &&
                variableEditorCursor < static_cast<int>(variableEditorBuffer.size()))
            {
                const int eraseEnd = utf8NextIndex(variableEditorCursor);
                variableEditorBuffer.erase(
                    static_cast<size_t>(variableEditorCursor),
                    static_cast<size_t>(eraseEnd - variableEditorCursor));
                clearVariableSelection();
            }
        }

        if (variableEditorMultiline && editorNavKeyTriggered(KEY_ENTER))
        {
            deleteVariableSelection();
            clampVariableCursor();
            variableEditorBuffer.insert(static_cast<size_t>(variableEditorCursor), "\n");
            ++variableEditorCursor;
            clearVariableSelection();
        }

        if (IsKeyPressed(KEY_ESCAPE))
        {
            closeVariableEditor();
            return;
        }

        clampVariableCursor();
        ensureCursorVisible(lines(), field.height, pad, lineHeight);
    }

    void drawVariableEditor(int screenWidth, int screenHeight)
    {
        if (!variableEditorOpen)
            return;

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

        std::string title = "Edit \"" + variableEditorKey + "\"";
        if (editorDocTarget == ConversationEditDoc::Conversations)
            title = "Edit conversation  —  " + variableEditorKey;
        else if (editorDocTarget == ConversationEditDoc::Scenes)
            title = "Edit narrative  —  " + variableEditorKey + "  (" + variableEditorSceneId + ")";
        else if (!variableEditorSceneId.empty())
            title = "Edit \"" + variableEditorKey + "\"  —  scene: " + variableEditorSceneId;
        if (editorTextTtsEnabled)
            title += editorShowTts ? "  [TTS]" : "  [text]";
        DrawTextEx(
            textFont(),
            title.c_str(),
            {dialog.x + 18.0f, dialog.y + 14.0f},
            kFontTitle,
            1.0f,
            kTextPrimary);

        // Upper-right: default voice for this line (or game_config tts.voice).
        if (editorDocTarget == ConversationEditDoc::Conversations ||
            editorDocTarget == ConversationEditDoc::Scenes)
        {
            const std::string voiceLine = "default voice: " + editorDefaultVoiceLabel();
            const Vector2 voiceSize = MeasureTextEx(textFont(), voiceLine.c_str(), kFontBody, 1.0f);
            DrawTextEx(
                textFont(),
                voiceLine.c_str(),
                {dialog.x + dialogW - voiceSize.x - 18.0f, dialog.y + 16.0f},
                kFontBody,
                1.0f,
                kPanelBorder);
        }

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

        // Publish field metrics so update() can process click/arrow input.
        variableEditorField = field;
        variableEditorFontSize = kFontBody;
        variableEditorLineHeight = variableEditorFontSize + 4.0f;
        variableEditorPad = 8.0f;

        const float fontSize = variableEditorFontSize;
        const float lineHeight = variableEditorLineHeight;
        const float pad = variableEditorPad;
        const float maxTextW = field.width - pad * 2.0f;

        const std::vector<EditorVisualLine>& lines = buildEditorVisualLines(maxTextW, fontSize);
        const float contentH = static_cast<float>(lines.size()) * lineHeight;
        const float maxScroll = std::max(0.0f, contentH - (field.height - pad * 2.0f));
        if (CheckCollisionPointRec(GetMousePosition(), field))
            variableEditorScrollY -= GetMouseWheelMove() * lineHeight;
        if (variableEditorScrollY < 0.0f)
            variableEditorScrollY = 0.0f;
        if (variableEditorScrollY > maxScroll)
            variableEditorScrollY = maxScroll;

        const bool highlightTts = editorShowTts && editorTextTtsEnabled;
        if (highlightTts)
            rebuildTtsHighlightColors();

        const bool caretOn = (static_cast<int>(GetTime() * 2.0) % 2) == 0;
        clampVariableCursor();
        const int caret = variableEditorCursor;
        const int caretLine = editorLineIndexForCursor(lines, caret);
        int selStart = 0;
        int selEnd = 0;
        const bool hasSel = variableHasSelection();
        if (hasSel)
            variableSelectionRange(selStart, selEnd);

        BeginScissorMode(
            static_cast<int>(field.x),
            static_cast<int>(field.y),
            static_cast<int>(field.width),
            static_cast<int>(field.height));

        float y = field.y + pad - variableEditorScrollY;
        if (!variableEditorMultiline)
            y = field.y + (field.height - fontSize) * 0.5f;

        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            const EditorVisualLine& line = lines[lineIndex];

            // Selection highlight for this visual line.
            if (hasSel)
            {
                const int lineSelStart = std::max(selStart, line.start);
                const int lineSelEnd = std::min(selEnd, line.end);
                if (lineSelStart < lineSelEnd)
                {
                    const float x0 = field.x + pad + editorCaretXOnLine(line, lineSelStart, fontSize);
                    const float x1 = field.x + pad + editorCaretXOnLine(line, lineSelEnd, fontSize);
                    DrawRectangleRec(
                        {x0, y, std::max(2.0f, x1 - x0), fontSize + 2.0f},
                        Color{70, 90, 140, 180});
                }
            }

            drawEditorLineText(line, field.x + pad, y, fontSize, highlightTts);

            if (caretOn && !hasSel && static_cast<int>(lineIndex) == caretLine)
            {
                const float caretX =
                    field.x + pad + editorCaretXOnLine(line, caret, fontSize);
                DrawLineEx({caretX, y}, {caretX, y + fontSize}, 1.5f, kTextPrimary);
            }
            else if (caretOn && hasSel && static_cast<int>(lineIndex) == caretLine)
            {
                // Still show caret at active end of the selection.
                const float caretX =
                    field.x + pad + editorCaretXOnLine(line, caret, fontSize);
                DrawLineEx({caretX, y}, {caretX, y + fontSize}, 1.5f, kTextPrimary);
            }

            y += lineHeight;
            if (!variableEditorMultiline)
                break;
        }

        EndScissorMode();

        const Rectangle saveBtn = {dialog.x + dialogW - btnW * 2.0f - 28.0f, btnY, btnW, btnH};
        const Rectangle cancelBtn = {dialog.x + dialogW - btnW - 18.0f, btnY, btnW, btnH};

        auto drawButton = [&](Rectangle bounds, const char* label, bool accent)
        {
            DrawRectangleRec(bounds, accent ? kPanelAccent : Color{44, 42, 52, 255});
            DrawRectangleLinesEx(bounds, 1.0f, kPanelBorder);
            const Vector2 size = MeasureTextEx(textFont(), label, kFontBody, 1.0f);
            DrawTextEx(
                textFont(),
                label,
                {bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + 9.0f},
                kFontBody,
                1.0f,
                kTextPrimary);
        };

        // Keep rects identical to update() hit-testing.
        variableEditorSaveBtn = saveBtn;
        variableEditorCancelBtn = cancelBtn;

        // Lower-left text/TTS switch (conversation dialog editors only).
        if (editorTextTtsEnabled)
        {
            const float toggleW = 56.0f;
            const float toggleH = 22.0f;
            const float trackX = dialog.x + 18.0f; // align with dialog content left edge
            const Rectangle track = {
                trackX,
                btnY + (btnH - toggleH) * 0.5f,
                toggleW,
                toggleH};
            variableEditorTextTtsToggle = {
                track.x,
                btnY,
                track.width,
                btnH};

            // Track: left half = text, right half = TTS (knob position matches).
            DrawRectangleRounded(track, 0.5f, 6, Color{44, 42, 52, 255});
            DrawRectangleLinesEx(track, 1.0f, kPanelBorder);
            if (editorShowTts)
            {
                // Highlight right half when TTS is active.
                DrawRectangleRec(
                    {track.x + track.width * 0.5f, track.y + 1.0f,
                     track.width * 0.5f - 1.0f, track.height - 2.0f},
                    kPanelAccent);
            }
            else
            {
                // Highlight left half when text is active.
                DrawRectangleRec(
                    {track.x + 1.0f, track.y + 1.0f,
                     track.width * 0.5f - 1.0f, track.height - 2.0f},
                    kPanelAccent);
            }

            const float knobSize = toggleH - 6.0f;
            // Left = text, right = TTS
            const float knobX = editorShowTts
                ? (track.x + track.width - knobSize - 3.0f)
                : (track.x + 3.0f);
            DrawRectangleRounded(
                {knobX, track.y + 3.0f, knobSize, knobSize},
                0.5f,
                6,
                kTextPrimary);

            const char* sideLabel = editorShowTts ? "TTS" : "text";
            const Vector2 sideSize = MeasureTextEx(textFont(), sideLabel, kFontTiny, 1.0f);
            DrawTextEx(
                textFont(),
                sideLabel,
                {track.x + track.width + 8.0f, track.y + (track.height - sideSize.y) * 0.5f},
                kFontTiny,
                1.0f,
                kPanelBorder);
        }
        else
        {
            variableEditorTextTtsToggle = {0, 0, 0, 0};
        }

        drawButton(saveBtn, "Save", true);
        drawButton(cancelBtn, "Cancel", false);

        if (!variableEditorError.empty())
        {
            DrawTextEx(
                textFont(),
                variableEditorError.c_str(),
                {dialog.x + 18.0f, btnY - 22.0f},
                kFontTiny,
                1.0f,
                Color{220, 120, 100, 255});
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
                   kFontLabel, 1.0f, kTextMuted);
        if (!sceneId.empty())
        {
            DrawTextEx(
                textFont(),
                sceneId.c_str(),
                {paneBounds.x + 150.0f, paneBounds.y + 10.0f},
                kFontTiny,
                1.0f,
                kPanelBorder);
        }
        DrawTextEx(
            textFont(),
            "Click a row to edit",
            {paneBounds.x + 12.0f, paneBounds.y + paneBounds.height - 18.0f},
            kFontTiny,
            1.0f,
            kTextMuted);

        const Rectangle editBtn = {
            paneBounds.x + paneBounds.width - 72.0f,
            paneBounds.y + 6.0f,
            60.0f,
            20.0f};
        DrawRectangleRec(editBtn, kPanelAccent);
        DrawRectangleLinesEx(editBtn, 1.0f, kPanelBorder);
        DrawTextEx(textFont(), "Edit", {editBtn.x + 16.0f, editBtn.y + 3.0f}, kFontTiny, 1.0f, kTextPrimary);

        if (sceneId.empty() || !scenesDoc.hasScene(sceneId))
        {
            DrawTextEx(textFont(), "Select a scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
                       kFontBody, 1.0f, kTextMuted);
            return;
        }

        const std::vector<std::pair<std::string, std::string>> rows =
            scenesDoc.sceneVariableRows(sceneId);
        if (rows.empty())
        {
            DrawTextEx(textFont(), "No variables on this scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
                       kFontBody, 1.0f, kTextMuted);
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
                       kFontSmall, 1.0f, kTextPrimary);

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
                   kFontLabel, 1.0f, kTextMuted);

        if (selectedSceneId.empty() || !scenesDoc.hasScene(selectedSceneId))
        {
            DrawTextEx(textFont(), "Select a scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
                       kFontBody, 1.0f, kTextMuted);
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
                       kFontSmall, 1.0f, kTextMuted);
            y += rowHeight;
        }
        else
        {
            for (const SceneActor& actor : actors)
            {
                const std::string line = actor.id + " — " + actor.name +
                    (actor.role.empty() ? "" : " (" + actor.role + ")");
                DrawTextEx(textFont(), line.c_str(), {paneBounds.x + 12.0f, y},
                           kFontSmall, 1.0f, kTextPrimary);
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
        std::string pathLabel = "Resources: " + resourceDir;
        if (isConversationsTab() && conversationsLoaded)
            pathLabel = conversationsPath;
        else if (scenesDoc.isLoaded())
            pathLabel = scenesDoc.path();
        DrawTextEx(textFont(), pathLabel.c_str(), {8.0f, static_cast<float>(screenHeight) - 18.0f},
                   kFontTiny, 1.0f, kTextMuted);
        DrawTextEx(textFont(), status.c_str(),
                   {static_cast<float>(screenWidth) - 70.0f, static_cast<float>(screenHeight) - 18.0f},
                   kFontTiny, 1.0f, dirty ? Color{200, 140, 80, 255} : kTextMuted);
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

        // Conversation tree input early (same layout math as draw; before heavy work).
        if (isConversationsTab() && !variableEditorOpen && !stackDialogOpen)
        {
            const Rectangle left = leftPaneBounds(screenWidth);
            const Rectangle listBounds = {
                left.x,
                left.y + kTabHeight + 4.0f,
                left.width,
                left.height - kTabHeight - 8.0f};
            handleConversationTreeInput(listBounds);
        }

        if (variableEditorOpen)
        {
            if (variableEditorIgnoreInputFrames > 0)
                --variableEditorIgnoreInputFrames;

            // Keep dialog layout metrics in sync so buttons and field hit-tests match.
            const float dialogW = variableEditorMultiline ? 760.0f : 520.0f;
            const float dialogH = variableEditorMultiline ? 520.0f : 190.0f;
            const float dialogX = (static_cast<float>(screenWidth) - dialogW) * 0.5f;
            const float dialogY = (static_cast<float>(screenHeight) - dialogH) * 0.5f;
            const float btnH = 34.0f;
            const float btnW = 110.0f;
            const float btnY = dialogY + dialogH - btnH - 16.0f;
            variableEditorField = {
                dialogX + 18.0f,
                dialogY + 44.0f,
                dialogW - 36.0f,
                btnY - (dialogY + 44.0f) - 14.0f};
            variableEditorSaveBtn = {
                dialogX + dialogW - btnW * 2.0f - 28.0f,
                btnY,
                btnW,
                btnH};
            variableEditorCancelBtn = {
                dialogX + dialogW - btnW - 18.0f,
                btnY,
                btnW,
                btnH};
            variableEditorFontSize = kFontBody;
            variableEditorLineHeight = 20.0f;
            variableEditorPad = 8.0f;

            handleVariableEditorTextInput();
            if (CheckCollisionPointRec(GetMousePosition(), variableEditorField))
                SetMouseCursor(MOUSE_CURSOR_IBEAM);
            else
                SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }
        else if (!stackDialogOpen)
        {
            handleDividerInput(screenWidth, screenHeight);
        }
        else
        {
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }

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
        if (isConversationsTab())
            drawConversationTree(listBounds);
        else
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
    if (app.dirty)
        app.saveDocument();
    CloseWindow();
    return 0;
}