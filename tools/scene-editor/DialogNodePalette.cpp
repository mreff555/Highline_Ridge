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
/** Wide column gutters so labels under icons do not collide. */
constexpr float kMinGapX = 28.0f;
constexpr float kGapY = 22.0f;
constexpr float kLabelH = 18.0f;
constexpr float kPad = 10.0f;
constexpr float kHeader = 28.0f;
constexpr float kFooter = 22.0f;
constexpr float kScrollBarW = kScrollBarSize;

int paletteRowCount()
{
    const int n = static_cast<int>(DialogNodeKind::Count);
    return (n + kCols - 1) / kCols;
}

struct PaletteLayout
{
    float icon = 48.0f;
    float cellW = 48.0f;
    float cellH = 66.0f;
    float gapX = kMinGapX;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float contentH = 0.0f;
    float viewH = 0.0f;
    Rectangle clip{0, 0, 0, 0};
    Rectangle scrollTrack{0, 0, 0, 0};
    bool showScroll = false;
};

PaletteLayout computeLayout(Rectangle bounds, float iconHint)
{
    PaletteLayout L;
    const int rows = std::max(1, paletteRowCount());
    const bool mayScroll = true; // reserve gutter so layout does not jump
    const float scrollGutter = mayScroll ? (kScrollBarW + 4.0f) : 0.0f;

    const float availW =
        std::max(1.0f, bounds.width - kPad * 2.0f - scrollGutter);
    L.gapX = kMinGapX;
    L.cellW = (availW - L.gapX * static_cast<float>(kCols - 1))
        / static_cast<float>(kCols);
    if (L.cellW < 36.0f)
    {
        L.gapX = 16.0f;
        L.cellW = (availW - L.gapX * static_cast<float>(kCols - 1))
            / static_cast<float>(kCols);
    }

    // Size icons from width — do not shrink to fit short panes; scroll instead (#63).
    L.icon = std::min({iconHint, L.cellW - 2.0f, 56.0f});
    if (L.icon < 28.0f)
        L.icon = 28.0f;

    L.cellH = L.icon + kLabelH + 4.0f;
    L.x0 = bounds.x + kPad;
    L.y0 = bounds.y + kHeader + kPad;
    L.contentH = static_cast<float>(rows) * L.cellH
        + static_cast<float>(rows - 1) * kGapY;
    L.viewH = std::max(
        1.0f, bounds.height - kHeader - kFooter - kPad * 2.0f);
    L.clip = {
        bounds.x + 1.0f,
        bounds.y + kHeader,
        std::max(1.0f, bounds.width - 2.0f),
        std::max(1.0f, bounds.height - kHeader - kFooter)};
    L.scrollTrack = {
        bounds.x + bounds.width - kPad - kScrollBarW,
        L.clip.y + 2.0f,
        kScrollBarW,
        std::max(1.0f, L.clip.height - 4.0f)};
    L.showScroll = L.contentH > L.viewH + 0.5f;
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
    {
        if (!editorMouseDown(MOUSE_BUTTON_LEFT))
            draggingScroll = false;
        return;
    }

    const PaletteLayout L = computeLayout(bounds, 64.0f);
    const float maxScroll = std::max(0.0f, L.contentH - L.viewH);
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
    {
        scrollY = std::clamp(scrollY - wheel * 28.0f, 0.0f, maxScroll);
    }

    if (L.showScroll && maxScroll > 0.0f)
    {
        const float thumbH =
            std::max(24.0f, L.scrollTrack.height * (L.viewH / L.contentH));
        const float thumbY = L.scrollTrack.y
            + (L.scrollTrack.height - thumbH) * (scrollY / maxScroll);
        const Rectangle thumb = {
            L.scrollTrack.x + 2.0f,
            thumbY,
            L.scrollTrack.width - 4.0f,
            thumbH};

        if (editorMousePressed(MOUSE_BUTTON_LEFT)
            && (CheckCollisionPointRec(mouse, thumb)
                || CheckCollisionPointRec(mouse, L.scrollTrack)))
        {
            draggingScroll = true;
        }
        if (!editorMouseDown(MOUSE_BUTTON_LEFT))
            draggingScroll = false;
        if (draggingScroll)
        {
            const float rel = (mouse.y - L.scrollTrack.y - thumbH * 0.5f)
                / std::max(1.0f, L.scrollTrack.height - thumbH);
            scrollY = std::clamp(rel, 0.0f, 1.0f) * maxScroll;
            return;
        }
    }
    else
    {
        draggingScroll = false;
    }

    if (!editorMousePressed(MOUSE_BUTTON_LEFT))
        return;

    flow->ensureIconsLoaded();

    for (int i = 0; i < static_cast<int>(DialogNodeKind::Count); ++i)
    {
        const int col = i % kCols;
        const int row = i / kCols;
        const float x = L.x0 + static_cast<float>(col) * (L.cellW + L.gapX);
        const float y =
            L.y0 + static_cast<float>(row) * (L.cellH + kGapY) - scrollY;
        const Rectangle cell = {x, y, L.cellW, L.cellH};
        if (!CheckCollisionPointRec(mouse, L.clip))
            continue;
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
    const float maxScroll = std::max(0.0f, L.contentH - L.viewH);
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);

    const Vector2 mouse = GetMousePosition();
    DialogNodeKind hoverKind = DialogNodeKind::Count;

    BeginScissorMode(
        static_cast<int>(L.clip.x),
        static_cast<int>(L.clip.y),
        static_cast<int>(L.clip.width),
        static_cast<int>(L.clip.height));

    for (int i = 0; i < static_cast<int>(DialogNodeKind::Count); ++i)
    {
        const DialogNodeKind kind = static_cast<DialogNodeKind>(i);
        const int col = i % kCols;
        const int row = i / kCols;
        const float x = L.x0 + static_cast<float>(col) * (L.cellW + L.gapX);
        const float y =
            L.y0 + static_cast<float>(row) * (L.cellH + kGapY) - scrollY;
        const Rectangle tile = {x, y, L.icon, L.icon};
        const Rectangle cell = {x, y, L.cellW, L.cellH};
        const bool hover = CheckCollisionPointRec(mouse, L.clip)
            && CheckCollisionPointRec(mouse, cell);
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
                    {tile.x + pad,
                     tile.y + pad,
                     tile.width - pad * 2.0f,
                     tile.height - pad * 2.0f},
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

        const std::string label =
            fitLabel(font, dialogNodeKindLabel(kind), L.cellW);
        DrawTextEx(
            font,
            label.c_str(),
            {x, tile.y + L.icon + 3.0f},
            kFontTiny,
            1.0f,
            kTextPrimary);
    }

    EndScissorMode();

    if (L.showScroll)
    {
        DrawRectangleRec(L.scrollTrack, kScrollTrack);
        if (maxScroll > 0.0f)
        {
            const float thumbH = std::max(
                24.0f, L.scrollTrack.height * (L.viewH / L.contentH));
            const float thumbY = L.scrollTrack.y
                + (L.scrollTrack.height - thumbH) * (scrollY / maxScroll);
            const Rectangle thumb = {
                L.scrollTrack.x + 2.0f,
                thumbY,
                L.scrollTrack.width - 4.0f,
                thumbH};
            DrawRectangleRec(
                thumb, draggingScroll ? kScrollThumbActive : kScrollThumb);
        }
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
            L.showScroll ? "Scroll for more · drag onto flowchart"
                         : "Drag onto flowchart",
            {bounds.x + 10.0f, bounds.y + bounds.height - 18.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
    }
}

} // namespace timberline_editor
