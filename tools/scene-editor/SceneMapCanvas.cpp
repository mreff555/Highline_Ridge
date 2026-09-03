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
#include "EditorInput.h"

#include "ConversationHelpers.h"
#include "DocumentWorkspace.h"
#include "EditorButton.h"
#include "EditorPaths.h"
#include "EditorPrefs.h"
#include "EditorTheme.h"
#include "EditorTypes.h"
#include "EditorUiDraw.h"
#include "SceneAuthoring.h"
#include "SceneAuthoringDialog.h"
#include "SceneAssistDialog.h"
#include "SceneEffectsDialog.h"
#include "EditorPreferencesDialog.h"
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

namespace
{

/** One-line description preview: word-safe fit with "..." (no space before ellipsis). */
std::string fitSceneListDescriptionPreview(
    Font font,
    const std::string& raw,
    float maxWidth,
    float fontSize)
{
    if (maxWidth < 8.0f)
        return {};

    // Flatten whitespace to single spaces.
    std::string text;
    text.reserve(raw.size());
    bool prevSpace = true;
    for (unsigned char uch : raw)
    {
        char ch = static_cast<char>(uch);
        if (ch == '\n' || ch == '\r' || ch == '\t')
            ch = ' ';
        if (ch == ' ')
        {
            if (prevSpace)
                continue;
            prevSpace = true;
            text.push_back(' ');
        }
        else
        {
            prevSpace = false;
            text.push_back(ch);
        }
    }
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    if (text.empty())
        return {};

    if (measureUiTextWidth(font, text, fontSize) <= maxWidth)
        return text;

    const std::string ellipsis = "...";
    // Largest prefix length n where prefix + "..." fits.
    size_t lo = 0;
    size_t hi = text.size();
    size_t best = 0;
    while (lo <= hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        const std::string candidate = text.substr(0, mid) + ellipsis;
        if (measureUiTextWidth(font, candidate, fontSize) <= maxWidth)
        {
            best = mid;
            lo = mid + 1;
        }
        else
        {
            if (mid == 0)
                break;
            hi = mid - 1;
        }
    }
    if (best == 0)
        return ellipsis;

    // Drop a partial final word when possible.
    size_t cut = best;
    if (cut < text.size())
    {
        const size_t sp = text.find_last_of(' ', cut - 1);
        if (sp != std::string::npos && sp + 1 < cut)
            cut = sp;
    }
    while (cut > 0 && text[cut - 1] == ' ')
        --cut;
    if (cut == 0)
    {
        // Single long word: hard-cut to whatever still fits with ellipsis.
        cut = best;
        while (cut > 0
               && measureUiTextWidth(font, text.substr(0, cut) + ellipsis, fontSize) > maxWidth)
            --cut;
        if (cut == 0)
            return ellipsis;
    }
    return text.substr(0, cut) + ellipsis;
}

} // namespace

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
    // Prefer the card whose center is closest to the cursor among hits. First-match
    // on sceneIds() order made overlapping / stacked cards feel one-directional.
    std::string bestId;
    float bestDistSq = 1.0e12f;
    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        if (!docs->scenes.hasMapPlacement(id))
            continue;
        const SceneLayout sceneLayout = docs->scenes.getLayout(id);
        if (sceneLayout.level != level)
            continue;
        const Rectangle card = sceneCardBounds(id, canvasBounds);
        if (!CheckCollisionPointRec(mouse, card))
            continue;
        const float cx = card.x + card.width * 0.5f;
        const float cy = card.y + card.height * 0.5f;
        const float dx = mouse.x - cx;
        const float dy = mouse.y - cy;
        const float distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestId = id;
        }
    }
    return bestId;
}

Rectangle SceneMapCanvas::directionPortBounds(Rectangle card, const std::string& direction) const
{
    const float s = kPortHitSize;
    if (direction == "forward")
        return {card.x + (card.width - s) * 0.5f, card.y - s * 0.5f, s, s};
    if (direction == "backward")
        return {card.x + (card.width - s) * 0.5f, card.y + card.height - s * 0.5f, s, s};
    if (direction == "left")
        return {card.x - s * 0.5f, card.y + (card.height - s) * 0.5f, s, s};
    if (direction == "right")
        return {card.x + card.width - s * 0.5f, card.y + (card.height - s) * 0.5f, s, s};
    return {0, 0, 0, 0};
}

bool SceneMapCanvas::hitTestDirectionPort(
    Vector2 mouse,
    Rectangle canvasBounds,
    std::string& outSceneId,
    std::string& outDirection) const
{
    outSceneId.clear();
    outDirection.clear();
    if (!docs || !docs->scenes.isLoaded())
        return false;

    // Closest port center wins. Adjacent cards' facing ports often overlap; the
    // old first-match-by-sceneIds() order made only one facing port grabable.
    const char* dirs[] = {"forward", "backward", "left", "right"};
    float bestDistSq = 1.0e12f;
    bool found = false;
    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        if (!docs->scenes.hasMapPlacement(id))
            continue;
        if (docs->scenes.getLayout(id).level != level)
            continue;
        const Rectangle card = sceneCardBounds(id, canvasBounds);
        for (const char* dir : dirs)
        {
            const Rectangle port = directionPortBounds(card, dir);
            if (!CheckCollisionPointRec(mouse, port))
                continue;
            const float cx = port.x + port.width * 0.5f;
            const float cy = port.y + port.height * 0.5f;
            const float dx = mouse.x - cx;
            const float dy = mouse.y - cy;
            const float distSq = dx * dx + dy * dy;
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                outSceneId = id;
                outDirection = dir;
                found = true;
            }
        }
    }
    return found;
}

void SceneMapCanvas::cancelPortDrag()
{
    if (dragSource == DragSource::ExitPort)
        dragSource = DragSource::None;
    portDragFromId.clear();
    portDragDirection.clear();
    linkDragHoverTarget.clear();
}

void SceneMapCanvas::drawDirectionPorts(Rectangle canvasBounds) const
{
    if (!docs || !docs->scenes.isLoaded())
        return;

    const Vector2 mouse = GetMousePosition();
    std::string hoverScene;
    std::string hoverDir;
    const bool hoveringPort =
        dragSource == DragSource::None
        && hitTestDirectionPort(mouse, canvasBounds, hoverScene, hoverDir);

    const char* dirs[] = {"forward", "backward", "left", "right"};
    const std::vector<std::string> ids = docs->scenes.sceneIds();
    for (const std::string& id : ids)
    {
        if (!docs->scenes.hasMapPlacement(id))
            continue;
        if (docs->scenes.getLayout(id).level != level)
            continue;
        const Rectangle card = sceneCardBounds(id, canvasBounds);
        for (const char* dir : dirs)
        {
            const Rectangle port = directionPortBounds(card, dir);
            const bool linked = graph && !graph->getExitTarget(id, dir).empty();
            const bool active =
                dragSource == DragSource::ExitPort
                && portDragFromId == id
                && portDragDirection == dir;
            const bool hovered =
                hoveringPort && hoverScene == id && hoverDir == dir;
            // Empty ports used to be nearly invisible (dark on dark) — users only
            // discovered linked/gold ports or top ports by accident.
            Color fill = linked ? Color{168, 138, 72, 230} : Color{120, 112, 140, 230};
            if (hovered)
                fill = Color{200, 185, 140, 255};
            if (active)
                fill = Color{220, 190, 100, 255};
            const float radius = port.width * 0.42f;
            DrawCircleV(
                {port.x + port.width * 0.5f, port.y + port.height * 0.5f},
                radius,
                fill);
            DrawCircleLines(
                static_cast<int>(port.x + port.width * 0.5f),
                static_cast<int>(port.y + port.height * 0.5f),
                radius,
                hovered || active ? Color{240, 220, 160, 255} : kPanelBorder);
        }
    }
}

void SceneMapCanvas::drawPortDragPreview(Rectangle canvasBounds) const
{
    if (dragSource != DragSource::ExitPort || portDragFromId.empty() || portDragDirection.empty())
        return;
    if (!docs->scenes.hasMapPlacement(portDragFromId))
        return;

    const Rectangle fromCard = sceneCardBounds(portDragFromId, canvasBounds);
    const Rectangle port = directionPortBounds(fromCard, portDragDirection);
    const Vector2 start = {port.x + port.width * 0.5f, port.y + port.height * 0.5f};
    const Vector2 mouse = GetMousePosition();

    const char* invalidReason = nullptr;
    bool valid = false;
    if (linkDragHoverTarget.empty() || linkDragHoverTarget == portDragFromId)
        invalidReason = "Drop on another scene";
    else if (!graph || !graph->isSameLevelLink(portDragFromId, linkDragHoverTarget))
        invalidReason = "Different map level";
    else if (graph->exitDirectionAlreadyLeadsTo(
                 portDragDirection, linkDragHoverTarget, portDragFromId))
        invalidReason = "That direction already enters target";
    else
        valid = true;

    DrawLineEx(start, mouse, 2.0f, valid ? Color{220, 190, 100, 255} : Color{160, 80, 80, 200});
    DrawCircleV(mouse, 5.0f, valid ? Color{220, 190, 100, 255} : Color{160, 80, 80, 200});
    if (!linkDragHoverTarget.empty())
    {
        const Rectangle target = sceneCardBounds(linkDragHoverTarget, canvasBounds);
        DrawRectangleLinesEx(
            target,
            2.0f,
            valid ? Color{220, 190, 100, 255} : Color{160, 80, 80, 200});
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            valid ? "Drop to create exit (sets reciprocal if free)"
                  : (invalidReason ? invalidReason : "Invalid target"),
            {target.x, target.y - 18.0f},
            kFontTiny,
            1.0f,
            valid ? kPanelBorder : Color{200, 100, 100, 255});
    }
}

bool SceneMapCanvas::placeSceneListDrop(Vector2 mouse, Rectangle canvasBounds, Rectangle contentView)
{
    // Accept drops anywhere on the map pane (including level chrome).
    if (!CheckCollisionPointRec(mouse, canvasBounds) || dragSceneId.empty())
        return false;
    if (!docs->scenes.hasScene(dragSceneId))
        return false;
    (void)contentView;

    // dragOffset is in card-local pixels (set when the list drag starts).
    const float dropX = mouse.x - canvasBounds.x - dragOffset.x - scroll.x;
    const float dropY = mouse.y - canvasBounds.y - dragOffset.y - scroll.y;

    std::string placeId = dragSceneId;
    if (docs->scenes.hasMapPlacement(dragSceneId))
    {
        // Already on the map — clone to a new independent scene.
        placeId = docs->scenes.duplicateScene(dragSceneId);
        if (placeId.empty())
            return false;
        if (thumbnails)
            thumbnails->clear();
        TraceLog(LOG_INFO, "TIMBERLINE: duplicated scene %s → %s", dragSceneId.c_str(), placeId.c_str());
    }

    SceneLayout sceneLayout{};
    sceneLayout.x = dropX;
    sceneLayout.y = dropY;
    sceneLayout.level = level;
    docs->scenes.setLayout(placeId, sceneLayout);
    if (selectionSceneId)
        *selectionSceneId = placeId;
    if (selectSceneForEditor)
        selectSceneForEditor(placeId);
    docs->markDirty();
    TraceLog(LOG_INFO, "TIMBERLINE: placed scene '%s' on map L%d at (%.0f, %.0f)",
        placeId.c_str(),
        level,
        dropX,
        dropY);
    return true;
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
        if (!docs->scenes.hasMapPlacement(id))
            continue;
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
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    DrawTextEx(
        font,
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

    // "Clean up" sized to the label (+ pad) so it is not ellipsized.
    const char* cleanLabel = "Clean up";
    const EditorButtonConfig& btnCfg = editorButtons().config;
    const float cleanTextW = MeasureTextEx(font, cleanLabel, btnCfg.fontSize, 1.0f).x;
    const float cleanW = std::clamp(
        cleanTextW + btnCfg.padX * 2.0f + 8.0f,
        std::max(btnCfg.minWidth, 96.0f),
        btnCfg.maxWidth);
    const float cleanH = std::max(24.0f, btnCfg.minHeight);
    const Rectangle cleanBtn = {
        levelDownBtn.x - cleanW - 8.0f,
        canvasBounds.y + 8.0f,
        cleanW,
        cleanH};
    const bool canClean =
        docs->scenes.isLoaded()
        && onLevel > 0
        && !graph->stackDialogOpen
        && !(docs->isConversationsTab())
        && confirmMode == ConfirmMode::None
        && contextMenuSource == ContextMenuSource::None
        && !anyAuthoringModalOpen();

    drawEditorButton(font, cleanBtn, cleanLabel, false, canClean);
    DrawRectangleRec(levelDownBtn, canGoDown ? kPanelAccent : kButtonDisabled);
    DrawRectangleRec(levelUpBtn, canGoUp ? kPanelAccent : kButtonDisabled);
    DrawRectangleLinesEx(levelDownBtn, 1.0f, canGoDown ? kPanelBorder : kTextDisabled);
    DrawRectangleLinesEx(levelUpBtn, 1.0f, canGoUp ? kPanelBorder : kTextDisabled);
    DrawTextEx(
        font,
        "-",
        {levelDownBtn.x + 10.0f, levelDownBtn.y + 3.0f},
        kFontTitle,
        1.0f,
        canGoDown ? kTextPrimary : kTextDisabled);
    DrawTextEx(
        font,
        "+",
        {levelUpBtn.x + 9.0f, levelUpBtn.y + 3.0f},
        kFontTitle,
        1.0f,
        canGoUp ? kTextPrimary : kTextDisabled);

    if (!graph->stackDialogOpen && editorMousePressed(MOUSE_BUTTON_LEFT))
    {
        const Vector2 mouse = GetMousePosition();
        if (canClean && CheckCollisionPointRec(mouse, cleanBtn))
        {
            // Port-align linked cards (keep neighborhood; straighten mid-runs).
            graph->cleanupLayoutLevel(level);
            cancelLinkDrag();
            cancelPortDrag();
            cachedLinkRoutes.clear();
            exitLinkFeedback = "Map cleaned up";
            exitLinkFeedbackUntil = GetTime() + 2.0;
        }
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


void SceneMapCanvas::applyEdgeAutoPanWhileDragging(
    Rectangle canvasBounds,
    Rectangle contentView,
    CanvasContentBounds& content,
    float speedPxPerSec)
{
    if (speedPxPerSec <= 0.0f)
        return;
    if (draggingHScroll || draggingVScroll)
        return;
    if (!editorMouseDown(MOUSE_BUTTON_LEFT))
        return;

    const bool sceneDrag =
        (dragSource == DragSource::Canvas || dragSource == DragSource::SceneList)
        && !dragSceneId.empty();
    const bool linkDrag =
        dragSource == DragSource::ExitLink || dragSource == DragSource::ExitPort;
    if (!sceneDrag && !linkDrag)
        return;

    Rectangle ghost{};
    if (sceneDrag)
    {
        const SceneCardMetrics metrics = measureSceneCard(dragSceneId);
        ghost = {
            static_cast<float>(GetMouseX()) - dragOffset.x,
            static_cast<float>(GetMouseY()) - dragOffset.y,
            metrics.width,
            metrics.height};
    }
    else
    {
        // Port/link: probe centered on cursor using half-card margins.
        const float probeW = std::max(48.0f, kSceneCardWidth * 0.5f);
        const float probeH = std::max(48.0f, kSceneCardMinHeight * 0.5f);
        ghost = {
            static_cast<float>(GetMouseX()) - probeW * 0.5f,
            static_cast<float>(GetMouseY()) - probeH * 0.5f,
            probeW,
            probeH};
    }

    // Expand layout bounds so clamp allows panning past current content.
    const float worldLeft = ghost.x - canvasBounds.x - scroll.x;
    const float worldTop = ghost.y - canvasBounds.y - scroll.y;
    const float worldRight = worldLeft + ghost.width;
    const float worldBottom = worldTop + ghost.height;
    if (!content.valid)
    {
        content.minX = worldLeft;
        content.minY = worldTop;
        content.maxX = worldRight;
        content.maxY = worldBottom;
        content.valid = true;
    }
    else
    {
        content.minX = std::min(content.minX, worldLeft);
        content.minY = std::min(content.minY, worldTop);
        content.maxX = std::max(content.maxX, worldRight);
        content.maxY = std::max(content.maxY, worldBottom);
    }

    const float marginX = std::max(24.0f, ghost.width * 0.5f);
    const float marginY = std::max(24.0f, ghost.height * 0.5f);
    const float dt = GetFrameTime();
    const float step = speedPxPerSec * dt;

    // Overlap with edge strips inside contentView (half-card thick).
    const bool hitLeft =
        ghost.x < contentView.x + marginX && ghost.x + ghost.width > contentView.x;
    const bool hitRight =
        ghost.x + ghost.width > contentView.x + contentView.width - marginX
        && ghost.x < contentView.x + contentView.width;
    const bool hitTop =
        ghost.y < contentView.y + marginY && ghost.y + ghost.height > contentView.y;
    const bool hitBottom =
        ghost.y + ghost.height > contentView.y + contentView.height - marginY
        && ghost.y < contentView.y + contentView.height;

    // scroll↑ moves content right on screen (reveals left). scroll↓ reveals right.
    if (hitLeft)
        scroll.x += step;
    if (hitRight)
        scroll.x -= step;
    if (hitTop)
        scroll.y += step;
    if (hitBottom)
        scroll.y -= step;
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
            && dragSource != DragSource::ExitPort
            && dragSource != DragSource::Canvas
            && dragSource != DragSource::SceneList;

        if (canDragBar)
        {
            if (editorMousePressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, thumb))
            {
                draggingHScroll = true;
                hScrollGrabOffset = mouse.x - thumb.x;
                dragSource = DragSource::None;
                dragSceneId.clear();
                cancelLinkDrag();
            }
            else if (editorMousePressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, track))
            {
                const float center = mouse.x - thumbW * 0.5f;
                const float ratio = (center - track.x) / std::max(1.0f, track.width - thumbW);
                scroll.x = maxScrollX - ratio * scrollRange;
                draggingHScroll = true;
                hScrollGrabOffset = thumbW * 0.5f;
            }
        }

        if (draggingHScroll && editorMouseDown(MOUSE_BUTTON_LEFT))
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
            && dragSource != DragSource::ExitPort
            && dragSource != DragSource::Canvas
            && dragSource != DragSource::SceneList;

        if (canDragBar)
        {
            if (editorMousePressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, thumb))
            {
                draggingVScroll = true;
                vScrollGrabOffset = mouse.y - thumb.y;
                dragSource = DragSource::None;
                dragSceneId.clear();
                cancelLinkDrag();
            }
            else if (editorMousePressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, track))
            {
                const float center = mouse.y - thumbH * 0.5f;
                const float ratio = (center - track.y) / std::max(1.0f, track.height - thumbH);
                scroll.y = maxScrollY - ratio * scrollRange;
                draggingVScroll = true;
                vScrollGrabOffset = thumbH * 0.5f;
            }
        }

        if (draggingVScroll && editorMouseDown(MOUSE_BUTTON_LEFT))
        {
            const float thumbPos = mouse.y - vScrollGrabOffset;
            const float ratio = (thumbPos - track.y) / std::max(1.0f, track.height - thumbH);
            const float clampedRatio = std::max(0.0f, std::min(1.0f, ratio));
            scroll.y = maxScrollY - clampedRatio * scrollRange;
        }
    }

    if (editorMouseReleased(MOUSE_BUTTON_LEFT))
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

    SceneMapCanvas::CanvasContentBounds content = contentBoundsForLevel(level);
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

    // Edge auto-pan while dragging (prefs: mapDragPanSpeed). Expands the
    // content AABB so clamp allows panning past currently placed scenes.
    if (docs != nullptr)
    {
        applyEdgeAutoPanWhileDragging(
            canvasBounds,
            contentView,
            content,
            loadMapDragPanSpeed(docs->resourceDir));
    }
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
        if (!docs->scenes.hasMapPlacement(id))
            continue;
        const SceneLayout sceneLayout = docs->scenes.getLayout(id);
        if (sceneLayout.level != level)
            continue;

        const SceneMapCanvas::SceneCardMetrics metrics = measureSceneCard(id);
        const Rectangle card = sceneCardBounds(id, canvasBounds);
        const bool selected = selectionSceneId && id == (*selectionSceneId);
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
    // Modals (New Scene, etc.) must block map hit-testing — interaction still
    // lives in this draw path, so open dialogs would otherwise steal presses
    // from scrollbar/field drags and move cards underneath.
    const bool modalOpen =
        sceneAuthoring.blocksInput()
        || sceneAssist.blocksInput()
        || sceneInventory.blocksInput()
        || sceneEffects.blocksInput()
        || sceneTransition.blocksInput()
        || (preferences && preferences->blocksInput())
        || (variableEditor && variableEditor->open)
        || (itemEditor && itemEditor->blocksInput())
        || confirmMode != ConfirmMode::None;
    if ((modalOpen || contextMenuSource != ContextMenuSource::None)
        && dragSource != DragSource::None)
    {
        dragSource = DragSource::None;
        dragSceneId.clear();
        cancelLinkDrag();
        cancelPortDrag();
    }
    const bool inputFree =
        !graph->stackDialogOpen
        && !modalOpen
        && contextMenuSource == ContextMenuSource::None
        && !(layout && layout->isDraggingDivider())
        && !draggingHScroll
        && !draggingVScroll;
    const bool conversationsTab = docs->isConversationsTab();
    const bool canSelectScene = inputFree;
    const bool canEditMapGeometry = inputFree && !conversationsTab;

    // Start port / exit-link / card drag (ports win over wires; wires over cards).
    if (canEditMapGeometry
        && dragSource == DragSource::None
        && editorMousePressed(MOUSE_BUTTON_LEFT))
    {
        std::string portScene;
        std::string portDir;
        if (hitTestDirectionPort(GetMousePosition(), canvasBounds, portScene, portDir))
        {
            portDragFromId = portScene;
            portDragDirection = portDir;
            dragSource = DragSource::ExitPort;
            linkDragHoverTarget.clear();
            if (selectSceneForEditor)
                selectSceneForEditor(portScene);
        }
        else
        {
            const int hit = hitTestLinkRoute(GetMousePosition());
            if (hit >= 0)
            {
                linkDragIndex = hit;
                dragSource = DragSource::ExitLink;
                linkDragHoverTarget.clear();
            }
        }
    }

    // Right-click map wire / card → context menu (select only; never starts drag).
    // Only handle when the cursor is over the map — otherwise a list right-click
    // would open a menu in drawSceneList and then get closed here on miss.
    // Wires win over cards so edge connectors stay reachable.
    const bool canOpenMapContext =
        !graph->stackDialogOpen
        && !modalOpen
        && !(layout && layout->isDraggingDivider())
        && !draggingHScroll
        && !draggingVScroll
        && dragSource == DragSource::None;
    if (canOpenMapContext && editorMousePressed(MOUSE_BUTTON_RIGHT)
        && CheckCollisionPointRec(GetMousePosition(), contentView))
    {
        const Vector2 mouse = GetMousePosition();
        bool opened = false;
        // Exit-link menu is a geometry edit (delete / transition SFX).
        if (!conversationsTab)
        {
            const int linkHit = hitTestLinkRoute(mouse);
            if (linkHit >= 0)
            {
                openLinkContextMenu(linkHit, mouse);
                opened = true;
            }
        }
        if (!opened)
        {
            for (const std::string& id : ids)
            {
                if (!docs->scenes.hasMapPlacement(id))
                    continue;
                const SceneLayout sceneLayout = docs->scenes.getLayout(id);
                if (sceneLayout.level != level)
                    continue;
                const Rectangle card = sceneCardBounds(id, canvasBounds);
                if (!CheckCollisionPointRec(mouse, card))
                    continue;
                openContextMenu(ContextMenuSource::Map, id, mouse);
                opened = true;
                break;
            }
        }
        if (!opened)
            closeContextMenu();
    }

    // Card select (all tabs that show the map). Drag only on scenes tab.
    if (canSelectScene
        && dragSource == DragSource::None
        && editorMousePressed(MOUSE_BUTTON_LEFT))
    {
        for (const std::string& id : ids)
        {
            if (!docs->scenes.hasMapPlacement(id))
                continue;
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

    // Update existing-wire retarget drag.
    if (dragSource == DragSource::ExitLink && linkDragIndex >= 0)
    {
        if (editorMouseDown(MOUSE_BUTTON_LEFT))
        {
            linkDragHoverTarget = sceneCardAtPoint(GetMousePosition(), canvasBounds);
            if (linkDragHoverTarget == cachedLinkRoutes[static_cast<size_t>(linkDragIndex)].fromId)
                linkDragHoverTarget.clear();
        }

        if (editorMouseReleased(MOUSE_BUTTON_LEFT))
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

    // Update new-connector port drag.
    if (dragSource == DragSource::ExitPort && !portDragFromId.empty())
    {
        if (editorMouseDown(MOUSE_BUTTON_LEFT))
        {
            linkDragHoverTarget = sceneCardAtPoint(GetMousePosition(), canvasBounds);
            if (linkDragHoverTarget == portDragFromId)
                linkDragHoverTarget.clear();
        }

        if (editorMouseReleased(MOUSE_BUTTON_LEFT))
        {
            const std::string dropId = sceneCardAtPoint(GetMousePosition(), canvasBounds);
            if (!dropId.empty() && dropId != portDragFromId && graph)
            {
                if (graph->createExitLink(
                        portDragFromId, portDragDirection, dropId, true))
                {
                    exitLinkFeedback.clear();
                    exitLinkFeedbackUntil = 0.0;
                }
                else
                {
                    if (!graph->isSameLevelLink(portDragFromId, dropId))
                        exitLinkFeedback = "Exit not created: scenes are on different levels";
                    else if (graph->exitDirectionAlreadyLeadsTo(
                                 portDragDirection, dropId, portDragFromId))
                        exitLinkFeedback =
                            "Exit not created: another scene already uses "
                            + portDragDirection + " into that target";
                    else
                        exitLinkFeedback = "Exit not created (invalid link)";
                    exitLinkFeedbackUntil = GetTime() + 4.0;
                    TraceLog(
                        LOG_WARNING,
                        "TIMBERLINE: createExitLink %s --%s--> %s failed",
                        portDragFromId.c_str(),
                        portDragDirection.c_str(),
                        dropId.c_str());
                }
            }
            cancelPortDrag();
        }
    }

    drawExitArrows(canvasBounds);
    drawStairIcons(canvasBounds);
    if (canEditMapGeometry)
        drawDirectionPorts(canvasBounds);
    if (dragSource == DragSource::ExitPort)
        drawPortDragPreview(canvasBounds);

    if (!exitLinkFeedback.empty() && GetTime() <= exitLinkFeedbackUntil)
    {
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            exitLinkFeedback.c_str(),
            {contentView.x + 8.0f, contentView.y + contentView.height - 22.0f},
            kFontTiny,
            1.0f,
            Color{220, 120, 100, 255});
    }
    else if (GetTime() > exitLinkFeedbackUntil)
    {
        exitLinkFeedback.clear();
    }

    // Canvas card move commit (ghost is drawn AFTER EndScissorMode so list→map
    // ghosts are not clipped to the map viewport).
    if (!graph->stackDialogOpen &&
        dragSource == DragSource::Canvas &&
        editorMouseReleased(MOUSE_BUTTON_LEFT))
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
                if (selectionSceneId)
                    *selectionSceneId = dragSceneId;
                docs->markDirty();
            }
        }

        dragSource = DragSource::None;
        dragSceneId.clear();
    }

    if (!graph->stackDialogOpen &&
        dragSource == DragSource::SceneList &&
        editorMouseReleased(MOUSE_BUTTON_LEFT))
    {
        placeSceneListDrop(GetMousePosition(), canvasBounds, contentView);
        dragSource = DragSource::None;
        dragSceneId.clear();
    }

    if (dragSource == DragSource::ExitLink && !editorMouseDown(MOUSE_BUTTON_LEFT)
        && !editorMouseReleased(MOUSE_BUTTON_LEFT))
    {
        // Safety: mouse lost while dragging.
        cancelLinkDrag();
    }
    if (dragSource == DragSource::ExitPort && !editorMouseDown(MOUSE_BUTTON_LEFT)
        && !editorMouseReleased(MOUSE_BUTTON_LEFT))
    {
        cancelPortDrag();
    }

    EndScissorMode();

    // Drag ghost above the scissor so list→map drags stay visible over the UI.
    if (!graph->stackDialogOpen
        && !dragSceneId.empty()
        && editorMouseDown(MOUSE_BUTTON_LEFT)
        && (dragSource == DragSource::Canvas || dragSource == DragSource::SceneList))
    {
        const SceneMapCanvas::SceneCardMetrics dragMetrics = measureSceneCard(dragSceneId);
        const Rectangle ghost = {
            static_cast<float>(GetMouseX()) - dragOffset.x,
            static_cast<float>(GetMouseY()) - dragOffset.y,
            dragMetrics.width,
            dragMetrics.height};
        DrawRectangleRec(ghost, Color{80, 70, 50, 160});
        DrawRectangleLinesEx(ghost, 2.0f, kPanelBorder);
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            dragSceneId.c_str(),
            {ghost.x + 8.0f, ghost.y + 8.0f},
            kFontTiny,
            1.0f,
            kTextPrimary);

        if (dragSource == DragSource::Canvas)
        {
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
        else if (dragSource == DragSource::SceneList)
        {
            const char* hint = docs->scenes.hasMapPlacement(dragSceneId)
                ? "Drop on map to duplicate"
                : "Drop on map to place";
            DrawTextEx(
                (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
                hint,
                {ghost.x, ghost.y - 18.0f},
                kFontTiny,
                1.0f,
                kPanelBorder);
        }
    }

    drawCanvasScrollBars(canvasBounds, contentView, content, showH, showV);
    clampCanvasScrollForCanvas(canvasBounds, contentView, content);

    const bool modalBlocksMapScroll =
        graph->stackDialogOpen
        || sceneAuthoring.blocksInput()
        || sceneAssist.blocksInput()
        || sceneInventory.blocksInput()
        || sceneEffects.blocksInput()
        || sceneTransition.blocksInput()
        || (preferences && preferences->blocksInput())
        || (variableEditor && variableEditor->open)
        || (itemEditor && itemEditor->blocksInput())
        || confirmMode != ConfirmMode::None
        || contextMenuSource != ContextMenuSource::None;

    if (!modalBlocksMapScroll &&
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


bool SceneMapCanvas::anyAuthoringModalOpen() const
{
    return sceneAuthoring.blocksInput()
        || sceneAssist.blocksInput()
        || sceneInventory.blocksInput()
        || sceneEffects.blocksInput()
        || sceneTransition.blocksInput()
        || (preferences && preferences->blocksInput())
        || (variableEditor && variableEditor->open)
        || (itemEditor && itemEditor->blocksInput())
        || (graph && graph->stackDialogOpen);
}

bool SceneMapCanvas::blocksInput() const
{
    return confirmMode != ConfirmMode::None || anyAuthoringModalOpen();
}

void SceneMapCanvas::closeContextMenu()
{
    contextMenuSource = ContextMenuSource::None;
    contextMenuSceneId.clear();
    contextMenuLinkFromId.clear();
    contextMenuLinkToId.clear();
    contextMenuLinkDirection.clear();
    contextMenuLinkReciprocal = false;
    contextMenuBounds = {0.0f, 0.0f, 0.0f, 0.0f};
    contextMenuAnchor = {0.0f, 0.0f};
}

void SceneMapCanvas::openContextMenu(
    ContextMenuSource source,
    const std::string& sceneId,
    Vector2 mouse)
{
    if (source == ContextMenuSource::None || source == ContextMenuSource::ExitLink
        || sceneId.empty() || docs == nullptr)
        return;
    if (!docs->scenes.hasScene(sceneId))
        return;
    if (blocksInput())
        return;

    // Select without starting a drag.
    dragSource = DragSource::None;
    dragSceneId.clear();
    cancelLinkDrag();
    cancelPortDrag();
    if (selectSceneForEditor)
        selectSceneForEditor(sceneId);

    contextMenuSource = source;
    contextMenuSceneId = sceneId;
    contextMenuLinkFromId.clear();
    contextMenuLinkToId.clear();
    contextMenuLinkDirection.clear();
    contextMenuLinkReciprocal = false;
    contextMenuAnchor = mouse;
    contextMenuBounds = {0.0f, 0.0f, 0.0f, 0.0f};
}

void SceneMapCanvas::openLinkContextMenu(int routeIndex, Vector2 mouse)
{
    if (routeIndex < 0 || routeIndex >= static_cast<int>(cachedLinkRoutes.size()))
        return;
    if (docs == nullptr || graph == nullptr)
        return;
    if (blocksInput())
        return;

    const SceneLinkRoute& route = cachedLinkRoutes[static_cast<size_t>(routeIndex)];
    if (route.fromId.empty() || route.toId.empty() || route.direction.empty())
        return;

    dragSource = DragSource::None;
    dragSceneId.clear();
    cancelLinkDrag();
    cancelPortDrag();

    contextMenuSource = ContextMenuSource::ExitLink;
    contextMenuSceneId.clear();
    contextMenuLinkFromId = route.fromId;
    contextMenuLinkToId = route.toId;
    contextMenuLinkDirection = route.direction;
    contextMenuLinkReciprocal = route.reciprocal;
    contextMenuAnchor = mouse;
    contextMenuBounds = {0.0f, 0.0f, 0.0f, 0.0f};
}

void SceneMapCanvas::deleteContextMenuLink()
{
    if (!graph || contextMenuLinkFromId.empty() || contextMenuLinkDirection.empty())
        return;
    graph->deleteExitLink(
        contextMenuLinkFromId,
        contextMenuLinkDirection,
        /*clearReciprocal=*/true);
    cancelLinkDrag();
    cachedLinkRoutes.clear();
}

void SceneMapCanvas::editContextMenuLinkTransition()
{
    if (contextMenuLinkFromId.empty() || contextMenuLinkToId.empty())
        return;
    sceneTransition.docs = docs;
    sceneTransition.graph = graph;
    sceneTransition.uiFont = uiFont;
    sceneTransition.uiFontBold = uiFontBold;
    sceneTransition.onSaved = [this]() {
        // Document already marked dirty by upsert.
        (void)this;
    };
    // Prefer destination (toId) when neither side has constrained SFX yet.
    sceneTransition.openForLink(
        contextMenuLinkFromId,
        contextMenuLinkToId,
        contextMenuLinkToId);
}

void SceneMapCanvas::cancelDragsForScene(const std::string& sceneId)
{
    if (!sceneId.empty() && dragSceneId == sceneId)
    {
        dragSource = DragSource::None;
        dragSceneId.clear();
    }
    if (!portDragFromId.empty() && portDragFromId == sceneId)
        cancelPortDrag();
    if (linkDragIndex >= 0 && linkDragIndex < static_cast<int>(cachedLinkRoutes.size()))
    {
        const SceneLinkRoute& route = cachedLinkRoutes[static_cast<size_t>(linkDragIndex)];
        if (route.fromId == sceneId || route.toId == sceneId)
            cancelLinkDrag();
    }
}

void SceneMapCanvas::beginRemoveFromMapConfirm(const std::string& sceneId)
{
    closeContextMenu();
    if (docs == nullptr || sceneId.empty() || !docs->scenes.hasScene(sceneId))
        return;
    if (!docs->scenes.hasMapPlacement(sceneId))
        return;
    confirmMode = ConfirmMode::RemoveFromMap;
    confirmSceneId = sceneId;
    pendingPurgePaths.clear();
    purgeListScroll = 0.0f;
    confirmWaitMouseRelease = true;
}

void SceneMapCanvas::beginDeleteSceneConfirm(const std::string& sceneId)
{
    closeContextMenu();
    if (docs == nullptr || sceneId.empty() || !docs->scenes.hasScene(sceneId))
        return;
    confirmMode = ConfirmMode::DeleteScene;
    confirmSceneId = sceneId;
    // Collect before removal so the confirm dialog can list unique resources.
    pendingPurgePaths = collectUniqueSceneAssetPaths(*docs, sceneId);
    purgeListScroll = 0.0f;
    confirmWaitMouseRelease = true;
}

void SceneMapCanvas::requestDeleteSelectedScene()
{
    if (selectionSceneId == nullptr || selectionSceneId->empty())
        return;
    if (blocksInput())
        return;
    beginDeleteSceneConfirm(*selectionSceneId);
}

void SceneMapCanvas::performRemoveFromMap()
{
    if (docs == nullptr || confirmSceneId.empty())
    {
        confirmMode = ConfirmMode::None;
        confirmSceneId.clear();
        confirmWaitMouseRelease = false;
        return;
    }
    const std::string id = confirmSceneId;
    docs->scenes.clearLayout(id);
    docs->markDirty();
    cancelDragsForScene(id);
    confirmMode = ConfirmMode::None;
    confirmSceneId.clear();
    confirmWaitMouseRelease = false;
}

void SceneMapCanvas::performDeleteScene(bool purgeUniqueAssets)
{
    if (docs == nullptr || confirmSceneId.empty() || !docs->scenes.hasScene(confirmSceneId))
    {
        confirmMode = ConfirmMode::None;
        confirmSceneId.clear();
        pendingPurgePaths.clear();
        confirmWaitMouseRelease = false;
        return;
    }

    const std::string removedId = confirmSceneId;
    std::vector<std::string> uniquePaths = pendingPurgePaths;
    if (uniquePaths.empty())
        uniquePaths = collectUniqueSceneAssetPaths(*docs, removedId);

    if (!docs->scenes.removeScene(removedId))
    {
        confirmMode = ConfirmMode::None;
        confirmSceneId.clear();
        pendingPurgePaths.clear();
        confirmWaitMouseRelease = false;
        return;
    }

    if (thumbnails)
        thumbnails->clear();
    cancelLinkDrag();
    cancelPortDrag();
    cancelDragsForScene(removedId);
    dragSource = DragSource::None;
    dragSceneId.clear();

    if (selectionSceneId && *selectionSceneId == removedId)
        selectionSceneId->clear();
    if (variableEditor)
        variableEditor->selectedVariableKey.clear();
    if (variablesScroll)
        *variablesScroll = 0.0f;

    const std::vector<std::string> remaining = docs->scenes.sceneIds();
    if (selectionSceneId && selectionSceneId->empty() && !remaining.empty())
        *selectionSceneId = remaining.front();

    if (docs->isConversationsTab() && conversation)
    {
        conversation->selectedKey.clear();
        conversation->rebuildConversationTree();
        for (const ConversationTreeNode& root : conversation->roots)
            conversation->expanded.insert(root.key);
    }

    docs->markDirty();

    if (purgeUniqueAssets && !uniquePaths.empty())
        purgeSceneAssetFiles(docs->assetRoot, uniquePaths);

    confirmMode = ConfirmMode::None;
    confirmSceneId.clear();
    pendingPurgePaths.clear();
    purgeListScroll = 0.0f;
    confirmWaitMouseRelease = false;
}

bool SceneMapCanvas::handleContextMenuClick(Vector2 mouse)
{
    if (contextMenuSource == ContextMenuSource::None)
        return false;
    if (contextMenuBounds.width < 1.0f)
        return false;

    if (!CheckCollisionPointRec(mouse, contextMenuBounds))
    {
        closeContextMenu();
        return true;
    }

    const float rowH = 22.0f;
    int i = static_cast<int>((mouse.y - contextMenuBounds.y - 2.0f) / rowH);
    const std::string sceneId = contextMenuSceneId;
    const ContextMenuSource source = contextMenuSource;
    // Snapshot link identity before closeContextMenu() clears it.
    const std::string linkFrom = contextMenuLinkFromId;
    const std::string linkTo = contextMenuLinkToId;
    const std::string linkDir = contextMenuLinkDirection;

    if (source == ContextMenuSource::ExitLink)
    {
        closeContextMenu();
        if (i == 0)
        {
            if (graph && !linkFrom.empty() && !linkDir.empty())
            {
                graph->deleteExitLink(linkFrom, linkDir, /*clearReciprocal=*/true);
                cancelLinkDrag();
                cachedLinkRoutes.clear();
            }
            return true;
        }
        if (i == 1)
        {
            if (!linkFrom.empty() && !linkTo.empty())
            {
                sceneTransition.docs = docs;
                sceneTransition.graph = graph;
                sceneTransition.uiFont = uiFont;
                sceneTransition.uiFontBold = uiFontBold;
                sceneTransition.onSaved = [this]() { (void)this; };
                sceneTransition.openForLink(linkFrom, linkTo, linkTo);
            }
            return true;
        }
        return true;
    }

    if (i == 0)
    {
        closeContextMenu();
        sceneAuthoring.openEditDialog(sceneId);
        return true;
    }
    if (i == 1)
    {
        if (source == ContextMenuSource::Map)
            beginRemoveFromMapConfirm(sceneId);
        else
            beginDeleteSceneConfirm(sceneId);
        return true;
    }

    closeContextMenu();
    return true;
}

void SceneMapCanvas::drawContextMenu()
{
    const bool isLinkMenu = contextMenuSource == ContextMenuSource::ExitLink;
    if (contextMenuSource == ContextMenuSource::None
        || (!isLinkMenu && contextMenuSceneId.empty())
        || (isLinkMenu && contextMenuLinkFromId.empty()))
    {
        contextMenuBounds = {0.0f, 0.0f, 0.0f, 0.0f};
        return;
    }

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    // ASCII "..." — UI fonts often lack U+2026 and draw it as '?'.
    const char* item0 = isLinkMenu ? "Delete" : "Edit...";
    const char* item1 = isLinkMenu
        ? "Edit Transition..."
        : ((contextMenuSource == ContextMenuSource::Map)
               ? "Remove from map"
               : "Delete scene...");
    const float rowH = 22.0f;
    const float pad = 10.0f;
    const float w0 = MeasureTextEx(font, item0, kFontSmall, 1.0f).x;
    const float w1 = MeasureTextEx(font, item1, kFontSmall, 1.0f).x;
    const float menuW = std::max(180.0f, std::max(w0, w1) + pad * 2.0f);
    const float menuH = rowH * 2.0f + 4.0f;

    float x = contextMenuAnchor.x;
    float y = contextMenuAnchor.y;
    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());
    if (x + menuW > screenW - 4.0f)
        x = screenW - menuW - 4.0f;
    if (y + menuH > screenH - 4.0f)
        y = screenH - menuH - 4.0f;
    if (x < 4.0f)
        x = 4.0f;
    if (y < 4.0f)
        y = 4.0f;

    contextMenuBounds = {x, y, menuW, menuH};
    DrawRectangleRec(contextMenuBounds, Color{36, 32, 44, 255});
    DrawRectangleLinesEx(contextMenuBounds, 1.0f, kPanelBorder);

    const Vector2 mouse = GetMousePosition();
    const char* items[2] = {item0, item1};
    float my = contextMenuBounds.y + 2.0f;
    for (int i = 0; i < 2; ++i)
    {
        const Rectangle row = {
            contextMenuBounds.x + 2.0f,
            my,
            contextMenuBounds.width - 4.0f,
            rowH - 2.0f};
        if (CheckCollisionPointRec(mouse, row))
            DrawRectangleRec(row, Color{60, 54, 72, 220});
        DrawTextEx(
            font,
            items[i],
            {row.x + 8.0f, row.y + 3.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);
        my += rowH;
    }

    if (editorMousePressed(MOUSE_BUTTON_LEFT))
        handleContextMenuClick(mouse);
    if (IsKeyPressed(KEY_ESCAPE))
        closeContextMenu();
}

void SceneMapCanvas::drawConfirmDialogs(int screenWidth, int screenHeight)
{
    if (confirmMode == ConfirmMode::None)
        return;

    if (confirmWaitMouseRelease)
    {
        if (!editorMouseDown(MOUSE_BUTTON_LEFT))
            confirmWaitMouseRelease = false;
    }

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    DrawRectangle(0, 0, screenWidth, screenHeight, kModalOverlay);
    const bool canClick =
        !confirmWaitMouseRelease && editorMousePressed(MOUSE_BUTTON_LEFT);

    if (confirmMode == ConfirmMode::RemoveFromMap)
    {
        const float dialogW = 460.0f;
        const float dialogH = 180.0f;
        const Rectangle dialog = {
            (static_cast<float>(screenWidth) - dialogW) * 0.5f,
            (static_cast<float>(screenHeight) - dialogH) * 0.5f,
            dialogW,
            dialogH};
        DrawRectangleRounded(dialog, 0.04f, 8, kModalFill);
        DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

        DrawTextEx(
            bold,
            "Remove from map",
            {dialog.x + 20.0f, dialog.y + 18.0f},
            kFontHeading,
            1.0f,
            kTextPrimary);
        drawWrappedText(
            font,
            "Remove " + confirmSceneId + " from the map? Scene stays in the list.",
            {dialog.x + 20.0f, dialog.y + 56.0f},
            dialogW - 40.0f,
            kFontBody,
            4.0f,
            kTextMuted);

        const float btnW = 120.0f;
        const float btnH = 34.0f;
        const float btnY = dialog.y + dialogH - btnH - 18.0f;
        const Rectangle confirmBtn = {
            dialog.x + dialogW - btnW * 2.0f - 36.0f, btnY, btnW, btnH};
        const Rectangle cancelBtn = {
            dialog.x + dialogW - btnW - 18.0f, btnY, btnW, btnH};
        drawEditorButton(font, confirmBtn, "Remove", true, true);
        drawEditorButton(font, cancelBtn, "Cancel", false, true);

        if (canClick)
        {
            const Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, confirmBtn))
                performRemoveFromMap();
            else if (CheckCollisionPointRec(mouse, cancelBtn)
                || !CheckCollisionPointRec(mouse, dialog))
            {
                confirmMode = ConfirmMode::None;
                confirmSceneId.clear();
            }
        }
        if (IsKeyPressed(KEY_ESCAPE))
        {
            confirmMode = ConfirmMode::None;
            confirmSceneId.clear();
            confirmWaitMouseRelease = false;
        }
        return;
    }

    // DeleteScene — one dialog with unique resource list + purge/keep/cancel.
    const bool hasUnique = !pendingPurgePaths.empty();
    const float dialogW = 560.0f;
    const float dialogH = hasUnique ? 360.0f : 200.0f;
    const Rectangle dialog = {
        (static_cast<float>(screenWidth) - dialogW) * 0.5f,
        (static_cast<float>(screenHeight) - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRounded(dialog, 0.04f, 8, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        "Delete scene",
        {dialog.x + 20.0f, dialog.y + 16.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    drawWrappedText(
        font,
        "Delete scene " + confirmSceneId + " permanently from the project?",
        {dialog.x + 20.0f, dialog.y + 48.0f},
        dialogW - 40.0f,
        kFontBody,
        4.0f,
        kTextMuted);

    if (hasUnique)
    {
        DrawTextEx(
            font,
            "These resources are only used by this scene. Delete them too?",
            {dialog.x + 20.0f, dialog.y + 88.0f},
            kFontTiny,
            1.0f,
            kTextMuted);

        const Rectangle listArea = {
            dialog.x + 16.0f,
            dialog.y + 110.0f,
            dialogW - 32.0f,
            dialogH - 180.0f};
        DrawRectangleRec(listArea, Color{18, 16, 24, 255});
        DrawRectangleLinesEx(listArea, 1.0f, kPanelInnerEdge);

        const float rowH = 18.0f;
        const float contentH = static_cast<float>(pendingPurgePaths.size()) * rowH;
        const float maxScroll = std::max(0.0f, contentH - listArea.height + 8.0f);
        if (CheckCollisionPointRec(GetMousePosition(), listArea))
            purgeListScroll -= GetMouseWheelMove() * 24.0f;
        purgeListScroll = std::clamp(purgeListScroll, 0.0f, maxScroll);

        BeginScissorMode(
            static_cast<int>(listArea.x),
            static_cast<int>(listArea.y),
            static_cast<int>(listArea.width),
            static_cast<int>(listArea.height));
        float y = listArea.y + 6.0f - purgeListScroll;
        for (const std::string& path : pendingPurgePaths)
        {
            DrawTextEx(
                font,
                path.c_str(),
                {listArea.x + 8.0f, y},
                kFontTiny,
                1.0f,
                kTextPrimary);
            y += rowH;
        }
        EndScissorMode();
    }
    else
    {
        DrawTextEx(
            font,
            "No unique asset files (shared resources will be kept).",
            {dialog.x + 20.0f, dialog.y + 96.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
    }

    const float btnH = 34.0f;
    const float btnY = dialog.y + dialogH - btnH - 16.0f;
    const float gap = 10.0f;
    if (hasUnique)
    {
        const float purgeW = 150.0f;
        const float keepW = 150.0f;
        const float cancelW = 100.0f;
        const Rectangle purgeBtn = {
            dialog.x + dialogW - purgeW - keepW - cancelW - gap * 2.0f - 18.0f,
            btnY,
            purgeW,
            btnH};
        const Rectangle keepBtn = {
            dialog.x + dialogW - keepW - cancelW - gap - 18.0f, btnY, keepW, btnH};
        const Rectangle cancelBtn = {
            dialog.x + dialogW - cancelW - 18.0f, btnY, cancelW, btnH};
        drawEditorButton(font, purgeBtn, "Delete & purge", true, true);
        drawEditorButton(font, keepBtn, "Delete, keep files", true, true);
        drawEditorButton(font, cancelBtn, "Cancel", false, true);

        if (canClick)
        {
            const Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, purgeBtn))
                performDeleteScene(true);
            else if (CheckCollisionPointRec(mouse, keepBtn))
                performDeleteScene(false);
            else if (CheckCollisionPointRec(mouse, cancelBtn)
                || !CheckCollisionPointRec(mouse, dialog))
            {
                confirmMode = ConfirmMode::None;
                confirmSceneId.clear();
                pendingPurgePaths.clear();
            }
        }
    }
    else
    {
        const float btnW = 120.0f;
        const Rectangle confirmBtn = {
            dialog.x + dialogW - btnW * 2.0f - 36.0f, btnY, btnW, btnH};
        const Rectangle cancelBtn = {
            dialog.x + dialogW - btnW - 18.0f, btnY, btnW, btnH};
        drawEditorButton(font, confirmBtn, "Delete", true, true);
        drawEditorButton(font, cancelBtn, "Cancel", false, true);

        if (canClick)
        {
            const Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, confirmBtn))
                performDeleteScene(false);
            else if (CheckCollisionPointRec(mouse, cancelBtn)
                || !CheckCollisionPointRec(mouse, dialog))
            {
                confirmMode = ConfirmMode::None;
                confirmSceneId.clear();
                pendingPurgePaths.clear();
            }
        }
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        confirmMode = ConfirmMode::None;
        confirmSceneId.clear();
        pendingPurgePaths.clear();
        confirmWaitMouseRelease = false;
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

    const Font stackFont = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    drawEditorButton(stackFont, upBtn, "Up", true, true);
    drawEditorButton(stackFont, downBtn, "Down", true, true);
    drawEditorButton(stackFont, cancelBtn, "Cancel", false, true);

    if (editorMousePressed(MOUSE_BUTTON_LEFT))
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

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Vector2 mouse = GetMousePosition();
    const bool canInteract = !graph->stackDialogOpen
        && !(variableEditor && variableEditor->open)
        && !(layout && layout->isDraggingDivider())
        && !sceneAuthoring.blocksInput()
        && !sceneAssist.blocksInput()
        && !sceneInventory.blocksInput()
        && !sceneEffects.blocksInput()
        && !sceneTransition.blocksInput()
        && !(preferences && preferences->blocksInput())
        && confirmMode == ConfirmMode::None
        && contextMenuSource == ContextMenuSource::None;

    // Header: New Scene
    const float headerH = 28.0f;
    const Rectangle newBtn = {
        listBounds.x + listBounds.width - 108.0f,
        listBounds.y + 2.0f,
        100.0f,
        24.0f};
    DrawTextEx(
        font,
        "Scenes",
        {listBounds.x + 10.0f, listBounds.y + 6.0f},
        kFontTiny,
        1.0f,
        kPanelBorder);
    drawEditorButton(font, newBtn, "New Scene", true, canInteract);
    if (canInteract && editorMousePressed(MOUSE_BUTTON_LEFT)
        && CheckCollisionPointRec(mouse, newBtn))
    {
        sceneAuthoring.openDialog();
    }

    const Rectangle treeBounds = {
        listBounds.x,
        listBounds.y + headerH,
        listBounds.width,
        listBounds.height - headerH};

    const std::vector<std::string> ids = docs->scenes.sceneIds();
    const float contentHeight = static_cast<float>(ids.size()) * kListRowHeight;
    const float maxScroll = std::max(0.0f, contentHeight - treeBounds.height);
    if (listScroll > maxScroll)
        listScroll = maxScroll;

    BeginScissorMode(
        static_cast<int>(treeBounds.x),
        static_cast<int>(treeBounds.y),
        static_cast<int>(treeBounds.width),
        static_cast<int>(treeBounds.height));

    float y = treeBounds.y - listScroll;
    for (const std::string& id : ids)
    {
        const Rectangle row = {
            treeBounds.x + 4.0f, y, treeBounds.width - 8.0f, kListRowHeight - 4.0f};
        const bool selected = selectionSceneId && id == (*selectionSceneId);
        if (selected)
            DrawRectangleRec(row, kSelection);

        const ThumbnailEntry& thumb =
            thumbnails->getOrLoad(id, docs->scenes, docs->assetRoot, docs->resourceDir);
        const Rectangle thumbRect = {
            row.x + 6.0f, row.y + 6.0f, kListThumbSize, kListThumbSize};
        DrawRectangleRec(thumbRect, Color{48, 44, 58, 255});
        if (thumb.loaded)
        {
            DrawTexturePro(
                thumb.texture,
                {0.0f,
                 0.0f,
                 static_cast<float>(thumb.texture.width),
                 static_cast<float>(thumb.texture.height)},
                thumbRect,
                {0.0f, 0.0f},
                0.0f,
                WHITE);
        }

        const float textX = row.x + kListThumbSize + 14.0f;
        const float textRightMargin = 10.0f;
        const float textMaxW = std::max(8.0f, row.x + row.width - textRightMargin - textX);
        // Keep text block aligned with the thumb (top pad matches thumbRect.y inset).
        const float textY = row.y + 6.0f;
        DrawTextEx(font, id.c_str(), {textX, textY}, kListNameFont, 1.0f, kTextPrimary);

        std::string description;
        if (const nlohmann::json* scene = docs->scenes.sceneJson(id);
            scene != nullptr && scene->is_object())
            description = scene->value("description", "");
        const std::string preview = fitSceneListDescriptionPreview(
            font, description, textMaxW, kListMetaFont);
        if (!preview.empty())
        {
            // Dimmer than kTextMuted so the id stays primary.
            const Color descColor{100, 92, 78, 200};
            DrawTextEx(
                font,
                preview.c_str(),
                {textX, textY + kListNameFont + 6.0f},
                kListMetaFont,
                1.0f,
                descColor);
        }

        y += kListRowHeight;
    }

    EndScissorMode();

    // Right-click list row → context menu (select only; never starts drag).
    // Menu itself is drawn later from draw() so the list scissor cannot clip it.
    const bool canOpenContext =
        !graph->stackDialogOpen
        && !(variableEditor && variableEditor->open)
        && !(layout && layout->isDraggingDivider())
        && !sceneAuthoring.blocksInput()
        && !sceneAssist.blocksInput()
        && !sceneInventory.blocksInput()
        && !sceneEffects.blocksInput()
        && !sceneTransition.blocksInput()
        && !(preferences && preferences->blocksInput())
        && confirmMode == ConfirmMode::None;
    if (canOpenContext && CheckCollisionPointRec(mouse, treeBounds)
        && editorMousePressed(MOUSE_BUTTON_RIGHT))
    {
        const float localY = (mouse.y - treeBounds.y) + listScroll;
        if (localY >= 0.0f)
        {
            const int index = static_cast<int>(localY / kListRowHeight);
            if (index >= 0 && index < static_cast<int>(ids.size()))
                openContextMenu(
                    ContextMenuSource::List,
                    ids[static_cast<size_t>(index)],
                    mouse);
            else
                closeContextMenu();
        }
        else
            closeContextMenu();
    }

    if (canInteract && CheckCollisionPointRec(mouse, treeBounds)
        && editorMousePressed(MOUSE_BUTTON_LEFT))
    {
        const float localY = (mouse.y - treeBounds.y) + listScroll;
        if (localY >= 0.0f)
        {
            const int index = static_cast<int>(localY / kListRowHeight);
            if (index >= 0 && index < static_cast<int>(ids.size()))
            {
                const std::string& id = ids[static_cast<size_t>(index)];
                if (selectSceneForEditor)
                    selectSceneForEditor(id);
                dragSource = DragSource::SceneList;
                dragSceneId = id;
                // Card-local grab point so the ghost follows the cursor across
                // the map (NOT list-pane coordinates — that pinned the ghost
                // to the left column and made list→map drag look broken).
                const SceneMapCanvas::SceneCardMetrics metrics = measureSceneCard(id);
                dragOffset = {metrics.width * 0.5f, 24.0f};
            }
        }
    }

    if (CheckCollisionPointRec(GetMousePosition(), treeBounds)
        && confirmMode == ConfirmMode::None
        && contextMenuSource == ContextMenuSource::None)
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

        if (CheckCollisionPointRec(GetMousePosition(), tab) && editorMousePressed(MOUSE_BUTTON_LEFT))
        {
            docs->activeTabIndex = static_cast<int>(i);
            thumbnails->clear();
            if (requestReload) requestReload();
        }

        x += tabWidth;
    }
}


void SceneMapCanvas::stopScenePreviewAudio()
{
    if (previewMusicLoaded && IsMusicStreamPlaying(previewMusic))
        StopMusicStream(previewMusic);
    previewMusicPlaying = false;
    if (previewAmbientLoaded && IsMusicStreamPlaying(previewAmbient))
        StopMusicStream(previewAmbient);
    previewAmbientPlaying = false;
}

void SceneMapCanvas::unloadScenePreviewMedia()
{
    stopScenePreviewAudio();
    if (previewMusicLoaded)
    {
        UnloadMusicStream(previewMusic);
        previewMusic = {};
        previewMusicLoaded = false;
    }
    if (!previewMusicTempFile.empty())
    {
        std::remove(previewMusicTempFile.c_str());
        previewMusicTempFile.clear();
    }
    previewMusicPath.clear();

    if (previewAmbientLoaded)
    {
        UnloadMusicStream(previewAmbient);
        previewAmbient = {};
        previewAmbientLoaded = false;
    }
    if (!previewAmbientTempFile.empty())
    {
        std::remove(previewAmbientTempFile.c_str());
        previewAmbientTempFile.clear();
    }
    previewAmbientPath.clear();

    if (previewLargeLoaded && previewLargeTexture.id != 0)
    {
        UnloadTexture(previewLargeTexture);
        previewLargeTexture = {};
        previewLargeLoaded = false;
    }
    previewLargePath.clear();
    previewBoundSceneId.clear();
}

bool SceneMapCanvas::loadScenePreviewMusic(
    const std::string& relPath,
    Music& outMusic,
    std::string& outTempFile)
{
    if (!previewAudioReady)
    {
        if (!IsAudioDeviceReady())
            InitAudioDevice();
        previewAudioReady = IsAudioDeviceReady();
    }
    if (!previewAudioReady || docs == nullptr || relPath.empty())
        return false;

    using timberline_engine::buildAssetSearchPaths;
    using timberline_engine::compressedAssetPath;
    using timberline_engine::loadAssetBytesFromFile;
    using timberline_engine::pathJoin;

    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    std::vector<std::string> candidates = buildAssetSearchPaths(assetRoot, relPath);
    if (!docs->resourceDir.empty())
    {
        std::string stripped = relPath;
        if (stripped.rfind("resources/", 0) == 0)
            stripped = stripped.substr(std::string("resources/").size());
        candidates.push_back(pathJoin(docs->resourceDir, stripped));
    }

    for (const std::string& path : candidates)
    {
        if (FileExists(path.c_str()))
        {
            outMusic = LoadMusicStream(path.c_str());
            if (IsMusicValid(outMusic))
            {
                outMusic.looping = true;
                return true;
            }
        }
        const std::string compressed = compressedAssetPath(path);
        if (FileExists(compressed.c_str()))
        {
            std::vector<unsigned char> bytes;
            if (loadAssetBytesFromFile(compressed, bytes) && !bytes.empty())
            {
                std::string fileType = ".mp3";
                const size_t dot = path.find_last_of('.');
                if (dot != std::string::npos)
                    fileType = path.substr(dot);
                const std::string tmp = pathJoin(
                    GetApplicationDirectory() ? GetApplicationDirectory() : ".",
                    std::string("editor_scene_bed_")
                        + (outTempFile.empty() ? "a" : "b")
                        + fileType);
                std::ofstream out(tmp.c_str(), std::ios::binary);
                if (out)
                {
                    out.write(
                        reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
                    out.close();
                    outMusic = LoadMusicStream(tmp.c_str());
                    if (IsMusicValid(outMusic))
                    {
                        outMusic.looping = true;
                        outTempFile = tmp;
                        return true;
                    }
                    std::remove(tmp.c_str());
                }
            }
        }
    }
    return false;
}

bool SceneMapCanvas::loadScenePreviewTexture(const std::string& relPath)
{
    if (previewLargeLoaded && previewLargeTexture.id != 0)
    {
        UnloadTexture(previewLargeTexture);
        previewLargeTexture = {};
        previewLargeLoaded = false;
    }
    previewLargePath = relPath;
    if (docs == nullptr || relPath.empty())
        return false;

    using timberline_engine::buildAssetSearchPaths;
    using timberline_engine::compressedAssetPath;
    using timberline_engine::loadTextureFromAssetFile;

    const std::string assetRoot = docs->assetRoot.empty() ? "." : docs->assetRoot;
    const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, relPath);
    for (const std::string& path : paths)
    {
        const std::string compressed = compressedAssetPath(path);
        if (FileExists(compressed.c_str())
            && loadTextureFromAssetFile(compressed, previewLargeTexture))
        {
            previewLargeLoaded = true;
            return true;
        }
        if (FileExists(path.c_str())
            && loadTextureFromAssetFile(path, previewLargeTexture))
        {
            previewLargeLoaded = true;
            return true;
        }
    }
    return false;
}

void SceneMapCanvas::syncScenePreviewMedia()
{
    if (docs == nullptr || selectionSceneId == nullptr)
        return;

    const std::string sceneId = *selectionSceneId;
    if (sceneId.empty() || !docs->scenes.hasScene(sceneId))
    {
        if (!previewBoundSceneId.empty())
            unloadScenePreviewMedia();
        return;
    }

    std::string imagePath = docs->scenes.getSceneImagePath(sceneId);
    std::string musicPath = docs->scenes.getSceneMusicPath(sceneId);
    std::string ambientPath = docs->scenes.getSceneAmbientPath(sceneId);

    // AI Assist preview overrides (temp paths).
    if (sceneAssist.hasPreviewOverride(sceneId))
    {
        const std::string oImg = sceneAssist.overrideImagePath(sceneId);
        const std::string oAmb = sceneAssist.overrideAmbientPath(sceneId);
        const std::string oMus = sceneAssist.overrideMusicPath(sceneId);
        if (!oImg.empty())
            imagePath = oImg;
        if (!oAmb.empty())
            ambientPath = oAmb;
        if (!oMus.empty())
            musicPath = oMus;
    }

    if (sceneId != previewBoundSceneId)
    {
        stopScenePreviewAudio();
        previewBoundSceneId = sceneId;
        // Force reload of all channels for the new scene.
        previewMusicPath.clear();
        previewAmbientPath.clear();
        previewLargePath.clear();
    }

    if (imagePath != previewLargePath)
        loadScenePreviewTexture(imagePath);

    // Reload when the path changes, or when a previous attempt failed (e.g. file
    // appeared after Generate). Clearing the cached path on failure lets the next
    // frame retry without waiting for an explicit invalidate.
    const bool musicNeedsLoad =
        (musicPath != previewMusicPath)
        || (!musicPath.empty() && !previewMusicLoaded);
    if (musicNeedsLoad)
    {
        if (previewMusicLoaded && IsMusicStreamPlaying(previewMusic))
            StopMusicStream(previewMusic);
        if (previewMusicLoaded)
        {
            UnloadMusicStream(previewMusic);
            previewMusic = {};
            previewMusicLoaded = false;
        }
        if (!previewMusicTempFile.empty())
        {
            std::remove(previewMusicTempFile.c_str());
            previewMusicTempFile.clear();
        }
        previewMusicPlaying = false;
        previewMusicPath = musicPath;
        if (!musicPath.empty())
            previewMusicLoaded =
                loadScenePreviewMusic(musicPath, previewMusic, previewMusicTempFile);
        if (!previewMusicLoaded)
            previewMusicPath.clear();
    }

    const bool ambientNeedsLoad =
        (ambientPath != previewAmbientPath)
        || (!ambientPath.empty() && !previewAmbientLoaded);
    if (ambientNeedsLoad)
    {
        if (previewAmbientLoaded && IsMusicStreamPlaying(previewAmbient))
            StopMusicStream(previewAmbient);
        if (previewAmbientLoaded)
        {
            UnloadMusicStream(previewAmbient);
            previewAmbient = {};
            previewAmbientLoaded = false;
        }
        if (!previewAmbientTempFile.empty())
        {
            std::remove(previewAmbientTempFile.c_str());
            previewAmbientTempFile.clear();
        }
        previewAmbientPlaying = false;
        previewAmbientPath = ambientPath;
        if (!ambientPath.empty())
            previewAmbientLoaded = loadScenePreviewMusic(
                ambientPath, previewAmbient, previewAmbientTempFile);
        if (!previewAmbientLoaded)
            previewAmbientPath.clear();
    }

    if (previewMusicLoaded && previewMusicPlaying)
        UpdateMusicStream(previewMusic);
    if (previewAmbientLoaded && previewAmbientPlaying)
        UpdateMusicStream(previewAmbient);
}

void SceneMapCanvas::drawScenePreviewPane(Rectangle paneBounds)
{
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    syncScenePreviewMedia();

    DrawTextEx(
        font,
        "Scene Preview",
        {paneBounds.x + 12.0f, paneBounds.y + 8.0f},
        kFontLabel,
        1.0f,
        kTextMuted);

    if (selectionSceneId == nullptr || selectionSceneId->empty()
        || docs == nullptr || !docs->scenes.hasScene(*selectionSceneId))
    {
        DrawTextEx(
            font,
            "Select a scene",
            {paneBounds.x + 12.0f, paneBounds.y + 36.0f},
            kFontBody,
            1.0f,
            kTextMuted);
        return;
    }

    const float transportW = 118.0f;
    const Rectangle transport = {
        paneBounds.x + 8.0f,
        paneBounds.y + 30.0f,
        transportW,
        paneBounds.height - 38.0f};
    const Rectangle imageBounds = {
        transport.x + transport.width + 8.0f,
        paneBounds.y + 30.0f,
        paneBounds.width - transportW - 24.0f,
        paneBounds.height - 38.0f};

    DrawRectangleRec(transport, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(transport, 1.0f, kPanelInnerEdge);

    const Vector2 mouse = GetMousePosition();
    const bool canInteract = !(graph && graph->stackDialogOpen)
        && !(variableEditor && variableEditor->open)
        && !sceneAuthoring.blocksInput()
        && !sceneAssist.blocksInput()
        && !sceneInventory.blocksInput()
        && !sceneEffects.blocksInput()
        && !sceneTransition.blocksInput()
        && !(preferences && preferences->blocksInput())
        && confirmMode == ConfirmMode::None;

    auto drawTransportBtn = [&](Rectangle btn, const char* label, bool enabled, bool active) {
        drawEditorButton(font, btn, label, active, enabled);
    };

    float ty = transport.y + 10.0f;
    DrawTextEx(font, "Music", {transport.x + 10.0f, ty}, kFontTiny, 1.0f, kTextMuted);
    ty += 18.0f;
    Rectangle musicBtn = {transport.x + 8.0f, ty, transport.width - 16.0f, 28.0f};
    const bool musicPlaying =
        previewMusicPlaying && previewMusicLoaded && IsMusicStreamPlaying(previewMusic);
    drawTransportBtn(
        musicBtn,
        musicPlaying ? "Pause" : "Play",
        previewMusicLoaded,
        musicPlaying);
    ty += 36.0f;
    DrawTextEx(
        font,
        previewMusicPath.empty() ? "(none)" : "loaded",
        {transport.x + 10.0f, ty},
        kFontTiny,
        1.0f,
        kTextMuted);

    ty += 28.0f;
    DrawTextEx(font, "Ambient", {transport.x + 10.0f, ty}, kFontTiny, 1.0f, kTextMuted);
    ty += 18.0f;
    Rectangle ambientBtn = {transport.x + 8.0f, ty, transport.width - 16.0f, 28.0f};
    const bool ambientPlaying =
        previewAmbientPlaying && previewAmbientLoaded
        && IsMusicStreamPlaying(previewAmbient);
    drawTransportBtn(
        ambientBtn,
        ambientPlaying ? "Pause" : "Play",
        previewAmbientLoaded,
        ambientPlaying);
    ty += 36.0f;
    DrawTextEx(
        font,
        previewAmbientPath.empty() ? "(none)" : "loaded",
        {transport.x + 10.0f, ty},
        kFontTiny,
        1.0f,
        kTextMuted);

    // Compact scene loot summary (Takeables / inventory).
    ty += 28.0f;
    DrawTextEx(font, "Loot", {transport.x + 10.0f, ty}, kFontTiny, 1.0f, kTextMuted);
    ty += 16.0f;
    {
        std::vector<std::string> lootIds;
        if (docs != nullptr && selectionSceneId != nullptr)
        {
            const nlohmann::json* scene = docs->scenes.sceneJson(*selectionSceneId);
            if (scene != nullptr && scene->is_object())
            {
                if (scene->contains("takeables") && (*scene)["takeables"].is_array())
                {
                    for (const nlohmann::json& row : (*scene)["takeables"])
                    {
                        if (!row.is_object())
                            continue;
                        const std::string id = row.value("id", row.value("defId", ""));
                        if (!id.empty())
                            lootIds.push_back(id);
                    }
                }
                else if (scene->contains("inventory") && (*scene)["inventory"].is_array())
                {
                    for (const nlohmann::json& row : (*scene)["inventory"])
                    {
                        if (!row.is_object())
                            continue;
                        const std::string id = row.value("defId", row.value("id", ""));
                        if (!id.empty())
                            lootIds.push_back(id);
                    }
                }
            }
        }
        if (lootIds.empty())
        {
            DrawTextEx(
                font,
                "(none)",
                {transport.x + 10.0f, ty},
                kFontTiny,
                1.0f,
                kTextMuted);
        }
        else
        {
            for (size_t i = 0; i < lootIds.size() && i < 4; ++i)
            {
                DrawTextEx(
                    font,
                    lootIds[i].c_str(),
                    {transport.x + 10.0f, ty},
                    kFontTiny,
                    1.0f,
                    kTextPrimary);
                ty += 14.0f;
            }
            if (lootIds.size() > 4)
            {
                DrawTextEx(
                    font,
                    ("+" + std::to_string(lootIds.size() - 4) + " more").c_str(),
                    {transport.x + 10.0f, ty},
                    kFontTiny,
                    1.0f,
                    kTextMuted);
                ty += 14.0f;
            }
        }
    }

    // Compact non-zero effects summary.
    ty += 18.0f;
    DrawTextEx(font, "Effects", {transport.x + 10.0f, ty}, kFontTiny, 1.0f, kTextMuted);
    ty += 16.0f;
    {
        std::string summary;
        if (docs != nullptr && selectionSceneId != nullptr)
        {
            const nlohmann::json* scene = docs->scenes.sceneJson(*selectionSceneId);
            if (scene != nullptr)
                summary = summarizeSceneEffects(*scene);
        }
        drawWrappedText(
            font,
            summary.empty() ? "(none)" : summary,
            {transport.x + 10.0f, ty},
            transport.width - 16.0f,
            kFontTiny,
            2.0f,
            summary.empty() ? kTextMuted : Color{160, 180, 120, 255});
    }

    if (canInteract && editorMousePressed(MOUSE_BUTTON_LEFT))
    {
        if (previewMusicLoaded && CheckCollisionPointRec(mouse, musicBtn))
        {
            if (musicPlaying)
            {
                PauseMusicStream(previewMusic);
                previewMusicPlaying = false;
            }
            else
            {
                if (!IsMusicStreamPlaying(previewMusic))
                    PlayMusicStream(previewMusic);
                else
                    ResumeMusicStream(previewMusic);
                previewMusicPlaying = true;
            }
        }
        else if (previewAmbientLoaded && CheckCollisionPointRec(mouse, ambientBtn))
        {
            if (ambientPlaying)
            {
                PauseMusicStream(previewAmbient);
                previewAmbientPlaying = false;
            }
            else
            {
                if (!IsMusicStreamPlaying(previewAmbient))
                    PlayMusicStream(previewAmbient);
                else
                    ResumeMusicStream(previewAmbient);
                previewAmbientPlaying = true;
            }
        }
    }

    DrawRectangleRec(imageBounds, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(imageBounds, 1.0f, kPanelInnerEdge);

    if (previewLargeLoaded && previewLargeTexture.id != 0)
    {
        const float tw = static_cast<float>(previewLargeTexture.width);
        const float th = static_cast<float>(previewLargeTexture.height);
        const float pad = 6.0f;
        const float maxW = imageBounds.width - pad * 2.0f;
        const float maxH = imageBounds.height - pad * 2.0f;
        float dw = maxW;
        float dh = (th / std::max(1.0f, tw)) * dw;
        if (dh > maxH)
        {
            dh = maxH;
            dw = (tw / std::max(1.0f, th)) * dh;
        }
        const Rectangle dest = {
            imageBounds.x + (imageBounds.width - dw) * 0.5f,
            imageBounds.y + (imageBounds.height - dh) * 0.5f,
            dw,
            dh};
        DrawTexturePro(
            previewLargeTexture,
            {0, 0, tw, th},
            dest,
            {0, 0},
            0.0f,
            WHITE);
    }
    else
    {
        DrawTextEx(
            font,
            "No scene image",
            {imageBounds.x + 12.0f, imageBounds.y + 12.0f},
            kFontSmall,
            1.0f,
            kTextMuted);
    }

    if (sceneAssist.hasPreviewOverride(*selectionSceneId))
    {
        DrawTextEx(
            font,
            "AI preview",
            {imageBounds.x + 8.0f, imageBounds.y + imageBounds.height - 18.0f},
            kFontTiny,
            1.0f,
            Color{220, 160, 80, 255});
    }
}


void SceneMapCanvas::drawBottomPane(Rectangle bottomBounds)
{
    drawPanel(bottomBounds);

    const float splitX = bottomBounds.x + bottomBounds.width * 0.55f;
    const Rectangle variablesBounds = {bottomBounds.x, bottomBounds.y,
                                       splitX - bottomBounds.x, bottomBounds.height};
    const Rectangle previewBounds = {splitX + 2.0f, bottomBounds.y,
                                    bottomBounds.x + bottomBounds.width - splitX - 2.0f,
                                    bottomBounds.height};

    DrawLineEx(
        {splitX, bottomBounds.y + 12.0f},
        {splitX, bottomBounds.y + bottomBounds.height - 12.0f},
        1.5f,
        kDividerGrip);

    const bool paneInteract = !sceneAuthoring.blocksInput()
        && !sceneAssist.blocksInput()
        && !sceneInventory.blocksInput()
        && !sceneEffects.blocksInput()
        && !sceneTransition.blocksInput()
        && !(preferences && preferences->blocksInput())
        && !(itemEditor && itemEditor->blocksInput())
        && !(variableEditor && variableEditor->open)
        && confirmMode == ConfirmMode::None;
    variableEditor->drawVariablesPane(variablesBounds, paneInteract);
    drawScenePreviewPane(previewBounds);
}


void SceneMapCanvas::drawDividers(int screenWidth, int screenHeight) const
{
    drawDivider(layout->verticalDividerBounds(screenWidth), layout->draggingVerticalDivider, true);
    drawDivider(layout->horizontalDividerBounds(screenWidth), layout->draggingHorizontalDivider, false);
}


void SceneMapCanvas::drawStatusBar(int screenWidth, int screenHeight)
{
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const std::string status = docs->dirty ? "Modified" : "Saved";
    std::string pathLabel = "Resources: " + docs->resourceDir;
    if (docs->isConversationsTab() && docs->conversationsLoaded)
        pathLabel = docs->conversationsPath;
    else if (docs->isItemsTab() && docs->itemsLoaded)
        pathLabel = docs->itemsPath;
    else if (docs->scenes.isLoaded())
        pathLabel = docs->scenes.path();

    const float statusX = static_cast<float>(screenWidth) - 70.0f;
    const float statusY = static_cast<float>(screenHeight) - 18.0f;
    // Compact gear hit target just left of the Saved/Modified label.
    const float gearSize = 12.0f;
    const Rectangle gearBtn = {
        statusX - gearSize - 10.0f,
        static_cast<float>(screenHeight) - 17.0f,
        gearSize,
        gearSize};
    const bool canPrefs = !(preferences && preferences->blocksInput())
        && !(variableEditor && variableEditor->open)
        && !sceneAuthoring.blocksInput()
        && !sceneAssist.blocksInput()
        && !sceneInventory.blocksInput()
        && !sceneEffects.blocksInput()
        && !sceneTransition.blocksInput()
        && confirmMode == ConfirmMode::None;

    DrawTextEx(
        font,
        pathLabel.c_str(),
        {8.0f, statusY},
        kFontTiny,
        1.0f,
        kTextMuted);

    // Procedural gear: round hub + 8 square teeth fused to the rim.
    {
        const Vector2 c = {
            gearBtn.x + gearBtn.width * 0.5f,
            gearBtn.y + gearBtn.height * 0.5f};
        const float bodyR = gearSize * 0.32f;
        const float toothW = gearSize * 0.22f;
        const float toothH = gearSize * 0.28f; // radial length; overlaps body
        const bool hover = CheckCollisionPointRec(GetMousePosition(), gearBtn);
        const Color gearColor = !canPrefs
            ? kTextDisabled
            : (hover ? kPanelBorder : kTextMuted);

        DrawCircleV(c, bodyR, gearColor);
        for (int i = 0; i < 8; ++i)
        {
            const float a = static_cast<float>(i) * (3.14159265f * 0.25f);
            // Center of tooth sits on the rim so it connects to the hub.
            const float midR = bodyR - 0.5f;
            const Vector2 mid = {
                c.x + std::cos(a) * midR,
                c.y + std::sin(a) * midR};
            DrawRectanglePro(
                {mid.x, mid.y, toothW, toothH},
                {toothW * 0.5f, toothH * 0.15f},
                a * (180.0f / 3.14159265f) + 90.0f,
                gearColor);
        }
        DrawCircleV(c, bodyR * 0.42f, Color{14, 13, 18, 255});
    }

    DrawTextEx(
        font,
        status.c_str(),
        {statusX, statusY},
        kFontTiny,
        1.0f,
        docs->dirty ? Color{200, 140, 80, 255} : kTextMuted);

    if (canPrefs && openPreferences && editorMousePressed(MOUSE_BUTTON_LEFT)
        && CheckCollisionPointRec(GetMousePosition(), gearBtn))
    {
        openPreferences();
    }
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
    if (docs && docs->isConversationsTab() && dialogWalkthrough)
    {
        dialogWalkthrough->draw(canvasBounds);
    }
    else if (docs && docs->isItemsTab())
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
    // Context menu after list/canvas (and their scissors) so it is never clipped.
    drawContextMenu();
    drawStackDialog(screenWidth, screenHeight);
    drawConfirmDialogs(screenWidth, screenHeight);
    if (itemEditor)
        itemEditor->drawNewItemDialog(screenWidth, screenHeight);
    if (variableEditor)
        variableEditor->drawVariableEditor(screenWidth, screenHeight);
    sceneAuthoring.draw(screenWidth, screenHeight);
    sceneAssist.draw(screenWidth, screenHeight);
    sceneInventory.draw(screenWidth, screenHeight);
    sceneEffects.draw(screenWidth, screenHeight);
    sceneTransition.draw(screenWidth, screenHeight);
    if (preferences)
        preferences->draw(screenWidth, screenHeight);

    EndDrawing();
}

} // namespace timberline_editor
