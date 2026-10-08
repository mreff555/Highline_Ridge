/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations bottom-left palette: dialog node type stubs (Phase 1).
 ******************************************************************************/

#include "DialogNodePalette.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace timberline_editor
{

namespace
{
constexpr int kCols = 3;
constexpr int kRows = 2;
/** Wide column gutters so labels under icons do not collide. */
constexpr float kMinGapX = 28.0f;
constexpr float kGapY = 22.0f;
constexpr float kLabelH = 18.0f;
constexpr float kPad = 10.0f;
constexpr float kHeader = 28.0f;
constexpr float kFooter = 22.0f;

struct PaletteLayout
{
    float icon = 48.0f;
    float cellW = 48.0f;
    float cellH = 66.0f;
    float gapX = kMinGapX;
    float x0 = 0.0f;
    float y0 = 0.0f;
};

PaletteLayout computeLayout(Rectangle bounds, float iconHint)
{
    PaletteLayout L;
    const float availW = std::max(1.0f, bounds.width - kPad * 2.0f);
    // Divide width into equal columns with a guaranteed horizontal gutter.
    L.gapX = kMinGapX;
    L.cellW = (availW - L.gapX * static_cast<float>(kCols - 1))
        / static_cast<float>(kCols);
    if (L.cellW < 36.0f)
    {
        // On very narrow panes, shrink gutters slightly but keep separation.
        L.gapX = 16.0f;
        L.cellW = (availW - L.gapX * static_cast<float>(kCols - 1))
            / static_cast<float>(kCols);
    }

    const float availH = bounds.height - kHeader - kFooter - kPad * 2.0f
        - kGapY * static_cast<float>(kRows - 1)
        - kLabelH * static_cast<float>(kRows);
    float byH = availH / static_cast<float>(kRows);
    L.icon = std::min({iconHint, L.cellW, byH, 64.0f});
    if (L.icon < 28.0f)
        L.icon = 28.0f;

    L.cellH = L.icon + kLabelH + 4.0f;
    // Upper-left justify (not centered in the pane).
    L.x0 = bounds.x + kPad;
    L.y0 = bounds.y + kHeader + kPad;
    return L;
}

std::string fitLabel(Font font, const char* label, float maxW)
{
    std::string s = label ? label : "";
    if (measureUiTextWidth(font, s, kFontTiny) <= maxW)
        return s;
    while (s.size() > 3
           && measureUiTextWidth(font, s + "...", kFontTiny) > maxW)
        s.pop_back();
    if (s.size() + 3 < std::string(label ? label : "").size())
        s += "...";
    return s;
}
} // namespace

float DialogNodePalette::iconSizeForBounds(Rectangle bounds) const
{
    return computeLayout(bounds, 64.0f).icon;
}

void DialogNodePalette::handleInput(Rectangle bounds, bool allowInteraction)
{
    if (!allowInteraction || flow == nullptr)
        return;

    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, bounds))
        return;

    if (!editorMousePressed(MOUSE_BUTTON_LEFT))
        return;

    flow->ensureIconsLoaded();

    const PaletteLayout L = computeLayout(bounds, 64.0f);
    for (int i = 0; i < static_cast<int>(DialogNodeKind::Count); ++i)
    {
        const int col = i % kCols;
        const int row = i / kCols;
        const float x = L.x0 + static_cast<float>(col) * (L.cellW + L.gapX);
        const float y = L.y0 + static_cast<float>(row) * (L.cellH + kGapY);
        const Rectangle cell = {x, y, L.cellW, L.cellH};
        if (CheckCollisionPointRec(mouse, cell))
        {
            flow->beginPlaceFromPalette(static_cast<DialogNodeKind>(i));
            return;
        }
    }
}

void DialogNodePalette::draw(Rectangle bounds)
{
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());

    if (flow != nullptr)
        flow->ensureIconsLoaded();

    DrawRectangleRec(bounds, kPanelFill);
    DrawRectangleLinesEx(bounds, 1.0f, kPanelInnerEdge);

    DrawTextEx(
        font,
        "Dialog types",
        {bounds.x + 10.0f, bounds.y + 8.0f},
        kFontLabel,
        1.0f,
        kTextMuted);

    const PaletteLayout L = computeLayout(bounds, 64.0f);
    const Vector2 mouse = GetMousePosition();
    DialogNodeKind hoverKind = DialogNodeKind::Count;

    for (int i = 0; i < static_cast<int>(DialogNodeKind::Count); ++i)
    {
        const DialogNodeKind kind = static_cast<DialogNodeKind>(i);
        const int col = i % kCols;
        const int row = i / kCols;
        const float x = L.x0 + static_cast<float>(col) * (L.cellW + L.gapX);
        const float y = L.y0 + static_cast<float>(row) * (L.cellH + kGapY);
        // Icon upper-left within its column cell.
        const Rectangle tile = {x, y, L.icon, L.icon};
        const Rectangle cell = {x, y, L.cellW, L.cellH};
        const bool hover = CheckCollisionPointRec(mouse, cell);
        if (hover)
            hoverKind = kind;

        DrawRectangleRounded(tile, 0.16f, 6, Color{40, 36, 48, 255});
        DrawRectangleRoundedLines(
            tile,
            0.16f,
            6,
            hover ? kPanelBorder : kPanelAccent);

        if (flow != nullptr)
        {
            const Texture2D tex = flow->iconForKind(kind);
            if (tex.id != 0)
            {
                const float pad = 4.0f;
                DrawTexturePro(
                    tex,
                    {0, 0, static_cast<float>(tex.width), static_cast<float>(tex.height)},
                    {tile.x + pad, tile.y + pad, tile.width - pad * 2.0f, tile.height - pad * 2.0f},
                    {0, 0},
                    0.0f,
                    WHITE);
            }
            else
            {
                const Font bold =
                    (uiFontBold.texture.id != 0 ? uiFontBold : font);
                const char* g = dialogNodeKindGlyph(kind);
                const float fs = std::max(14.0f, L.icon * 0.42f);
                const float gw = measureUiTextWidth(bold, g, fs);
                DrawTextEx(
                    bold,
                    g,
                    {tile.x + (tile.width - gw) * 0.5f,
                     tile.y + (tile.height - fs) * 0.35f},
                    fs,
                    1.0f,
                    kTextPrimary);
            }
        }

        // Left-aligned label under icon, clipped to column width (no overlap).
        const std::string label = fitLabel(font, dialogNodeKindLabel(kind), L.cellW);
        DrawTextEx(
            font,
            label.c_str(),
            {x, tile.y + L.icon + 3.0f},
            kFontTiny,
            1.0f,
            kTextPrimary);
    }

    if (hoverKind != DialogNodeKind::Count)
    {
        DrawTextEx(
            font,
            dialogNodeKindBrief(hoverKind),
            {bounds.x + 10.0f, bounds.y + bounds.height - 18.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
    }
    else
    {
        DrawTextEx(
            font,
            "Drag onto flowchart",
            {bounds.x + 10.0f, bounds.y + bounds.height - 18.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
    }
}

} // namespace timberline_editor
