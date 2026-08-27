/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_MAP_CANVAS_H
#define TIMBERLINE_SCENE_MAP_CANVAS_H

#include "DocumentWorkspace.h"
#include "EditorLayout.h"
#include "EditorTheme.h"
#include "EditorTypes.h"
#include "SceneGraphModel.h"
#include "ThumbnailCache.h"
#include "VariableEditor.h"
#include "ConversationTree.h"
#include "DialogWalkthrough.h"
#include "ItemEditor.h"
#include "SceneAuthoringDialog.h"
#include "SceneAssistDialog.h"
#include "SceneInventoryDialog.h"
#include "SceneEffectsDialog.h"

#include <functional>
#include <string>
#include <vector>
#include <raylib.h>

namespace timberline_editor
{

using timberline_engine::SceneLayout;


struct SceneMapCanvas
{
    struct SceneCardMetrics
    {
        float width = kSceneCardWidth;
        float height = kSceneCardMinHeight;
        float thumbHeight = kSceneCardThumbHeight;
        std::vector<std::string> titleLines;
    };

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

    Vector2 scroll{0.0f, 0.0f};
    bool draggingHScroll = false;
    bool draggingVScroll = false;
    float hScrollGrabOffset = 0.0f;
    float vScrollGrabOffset = 0.0f;
    DragSource dragSource = DragSource::None;
    std::string dragSceneId;
    Vector2 dragOffset{0.0f, 0.0f};
    int level = 0;
    float listScroll = 0.0f;

    // Bottom-right scene preview + music/ambient transport.
    bool previewAudioReady = false;
    Music previewMusic{};
    bool previewMusicLoaded = false;
    std::string previewMusicPath;
    std::string previewMusicTempFile;
    bool previewMusicPlaying = false;
    Music previewAmbient{};
    bool previewAmbientLoaded = false;
    std::string previewAmbientPath;
    std::string previewAmbientTempFile;
    bool previewAmbientPlaying = false;
    std::string previewBoundSceneId;
    Texture2D previewLargeTexture{};
    bool previewLargeLoaded = false;
    std::string previewLargePath;
    std::string previewLargeTempFile;

    // Same-level exit links drawn on the map (cached for hit-testing / drag).
    struct SceneLinkRoute
    {
        std::vector<Vector2> points;
        bool arrowAtStart = false;
        bool arrowAtEnd = true;
        bool semicircleAtStart = false;
        std::string fromSide;
        std::string fromId;
        std::string toId;
        std::string direction;
        bool reciprocal = false;
    };
    std::vector<SceneLinkRoute> cachedLinkRoutes;
    int linkDragIndex = -1; // index into cachedLinkRoutes while dragging
    std::string linkDragHoverTarget;
    static constexpr float kLinkHitSlop = 10.0f;

    DocumentWorkspace* docs = nullptr;
    SceneGraphModel* graph = nullptr;
    ThumbnailCache* thumbnails = nullptr;
    EditorLayout* layout = nullptr;
    VariableEditor* variableEditor = nullptr;
    ConversationTree* conversation = nullptr;
    DialogWalkthrough* dialogWalkthrough = nullptr;
    ItemEditor* itemEditor = nullptr;
    SceneAuthoringDialog sceneAuthoring;
    SceneAssistDialog sceneAssist;
    SceneInventoryDialog sceneInventory;
    SceneEffectsDialog sceneEffects;
    std::string* selectionSceneId = nullptr;
    float* variablesScroll = nullptr;
    std::function<void()> requestReload;
    std::function<void(const std::string&)> selectSceneForEditor;
    std::function<bool()> saveDocument;
    Font uiFont{};
    Font uiFontBold{};


Vector2 sceneCardScreenPos(const SceneLayout& sceneLayout, Rectangle canvasBounds) const;

std::vector<std::string> wrapTextToWidth(
    const std::string& text,
    float maxWidth,
    float fontSize) const;

SceneCardMetrics measureSceneCard(const std::string& sceneId) const;

float maxSceneCardHeightOnLevel(int level) const;

Rectangle sceneCardBounds(const std::string& sceneId, Rectangle canvasBounds) const;

bool segmentIntersectsRect(Vector2 a, Vector2 b, Rectangle rect, float pad) const;

bool pathHitsObstacle(
    const std::vector<Vector2>& points,
    const std::vector<Rectangle>& obstacles) const;

Vector2 cardPort(Rectangle card, const std::string& side) const;

Vector2 sideOutwardNormal(const std::string& side) const;

std::string facingSide(Rectangle from, Rectangle to) const;

std::string oppositeSide(const std::string& side) const;

bool isOppositeReciprocal(
    const std::string& fromId,
    const std::string& direction,
    const std::string& toId) const;

void drawArrowHead(Vector2 tip, Vector2 fromDir) const;

void drawSourceEndCap(Vector2 edgePoint, const std::string& fromSide) const;

bool isHorizontalSeg(Vector2 a, Vector2 b) const;

bool isVerticalSeg(Vector2 a, Vector2 b) const;

bool findOrthogonalCrossing(
    Vector2 a1,
    Vector2 a2,
    Vector2 b1,
    Vector2 b2,
    Vector2& outCross) const;

void drawWireLine(Vector2 a, Vector2 b, float thick, Color color) const;

void drawWireHop(Vector2 center, bool hopIsOnHorizontal, Vector2 travelDir) const;

void drawOrthogonalSegWithHops(
    Vector2 a,
    Vector2 b,
    std::vector<Vector2> hops) const;

void drawPolyline(
    const std::vector<Vector2>& points,
    bool arrowAtStart,
    bool arrowAtEnd,
    bool semicircleAtStart,
    const std::string& fromSide,
    const std::vector<std::vector<Vector2> >& hopsPerSegment,
    Color wireColor = Color{0, 0, 0, 0}) const;

std::vector<Vector2> buildOrthogonalRoute(
    Rectangle fromCard,
    Rectangle toCard,
    const std::string& exitDir,
    const std::vector<Rectangle>& obstacles) const;

void rebuildLinkRoutes(Rectangle canvasBounds);
void drawExitArrows(Rectangle canvasBounds);
void drawLinkDragPreview(Rectangle canvasBounds) const;

float distancePointToSegment(Vector2 p, Vector2 a, Vector2 b) const;
float distancePointToPolyline(Vector2 p, const std::vector<Vector2>& points) const;
int hitTestLinkRoute(Vector2 mouse) const;
std::string sceneCardAtPoint(Vector2 mouse, Rectangle canvasBounds) const;
bool isValidLinkDropTarget(const SceneLinkRoute& route, const std::string& newToId) const;
void cancelLinkDrag();

void drawStairIcons(Rectangle canvasBounds);

void drawLevelChrome(Rectangle canvasBounds);

CanvasContentBounds contentBoundsForLevel(int level) const;

void clampCanvasScrollForCanvas(Rectangle canvasBounds, Rectangle contentView, const CanvasContentBounds& content);

void drawCanvasScrollBars(
    Rectangle canvasBounds,
    Rectangle contentView,
    const CanvasContentBounds& content,
    bool showH,
    bool showV);

void drawCanvas(Rectangle canvasBounds);

void drawStackDialog(int screenWidth, int screenHeight);

void drawSceneList(Rectangle listBounds);

void drawPanel(Rectangle bounds) const;

void drawDivider(Rectangle bounds, bool active, bool vertical) const;

void drawTabs(Rectangle leftBounds);

void drawScenePreviewPane(Rectangle paneBounds);
void syncScenePreviewMedia();
void stopScenePreviewAudio();
void unloadScenePreviewMedia();
bool loadScenePreviewMusic(
    const std::string& relPath,
    Music& outMusic,
    std::string& outTempFile);
bool loadScenePreviewTexture(const std::string& relPath);

void drawBottomPane(Rectangle bottomBounds);

void drawDividers(int screenWidth, int screenHeight) const;

void drawStatusBar(int screenWidth, int screenHeight);

std::string truncate(const std::string& text, size_t maxLen) const;
    void draw();
};

} // namespace timberline_editor
#endif
