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

#include "SceneMapCanvas.h"

#include "ConversationHelpers.h"
#include "DocumentWorkspace.h"
#include "EditorPaths.h"
#include "EditorTheme.h"
#include "EditorTypes.h"
#include "EditorUiDraw.h"
#include "ImageCompression.h"
#include "PlatformPath.h"
#include "RaylibCompat.h"
#include "SceneDocument.h"
#include "ThumbnailCache.h"

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

using timberline_engine::SceneActor;
using timberline_engine::SceneDocument;
using timberline_engine::SceneLayout;
using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::listDirectoryFileNames;
using timberline_engine::loadTextureFromAssetFile;
using timberline_engine::pathJoin;

namespace fs = std::filesystem;

namespace timberline_editor
{

Vector2 SceneMapCanvas::sceneCardScreenPos(const SceneLayout& sceneLayout, Rectangle canvasBounds) const
{
    return {
        canvasBounds.x + sceneLayout.x + scroll.x,
        canvasBounds.y + sceneLayout.y + scroll.y
    };
}


std::vector<std::string> SceneMapCanvas::wrapTextToWidth(
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
            MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), candidate.c_str(), fontSize, 1.0f).x > maxWidth)
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
        while (MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), line.c_str(), fontSize, 1.0f).x > maxWidth &&
               line.size() > 1)
        {
            size_t cut = line.size();
            while (cut > 1 &&
                   MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), line.substr(0, cut).c_str(), fontSize, 1.0f).x >
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


SceneMapCanvas::SceneCardMetrics SceneMapCanvas::measureSceneCard(const std::string& sceneId) const
{
    SceneMapCanvas::SceneCardMetrics metrics;
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


float SceneMapCanvas::maxSceneCardHeightOnLevel(int level) const
{
    float maxH = kSceneCardMinHeight;
    const std::vector<std::string> ids = graph->scenesOnLevel(level);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        const float h = measureSceneCard(ids[i]).height;
        if (h > maxH)
            maxH = h;
    }
    return maxH;
}


Rectangle SceneMapCanvas::sceneCardBounds(const std::string& sceneId, Rectangle canvasBounds) const
{
    const SceneLayout sceneLayout = docs->scenes.getLayout(sceneId);
    const Vector2 pos = sceneCardScreenPos(sceneLayout, canvasBounds);
    const SceneMapCanvas::SceneCardMetrics metrics = measureSceneCard(sceneId);
    return {pos.x, pos.y, metrics.width, metrics.height};
}


bool SceneMapCanvas::segmentIntersectsRect(Vector2 a, Vector2 b, Rectangle rect, float pad) const
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


bool SceneMapCanvas::pathHitsObstacle(
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


Vector2 SceneMapCanvas::cardPort(Rectangle card, const std::string& side) const
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


Vector2 SceneMapCanvas::sideOutwardNormal(const std::string& side) const
{
    if (side == "left")
        return {-1.0f, 0.0f};
    if (side == "right")
        return {1.0f, 0.0f};
    if (side == "top")
        return {0.0f, -1.0f};
    return {0.0f, 1.0f};
}


std::string SceneMapCanvas::facingSide(Rectangle from, Rectangle to) const
{
    const float dx = (to.x + to.width * 0.5f) - (from.x + from.width * 0.5f);
    const float dy = (to.y + to.height * 0.5f) - (from.y + from.height * 0.5f);
    if (std::fabs(dx) >= std::fabs(dy))
        return dx >= 0.0f ? "right" : "left";
    return dy >= 0.0f ? "bottom" : "top";
}


std::string SceneMapCanvas::oppositeSide(const std::string& side) const
{
    if (side == "left")
        return "right";
    if (side == "right")
        return "left";
    if (side == "top")
        return "bottom";
    return "top";
}


bool SceneMapCanvas::isOppositeReciprocal(
    const std::string& fromId,
    const std::string& direction,
    const std::string& toId) const
{
    const std::string reverseDir = graph->oppositeDirection(direction);
    if (reverseDir.empty())
        return false;
    return graph->getExitTarget(toId, reverseDir) == fromId;
}


void SceneMapCanvas::drawArrowHead(Vector2 tip, Vector2 fromDir) const
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


void SceneMapCanvas::drawSourceEndCap(Vector2 edgePoint, const std::string& fromSide) const
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


bool SceneMapCanvas::isHorizontalSeg(Vector2 a, Vector2 b) const
{
    return std::fabs(a.y - b.y) < 0.75f && std::fabs(a.x - b.x) > 0.75f;
}


bool SceneMapCanvas::isVerticalSeg(Vector2 a, Vector2 b) const
{
    return std::fabs(a.x - b.x) < 0.75f && std::fabs(a.y - b.y) > 0.75f;
}


bool SceneMapCanvas::findOrthogonalCrossing(
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


void SceneMapCanvas::drawWireLine(Vector2 a, Vector2 b, float thick, Color color) const
{
    DrawLineEx(a, b, thick, color);
}


void SceneMapCanvas::drawWireHop(Vector2 center, bool hopIsOnHorizontal, Vector2 travelDir) const
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


void SceneMapCanvas::drawOrthogonalSegWithHops(
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


void SceneMapCanvas::drawPolyline(
    const std::vector<Vector2>& points,
    bool arrowAtStart,
    bool arrowAtEnd,
    bool semicircleAtStart,
    const std::string& fromSide,
    const std::vector<std::vector<Vector2> >& hopsPerSegment,
    Color wireColor) const
{
    if (points.size() < 2)
        return;

    const Color lineColor = (wireColor.a != 0) ? wireColor : kExitArrow;

    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        std::vector<Vector2> hops;
        if (i < hopsPerSegment.size())
            hops = hopsPerSegment[i];
        // drawOrthogonalSegWithHops uses kExitArrow internally; for highlight redraw
        // simple thick segments without hops when custom color is set.
        if (wireColor.a != 0)
        {
            drawWireLine(points[i], points[i + 1], 4.0f, Color{8, 7, 12, 220});
            drawWireLine(points[i], points[i + 1], 2.5f, lineColor);
        }
        else
        {
            drawOrthogonalSegWithHops(points[i], points[i + 1], hops);
        }
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


std::vector<Vector2> SceneMapCanvas::buildOrthogonalRoute(
    Rectangle fromCard,
    Rectangle toCard,
    const std::string& exitDir,
    const std::vector<Rectangle>& obstacles) const
{
    std::string fromSide = "right";
    std::string toSide = "left";
    int dCol = 0;
    int dRow = 0;
    if (graph->directionDelta(exitDir, dCol, dRow))
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


void SceneMapCanvas::rebuildLinkRoutes(Rectangle canvasBounds)
{
    cachedLinkRoutes.clear();
    if (!docs || !graph || !docs->scenes.isLoaded())
        return;

    const std::vector<std::string> levelIds = graph->scenesOnLevel(level);
    std::vector<Rectangle> allCards;
    allCards.reserve(levelIds.size());
    for (const std::string& id : levelIds)
        allCards.push_back(sceneCardBounds(id, canvasBounds));

    for (size_t i = 0; i < levelIds.size(); ++i)
    {
        const std::string& fromId = levelIds[i];
        const char* dirs[] = {"forward", "backward", "left", "right"};
        for (size_t d = 0; d < 4; ++d)
        {
            const std::string direction = dirs[d];
            const std::string toId = graph->getExitTarget(fromId, direction);
            if (toId.empty() || !graph->isSameLevelLink(fromId, toId))
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

            SceneLinkRoute route;
            route.points = buildOrthogonalRoute(fromCard, toCard, direction, obstacles);
            route.arrowAtStart = reciprocalOpposite;
            route.arrowAtEnd = true;
            route.semicircleAtStart = !reciprocalOpposite;
            route.fromId = fromId;
            route.toId = toId;
            route.direction = direction;
            route.reciprocal = reciprocalOpposite;

            int dCol = 0;
            int dRow = 0;
            if (graph->directionDelta(direction, dCol, dRow))
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

            cachedLinkRoutes.push_back(route);
        }
    }
}


float SceneMapCanvas::distancePointToSegment(Vector2 p, Vector2 a, Vector2 b) const
{
    const Vector2 ab = Vector2Subtract(b, a);
    const float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 0.0001f)
        return Vector2Distance(p, a);
    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    const Vector2 proj = {a.x + ab.x * t, a.y + ab.y * t};
    return Vector2Distance(p, proj);
}


float SceneMapCanvas::distancePointToPolyline(Vector2 p, const std::vector<Vector2>& points) const
{
    if (points.size() < 2)
        return 1.0e9f;
    float best = 1.0e9f;
    for (size_t i = 0; i + 1 < points.size(); ++i)
        best = std::min(best, distancePointToSegment(p, points[i], points[i + 1]));
    return best;
}


int SceneMapCanvas::hitTestLinkRoute(Vector2 mouse) const
{
    int best = -1;
    float bestDist = kLinkHitSlop;
    for (size_t i = 0; i < cachedLinkRoutes.size(); ++i)
    {
        const SceneLinkRoute& route = cachedLinkRoutes[i];
        if (route.points.size() < 2)
            continue;

        float dist = distancePointToPolyline(mouse, route.points);
        // Prefer end caps (circle / arrows) slightly.
        dist = std::min(dist, Vector2Distance(mouse, route.points.front()) - 2.0f);
        dist = std::min(dist, Vector2Distance(mouse, route.points.back()) - 2.0f);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = static_cast<int>(i);
        }
    }
    return best;
}


std::string SceneMapCanvas::sceneCardAtPoint(Vector2 mouse, Rectangle canvasBounds) const
{
    if (!docs || !docs->scenes.isLoaded())
        return "";
    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        const SceneLayout sceneLayout = docs->scenes.getLayout(id);
        if (sceneLayout.level != level)
            continue;
        if (CheckCollisionPointRec(mouse, sceneCardBounds(id, canvasBounds)))
            return id;
    }
    return "";
}


bool SceneMapCanvas::isValidLinkDropTarget(const SceneLinkRoute& route, const std::string& newToId) const
{
    if (!graph || newToId.empty() || newToId == route.fromId)
        return false;
    if (!graph->isSameLevelLink(route.fromId, newToId))
        return false;
    if (newToId == route.toId)
        return true;
    if (graph->exitDirectionAlreadyLeadsTo(route.direction, newToId, route.fromId))
        return false;
    if (route.reciprocal)
    {
        const std::string reverseDir = graph->oppositeDirection(route.direction);
        if (!reverseDir.empty())
        {
            const std::string existingReverse = graph->getExitTarget(newToId, reverseDir);
            if (!existingReverse.empty() && existingReverse != route.fromId)
                return false;
        }
    }
    return true;
}


void SceneMapCanvas::cancelLinkDrag()
{
    linkDragIndex = -1;
    linkDragHoverTarget.clear();
    if (dragSource == DragSource::ExitLink)
    {
        dragSource = DragSource::None;
        dragSceneId.clear();
    }
}


void SceneMapCanvas::drawLinkDragPreview(Rectangle canvasBounds) const
{
    if (linkDragIndex < 0 || linkDragIndex >= static_cast<int>(cachedLinkRoutes.size()))
        return;

    const SceneLinkRoute& route = cachedLinkRoutes[static_cast<size_t>(linkDragIndex)];
    if (route.points.size() < 2)
        return;

    // Dim original route while dragging.
    const Color highlight = {230, 190, 90, 255};
    std::vector<std::vector<Vector2> > noHops(
        route.points.size() > 0 ? route.points.size() - 1 : 0);
    drawPolyline(
        route.points,
        route.arrowAtStart,
        route.arrowAtEnd,
        route.semicircleAtStart,
        route.fromSide,
        noHops,
        highlight);

    const Vector2 mouse = GetMousePosition();
    const Vector2 start = route.points.front();
    DrawLineEx(start, mouse, 2.0f, highlight);
    DrawCircleV(mouse, 5.0f, highlight);

    if (!linkDragHoverTarget.empty())
    {
        const Rectangle targetCard = sceneCardBounds(linkDragHoverTarget, canvasBounds);
        const bool valid = isValidLinkDropTarget(route, linkDragHoverTarget);
        DrawRectangleLinesEx(
            targetCard,
            2.0f,
            valid ? Color{100, 200, 120, 255} : Color{210, 90, 80, 255});
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            valid ? "Drop to reconnect exit" : "Invalid target",
            {targetCard.x, targetCard.y - 18.0f},
            kFontTiny,
            1.0f,
            valid ? Color{100, 200, 120, 255} : Color{210, 90, 80, 255});
    }
}


void SceneMapCanvas::drawExitArrows(Rectangle canvasBounds)
{
    rebuildLinkRoutes(canvasBounds);
    if (cachedLinkRoutes.empty())
        return;

    // hopsPerRoute[route][seg] = hop centers on that segment
    std::vector<std::vector<std::vector<Vector2> > > hopsPerRoute(cachedLinkRoutes.size());
    for (size_t r = 0; r < cachedLinkRoutes.size(); ++r)
        hopsPerRoute[r].assign(
            cachedLinkRoutes[r].points.empty() ? 0 : cachedLinkRoutes[r].points.size() - 1,
            std::vector<Vector2>());

    for (size_t r0 = 0; r0 < cachedLinkRoutes.size(); ++r0)
    {
        const std::vector<Vector2>& p0 = cachedLinkRoutes[r0].points;
        for (size_t s0 = 0; s0 + 1 < p0.size(); ++s0)
        {
            for (size_t r1 = r0 + 1; r1 < cachedLinkRoutes.size(); ++r1)
            {
                const std::vector<Vector2>& p1 = cachedLinkRoutes[r1].points;
                for (size_t s1 = 0; s1 + 1 < p1.size(); ++s1)
                {
                    Vector2 cross;
                    if (!findOrthogonalCrossing(p0[s0], p0[s0 + 1], p1[s1], p1[s1 + 1], cross))
                        continue;

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

    for (size_t r = 0; r < cachedLinkRoutes.size(); ++r)
    {
        // Skip normal draw for the route currently being dragged (preview draws it).
        if (static_cast<int>(r) == linkDragIndex)
            continue;
        drawPolyline(
            cachedLinkRoutes[r].points,
            cachedLinkRoutes[r].arrowAtStart,
            cachedLinkRoutes[r].arrowAtEnd,
            cachedLinkRoutes[r].semicircleAtStart,
            cachedLinkRoutes[r].fromSide,
            hopsPerRoute[r]);
    }

    if (linkDragIndex >= 0)
        drawLinkDragPreview(canvasBounds);
}


void SceneMapCanvas::drawStairIcons(Rectangle canvasBounds)
{
    if (!docs->scenes.isLoaded())
        return;

    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        const SceneLayout sceneLayout = docs->scenes.getLayout(id);
        if (sceneLayout.level != level)
            continue;

        const bool hasUp = !graph->getExitTarget(id, "up").empty();
        const bool hasDown = !graph->getExitTarget(id, "down").empty();
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
            DrawTextEx((uiFontBold.texture.id != 0 ? uiFontBold : (uiFont.texture.id != 0 ? uiFont : GetFontDefault())), "^", {iconX, badge.y}, iconSize, 1.0f, kPanelBorder);
            iconX -= iconSlot;
        }
        if (hasDown)
        {
            DrawTextEx((uiFontBold.texture.id != 0 ? uiFontBold : (uiFont.texture.id != 0 ? uiFont : GetFontDefault())), "v", {iconX, badge.y}, iconSize, 1.0f, kPanelBorder);
        }
    }
}


void SceneMapCanvas::drawLevelChrome(Rectangle canvasBounds)
{
    int minLevel = 0;
    int maxLevel = 0;
    graph->getLevelRange(minLevel, maxLevel);
    const bool canGoDown = docs->scenes.isLoaded() && level > minLevel;
    const bool canGoUp = docs->scenes.isLoaded() && level < maxLevel;
    const int onLevel = docs->scenes.isLoaded() ? graph->countScenesOnLevel(level) : 0;

    const std::string levelLabel = TextFormat(
        "Floor level %d  |  range %d to %d  |  %d scene(s) here",
        level,
        minLevel,
        maxLevel,
        onLevel);
    DrawTextEx(
        (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
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
        (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
        "-",
        {levelDownBtn.x + 10.0f, levelDownBtn.y + 3.0f},
        kFontTitle,
        1.0f,
        canGoDown ? kTextPrimary : kTextDisabled);
    DrawTextEx(
        (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
        "+",
        {levelUpBtn.x + 9.0f, levelUpBtn.y + 3.0f},
        kFontTitle,
        1.0f,
        canGoUp ? kTextPrimary : kTextDisabled);

    if (!graph->stackDialogOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        const Vector2 mouse = GetMousePosition();
        if (canGoDown && CheckCollisionPointRec(mouse, levelDownBtn))
            level -= 1;
        if (canGoUp && CheckCollisionPointRec(mouse, levelUpBtn))
            level += 1;
    }
}


SceneMapCanvas::CanvasContentBounds SceneMapCanvas::contentBoundsForLevel(int level) const
{
    SceneMapCanvas::CanvasContentBounds bounds;
    const std::vector<std::string> ids = graph->scenesOnLevel(level);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        const SceneLayout sceneLayout = docs->scenes.getLayout(ids[i]);
        const SceneMapCanvas::SceneCardMetrics metrics = measureSceneCard(ids[i]);
        const float right = sceneLayout.x + metrics.width;
        const float bottom = sceneLayout.y + metrics.height;
        if (!bounds.valid)
        {
            bounds.minX = sceneLayout.x;
            bounds.minY = sceneLayout.y;
            bounds.maxX = right;
            bounds.maxY = bottom;
            bounds.valid = true;
        }
        else
        {
            bounds.minX = std::min(bounds.minX, sceneLayout.x);
            bounds.minY = std::min(bounds.minY, sceneLayout.y);
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


void SceneMapCanvas::clampCanvasScrollForCanvas(Rectangle canvasBounds, Rectangle contentView, const CanvasContentBounds& content)
{
    if (!content.valid)
    {
        scroll = {0.0f, 0.0f};
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
        scroll.x = maxScrollX;
    else
    {
        if (minScrollX > maxScrollX)
            std::swap(minScrollX, maxScrollX);
        if (scroll.x < minScrollX)
            scroll.x = minScrollX;
        if (scroll.x > maxScrollX)
            scroll.x = maxScrollX;
    }

    if (content.height() <= contentView.height)
        scroll.y = maxScrollY;
    else
    {
        if (minScrollY > maxScrollY)
            std::swap(minScrollY, maxScrollY);
        if (scroll.y < minScrollY)
            scroll.y = minScrollY;
        if (scroll.y > maxScrollY)
            scroll.y = maxScrollY;
    }
}


void SceneMapCanvas::drawCanvasScrollBars(
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
        const float t = (maxScrollX - scroll.x) / scrollRange;
        const float thumbX = track.x + t * (track.width - thumbW);
        const Rectangle thumb = {thumbX, track.y + 2.0f, thumbW, track.height - 4.0f};
        DrawRectangleRec(thumb, draggingHScroll ? kScrollThumbActive : kScrollThumb);

        const bool canDragBar =
            !graph->stackDialogOpen
            && !(layout && layout->isDraggingDivider())
            && dragSource != DragSource::ExitLink
            && dragSource != DragSource::Canvas;

        if (canDragBar)
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, thumb))
            {
                draggingHScroll = true;
                hScrollGrabOffset = mouse.x - thumb.x;
                dragSource = DragSource::None;
                dragSceneId.clear();
                cancelLinkDrag();
            }
            else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, track))
            {
                const float center = mouse.x - thumbW * 0.5f;
                const float ratio = (center - track.x) / std::max(1.0f, track.width - thumbW);
                scroll.x = maxScrollX - ratio * scrollRange;
                draggingHScroll = true;
                hScrollGrabOffset = thumbW * 0.5f;
            }
        }

        if (draggingHScroll && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            const float thumbPos = mouse.x - hScrollGrabOffset;
            const float ratio = (thumbPos - track.x) / std::max(1.0f, track.width - thumbW);
            const float clampedRatio = std::max(0.0f, std::min(1.0f, ratio));
            scroll.x = maxScrollX - clampedRatio * scrollRange;
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
        const float t = (maxScrollY - scroll.y) / scrollRange;
        const float thumbY = track.y + t * (track.height - thumbH);
        const Rectangle thumb = {track.x + 2.0f, thumbY, track.width - 4.0f, thumbH};
        DrawRectangleRec(thumb, draggingVScroll ? kScrollThumbActive : kScrollThumb);

        const bool canDragBar =
            !graph->stackDialogOpen
            && !(layout && layout->isDraggingDivider())
            && dragSource != DragSource::ExitLink
            && dragSource != DragSource::Canvas;

        if (canDragBar)
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, thumb))
            {
                draggingVScroll = true;
                vScrollGrabOffset = mouse.y - thumb.y;
                dragSource = DragSource::None;
                dragSceneId.clear();
                cancelLinkDrag();
            }
            else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, track))
            {
                const float center = mouse.y - thumbH * 0.5f;
                const float ratio = (center - track.y) / std::max(1.0f, track.height - thumbH);
                scroll.y = maxScrollY - ratio * scrollRange;
                draggingVScroll = true;
                vScrollGrabOffset = thumbH * 0.5f;
            }
        }

        if (draggingVScroll && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            const float thumbPos = mouse.y - vScrollGrabOffset;
            const float ratio = (thumbPos - track.y) / std::max(1.0f, track.height - thumbH);
            const float clampedRatio = std::max(0.0f, std::min(1.0f, ratio));
            scroll.y = maxScrollY - clampedRatio * scrollRange;
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


void SceneMapCanvas::drawCanvas(Rectangle canvasBounds)
{
    DrawRectangleRec(canvasBounds, kCanvasBg);
    drawLevelChrome(canvasBounds);

    if (!docs->scenes.isLoaded())
    {
        const std::string message = docs->loadError.empty()
            ? "Select the scenes.json or conversations.json tab."
            : docs->loadError;
        drawWrappedText(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            message,
            {canvasBounds.x + 20.0f, canvasBounds.y + 44.0f},
            canvasBounds.width - 40.0f,
            15.0f,
            5.0f,
            kTextMuted);
        return;
    }

    const SceneMapCanvas::CanvasContentBounds content = contentBoundsForLevel(level);
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
    // under (*thumbnails).
    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        const SceneLayout sceneLayout = docs->scenes.getLayout(id);
        if (sceneLayout.level != level)
            continue;

        const SceneMapCanvas::SceneCardMetrics metrics = measureSceneCard(id);
        const Rectangle card = sceneCardBounds(id, canvasBounds);
        const bool selected = id == (*selectionSceneId);
        DrawRectangleRec(card, selected ? Color{52, 46, 62, 255} : Color{36, 32, 44, 255});
        DrawRectangleLinesEx(card, selected ? 2.0f : 1.0f, selected ? kPanelBorder : kPanelAccent);

        const ThumbnailEntry& thumb = thumbnails->getOrLoad(id, docs->scenes, docs->assetRoot, docs->resourceDir);
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
                (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
                metrics.titleLines[lineIndex].c_str(),
                {card.x + 6.0f, titleY},
                kSceneCardTitleFont,
                1.0f,
                kTextPrimary);
            titleY += kSceneCardTitleLineHeight;
        }

    }

    // Build routes before hit-testing / drawing so click targets match geometry.
    rebuildLinkRoutes(canvasBounds);

    // Conversations tab: allow selecting scenes on the map so the left tree can
    // rebuild for another scene, but do not drag cards or retarget exit links.
    const bool inputFree =
        !graph->stackDialogOpen
        && !(variableEditor && variableEditor->open)
        && !(layout && layout->isDraggingDivider())
        && !draggingHScroll
        && !draggingVScroll;
    const bool conversationsTab = docs->isConversationsTab();
    const bool canSelectScene = inputFree;
    const bool canEditMapGeometry = inputFree && !conversationsTab;

    // Start exit-link drag (before card drag so ports/wires win near edges).
    if (canEditMapGeometry
        && dragSource == DragSource::None
        && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        const int hit = hitTestLinkRoute(GetMousePosition());
        if (hit >= 0)
        {
            linkDragIndex = hit;
            dragSource = DragSource::ExitLink;
            linkDragHoverTarget.clear();
        }
    }

    // Card select (all tabs that show the map). Drag only on scenes tab.
    if (canSelectScene
        && dragSource == DragSource::None
        && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        for (const std::string& id : ids)
        {
            const SceneLayout sceneLayout = docs->scenes.getLayout(id);
            if (sceneLayout.level != level)
                continue;
            const Rectangle card = sceneCardBounds(id, canvasBounds);
            if (!CheckCollisionPointRec(GetMousePosition(), card))
                continue;
            if (selectSceneForEditor)
                selectSceneForEditor(id);
            if (canEditMapGeometry)
            {
                dragSource = DragSource::Canvas;
                dragSceneId = id;
                dragOffset = {GetMouseX() - card.x, GetMouseY() - card.y};
            }
            break;
        }
    }

    // Update link drag hover / commit.
    if (dragSource == DragSource::ExitLink && linkDragIndex >= 0)
    {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            linkDragHoverTarget = sceneCardAtPoint(GetMousePosition(), canvasBounds);
            if (linkDragHoverTarget == cachedLinkRoutes[static_cast<size_t>(linkDragIndex)].fromId)
                linkDragHoverTarget.clear();
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            const SceneLinkRoute route = cachedLinkRoutes[static_cast<size_t>(linkDragIndex)];
            const std::string dropId = sceneCardAtPoint(GetMousePosition(), canvasBounds);
            if (!dropId.empty() && isValidLinkDropTarget(route, dropId))
            {
                graph->retargetExitLink(
                    route.fromId,
                    route.direction,
                    dropId,
                    route.reciprocal);
            }
            cancelLinkDrag();
        }
    }

    drawExitArrows(canvasBounds);
    drawStairIcons(canvasBounds);

    if (!graph->stackDialogOpen &&
        dragSource == DragSource::Canvas &&
        !dragSceneId.empty() &&
        IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        const SceneMapCanvas::SceneCardMetrics dragMetrics = measureSceneCard(dragSceneId);
        const Rectangle ghost = {
            static_cast<float>(GetMouseX()) - dragOffset.x,
            static_cast<float>(GetMouseY()) - dragOffset.y,
            dragMetrics.width,
            dragMetrics.height};
        DrawRectangleRec(ghost, Color{80, 70, 50, 120});
        DrawRectangleLinesEx(ghost, 1.0f, kPanelBorder);

        const std::string hoverTarget = graph->findStackTarget(ghost, canvasBounds, dragSceneId);
        if (!hoverTarget.empty())
        {
            const Rectangle targetCard = sceneCardBounds(hoverTarget, canvasBounds);
            DrawRectangleLinesEx(targetCard, 2.0f, Color{220, 180, 80, 255});
            DrawTextEx(
                (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
                "Drop for Up / Down / Cancel",
                {targetCard.x, targetCard.y - 18.0f},
                kFontTiny,
                1.0f,
                kPanelBorder);
        }
    }

    if (!graph->stackDialogOpen &&
        dragSource == DragSource::Canvas &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(GetMousePosition(), contentView) &&
            docs->scenes.hasScene(dragSceneId))
        {
            const float dropX =
                static_cast<float>(GetMouseX()) - canvasBounds.x - dragOffset.x - scroll.x;
            const float dropY =
                static_cast<float>(GetMouseY()) - canvasBounds.y - dragOffset.y - scroll.y;
            const SceneMapCanvas::SceneCardMetrics dropMetrics = measureSceneCard(dragSceneId);
            const Rectangle ghost = {
                static_cast<float>(GetMouseX()) - dragOffset.x,
                static_cast<float>(GetMouseY()) - dragOffset.y,
                dropMetrics.width,
                dropMetrics.height};
            const std::string targetId = graph->findStackTarget(ghost, canvasBounds, dragSceneId);

            if (!targetId.empty())
            {
                graph->stackDialogOpen = true;
                graph->stackSourceId = dragSceneId;
                graph->stackTargetId = targetId;
                graph->stackPendingX = dropX;
                graph->stackPendingY = dropY;
            }
            else
            {
                SceneLayout sceneLayout = docs->scenes.getLayout(dragSceneId);
                sceneLayout.x = dropX;
                sceneLayout.y = dropY;
                sceneLayout.level = level;
                docs->scenes.setLayout(dragSceneId, sceneLayout);
                (*selectionSceneId) = dragSceneId;
                docs->markDirty();
            }
        }

        dragSource = DragSource::None;
        dragSceneId.clear();
    }

    if (dragSource == DragSource::ExitLink && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)
        && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        // Safety: mouse lost while dragging.
        cancelLinkDrag();
    }

    EndScissorMode();

    drawCanvasScrollBars(canvasBounds, contentView, content, showH, showV);
    clampCanvasScrollForCanvas(canvasBounds, contentView, content);

    if (!graph->stackDialogOpen &&
        !draggingHScroll &&
        !draggingVScroll &&
        dragSource == DragSource::None &&
        CheckCollisionPointRec(GetMousePosition(), contentView))
    {
        // Prefer axis-separated wheel input when available (trackpads / horizontal
        // mouse wheels). Fall back to Shift/Ctrl + vertical wheel for X pan.
        const Vector2 wheelV = GetMouseWheelMoveV();
        float dx = wheelV.x * 32.0f;
        float dy = -wheelV.y * 32.0f;

        const bool modHorizontal =
            IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
            IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (modHorizontal && std::fabs(wheelV.x) < 0.001f && std::fabs(wheelV.y) > 0.001f)
        {
            // Map vertical wheel to horizontal when only Y is reported.
            dx = wheelV.y * 32.0f;
            dy = 0.0f;
        }

        if (std::fabs(dx) > 0.001f || std::fabs(dy) > 0.001f)
        {
            scroll.x += dx;
            scroll.y += dy;
            clampCanvasScrollForCanvas(canvasBounds, contentView, content);
        }
    }
}


void SceneMapCanvas::drawStackDialog(int screenWidth, int screenHeight)
{
    if (!graph->stackDialogOpen)
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
        (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
        "Stack scene floors",
        {dialog.x + 20.0f, dialog.y + 18.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);

    const std::string body = TextFormat(
        "Place \"%s\" relative to \"%s\"?",
        graph->stackSourceId.c_str(),
        graph->stackTargetId.c_str());
    drawWrappedText(
        (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
        body,
        {dialog.x + 20.0f, dialog.y + 52.0f},
        dialogW - 40.0f,
        kFontBody,
        4.0f,
        kTextMuted);

    DrawTextEx(
        (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
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
        const Vector2 size = MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), label, kFontBody, 1.0f);
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
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
            graph->applyStackLink(true);
            graph->closeStackDialog();
        }
        else if (CheckCollisionPointRec(mouse, downBtn))
        {
            graph->applyStackLink(false);
            graph->closeStackDialog();
        }
        else if (CheckCollisionPointRec(mouse, cancelBtn) || !CheckCollisionPointRec(mouse, dialog))
        {
            graph->closeStackDialog();
        }
    }

    if (IsKeyPressed(KEY_ESCAPE))
        graph->closeStackDialog();
}


void SceneMapCanvas::drawSceneList(Rectangle listBounds)
{
    if (!docs->scenes.isLoaded())
    {
        const std::string message = docs->loadError.empty()
            ? "Loading scenes.json..."
            : docs->loadError;
        drawWrappedText(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            message,
            {listBounds.x + 12.0f, listBounds.y + 12.0f},
            listBounds.width - 24.0f,
            kListMetaFont,
            4.0f,
            kTextMuted);
        return;
    }

    const std::vector<std::string> ids = docs->scenes.sceneIds();
    const float contentHeight = static_cast<float>(ids.size()) * kListRowHeight;
    const float maxScroll = std::max(0.0f, contentHeight - listBounds.height);
    if (listScroll > maxScroll)
        listScroll = maxScroll;

    BeginScissorMode(
        static_cast<int>(listBounds.x),
        static_cast<int>(listBounds.y),
        static_cast<int>(listBounds.width),
        static_cast<int>(listBounds.height));

    float y = listBounds.y - listScroll;
    for (const std::string& id : ids)
    {
        const Rectangle row = {listBounds.x + 4.0f, y, listBounds.width - 8.0f, kListRowHeight - 4.0f};
        const bool selected = id == (*selectionSceneId);
        if (selected)
            DrawRectangleRec(row, kSelection);

        const ThumbnailEntry& thumb = thumbnails->getOrLoad(id, docs->scenes, docs->assetRoot, docs->resourceDir);
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

        const int sceneLevel = docs->scenes.getLayout(id).level;
        const float textX = row.x + kListThumbSize + 14.0f;
        const float textY = row.y + (kListRowHeight - kListNameFont - kListMetaFont - 8.0f) * 0.5f;
        DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), id.c_str(), {textX, textY},
                   kListNameFont, 1.0f, kTextPrimary);
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
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
    if (!graph->stackDialogOpen &&
        !(variableEditor && variableEditor->open) &&
        !layout && layout->isDraggingDivider() &&
        CheckCollisionPointRec(mouse, listBounds) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        const float localY = (mouse.y - listBounds.y) + listScroll;
        if (localY >= 0.0f)
        {
            const int index = static_cast<int>(localY / kListRowHeight);
            if (index >= 0 && index < static_cast<int>(ids.size()))
            {
                const std::string& id = ids[static_cast<size_t>(index)];
                if (selectSceneForEditor) selectSceneForEditor(id);
                dragSource = DragSource::SceneList;
                dragSceneId = id;
                const float rowTop = listBounds.y - listScroll + static_cast<float>(index) * kListRowHeight;
                dragOffset = {mouse.x - listBounds.x - 4.0f, mouse.y - rowTop};
            }
        }
    }

    if (CheckCollisionPointRec(GetMousePosition(), listBounds))
        listScroll -= GetMouseWheelMove() * 24.0f;
    if (listScroll < 0.0f)
        listScroll = 0.0f;
    if (listScroll > maxScroll)
        listScroll = maxScroll;
}


void SceneMapCanvas::drawPanel(Rectangle bounds) const
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


void SceneMapCanvas::drawDivider(Rectangle bounds, bool active, bool vertical) const
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


void SceneMapCanvas::drawTabs(Rectangle leftBounds)
{
    if (docs->jsonTabs.empty())
    {
        DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), "No resource JSON files",
                   {leftBounds.x + 8.0f, leftBounds.y + 8.0f}, kListTabFont, 1.0f, kTextMuted);
        return;
    }

    const float tabWidth = leftBounds.width / static_cast<float>(std::max<size_t>(1, docs->jsonTabs.size()));
    float x = leftBounds.x;
    for (size_t i = 0; i < docs->jsonTabs.size(); ++i)
    {
        const Rectangle tab = {x, leftBounds.y, tabWidth, kTabHeight};
        const bool active = static_cast<int>(i) == docs->activeTabIndex;
        DrawRectangleRec(tab, active ? kPanelAccent : Color{40, 36, 48, 255});
        DrawRectangleLinesEx(tab, 1.0f, kPanelBorder);

        std::string label = docs->jsonTabs[i];
        if (label.size() > 5 && label.compare(label.size() - 5, 5, ".json") == 0)
            label.resize(label.size() - 5);
        const float fontSize = kListTabFont;
        const Vector2 textSize = MeasureTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), label.c_str(), fontSize, 1.0f);
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            label.c_str(),
            {tab.x + (tab.width - textSize.x) * 0.5f, tab.y + 8.0f},
            fontSize,
            1.0f,
            active ? kTextPrimary : kTextMuted);

        if (CheckCollisionPointRec(GetMousePosition(), tab) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            docs->activeTabIndex = static_cast<int>(i);
            thumbnails->clear();
            if (requestReload) requestReload();
        }

        x += tabWidth;
    }
}


void SceneMapCanvas::drawActorsPane(Rectangle paneBounds)
{
    DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), "Actors", {paneBounds.x + 12.0f, paneBounds.y + 8.0f},
               kFontLabel, 1.0f, kTextMuted);

    if ((*selectionSceneId).empty() || !docs->scenes.hasScene((*selectionSceneId)))
    {
        DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), "Select a scene", {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
                   kFontBody, 1.0f, kTextMuted);
        return;
    }

    const std::vector<SceneActor> actors = docs->scenes.getActors((*selectionSceneId));
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
        DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), "(no actors)", {paneBounds.x + 12.0f, y},
                   kFontSmall, 1.0f, kTextMuted);
        y += rowHeight;
    }
    else
    {
        for (const SceneActor& actor : actors)
        {
            const std::string line = actor.id + " — " + actor.name +
                (actor.role.empty() ? "" : " (" + actor.role + ")");
            DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), line.c_str(), {paneBounds.x + 12.0f, y},
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


void SceneMapCanvas::drawBottomPane(Rectangle bottomBounds)
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

    variableEditor->drawVariablesPane(variablesBounds);
    drawActorsPane(actorsBounds);
}


void SceneMapCanvas::drawDividers(int screenWidth, int screenHeight) const
{
    drawDivider(layout->verticalDividerBounds(screenWidth), layout->draggingVerticalDivider, true);
    drawDivider(layout->horizontalDividerBounds(screenWidth), layout->draggingHorizontalDivider, false);
}


void SceneMapCanvas::drawStatusBar(int screenWidth, int screenHeight)
{
    const std::string status = docs->dirty ? "Modified" : "Saved";
    std::string pathLabel = "Resources: " + docs->resourceDir;
    if (docs->isConversationsTab() && docs->conversationsLoaded)
        pathLabel = docs->conversationsPath;
    else if (docs->isItemsTab() && docs->itemsLoaded)
        pathLabel = docs->itemsPath;
    else if (docs->scenes.isLoaded())
        pathLabel = docs->scenes.path();
    DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), pathLabel.c_str(), {8.0f, static_cast<float>(screenHeight) - 18.0f},
               kFontTiny, 1.0f, kTextMuted);
    DrawTextEx((uiFont.texture.id != 0 ? uiFont : GetFontDefault()), status.c_str(),
               {static_cast<float>(screenWidth) - 70.0f, static_cast<float>(screenHeight) - 18.0f},
               kFontTiny, 1.0f, docs->dirty ? Color{200, 140, 80, 255} : kTextMuted);
}


std::string SceneMapCanvas::truncate(const std::string& text, size_t maxLen) const
{
    if (text.size() <= maxLen)
        return text;
    return text.substr(0, maxLen - 3) + "...";
}

void SceneMapCanvas::draw()
{
    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();
    if (layout)
        layout->syncToWindow(screenWidth, screenHeight);

    BeginDrawing();
    ClearBackground(Color{14, 13, 18, 255});

    const Rectangle left = layout->leftPaneBounds(screenWidth);
    const Rectangle main = layout->mainPaneBounds(screenWidth);
    const Rectangle bottom = layout->bottomPaneBounds(screenWidth, screenHeight);

    drawPanel(left);
    drawTabs(left);
    const Rectangle listBounds = {
        left.x,
        left.y + kTabHeight + 4.0f,
        left.width,
        left.height - kTabHeight - 8.0f};
    if (docs && docs->isConversationsTab() && conversation)
        conversation->drawConversationTree(listBounds);
    else if (docs && docs->isItemsTab() && itemEditor)
        itemEditor->draw(listBounds);
    else
        drawSceneList(listBounds);

    drawPanel(main);
    const Rectangle canvasBounds = {
        main.x + 4.0f,
        main.y + 4.0f,
        main.width - 8.0f,
        main.height - 8.0f};
    if (docs && docs->isItemsTab())
    {
        // Item tab: main pane is a detail placeholder (fields edit via left tree dialogs).
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            "Item editor",
            {canvasBounds.x + 16.0f, canvasBounds.y + 16.0f},
            kFontTitle,
            1.0f,
            kTextPrimary);
        std::string hint = itemEditor && !itemEditor->selectedItemId.empty()
            ? ("Selected: " + itemEditor->selectedItemId
               + "\nExpand an item and click a field for direct JSON edits.\n"
                 "Edit Item (or click the selected root again) opens the authoring pane.\n"
                 "New Item creates a new entry. Id is fixed when modifying.")
            : "Select an item in the list.\n"
              "New Item / Edit Item open the authoring pane (capabilities, Product Recipe, AI assist).\n"
              "Expand a tree node for field-level edits.";
        if (itemEditor && !itemEditor->lastAuthoringStatus.empty())
            hint += "\n\n" + itemEditor->lastAuthoringStatus;
        drawWrappedText(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            hint,
            {canvasBounds.x + 16.0f, canvasBounds.y + 52.0f},
            canvasBounds.width - 32.0f,
            kFontBody,
            6.0f,
            kTextMuted);
    }
    else
    {
        drawCanvas(canvasBounds);
    }

    drawBottomPane(bottom);
    drawDividers(screenWidth, screenHeight);
    drawStatusBar(screenWidth, screenHeight);
    drawStackDialog(screenWidth, screenHeight);
    if (itemEditor)
        itemEditor->drawNewItemDialog(screenWidth, screenHeight);
    if (variableEditor)
        variableEditor->drawVariableEditor(screenWidth, screenHeight);

    EndDrawing();
}

} // namespace timberline_editor
