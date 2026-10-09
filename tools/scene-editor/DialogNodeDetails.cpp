/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations bottom-center pane: details for the selected flowchart node.
 ******************************************************************************/

#include "DialogNodeDetails.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace timberline_editor
{

namespace
{
constexpr float kGap = 2.0f;
/** Types / details / media — types+media shrunk to fit the middle pane. */
constexpr float kTypesRatio = 0.24f;
constexpr float kDetailsRatio = 0.34f;
constexpr float kPad = 10.0f;
constexpr float kHeader = 28.0f;
constexpr float kLineGap = 4.0f;

std::string ellipsize(Font font, const std::string& text, float maxW, float fontSize)
{
    if (text.empty() || measureUiTextWidth(font, text, fontSize) <= maxW)
        return text;
    std::string s = text;
    while (s.size() > 3
           && measureUiTextWidth(font, s + "...", fontSize) > maxW)
        s.pop_back();
    return s + "...";
}

std::string firstLinePreview(const std::string& text, size_t maxChars = 160)
{
    std::string out;
    out.reserve(std::min(text.size(), maxChars));
    for (char ch : text)
    {
        if (ch == '\n' || ch == '\r')
        {
            if (!out.empty())
                break;
            continue;
        }
        out.push_back(ch);
        if (out.size() >= maxChars)
            break;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t'))
        out.pop_back();
    if (text.size() > out.size())
        out += "…";
    return out;
}

struct DetailRow
{
    std::string label;
    std::string value;
    bool muted = false;
};

void appendRow(std::vector<DetailRow>& rows, const char* label, const std::string& value)
{
    if (value.empty())
        return;
    rows.push_back({label, value, false});
}

void appendEmptyHint(std::vector<DetailRow>& rows, const char* label, const char* hint)
{
    rows.push_back({label, hint, true});
}
} // namespace

DialogBottomSplit computeDialogBottomSplit(Rectangle bottomBounds)
{
    DialogBottomSplit out;
    const float inner = std::max(0.0f, bottomBounds.width - kGap * 2.0f);
    const float typesW = inner * kTypesRatio;
    const float detailsW = inner * kDetailsRatio;
    const float mediaW = std::max(0.0f, inner - typesW - detailsW);

    out.types = {bottomBounds.x, bottomBounds.y, typesW, bottomBounds.height};
    out.details = {
        out.types.x + out.types.width + kGap,
        bottomBounds.y,
        detailsW,
        bottomBounds.height};
    out.media = {
        out.details.x + out.details.width + kGap,
        bottomBounds.y,
        mediaW,
        bottomBounds.height};
    return out;
}

void DialogNodeDetails::handleInput(Rectangle bounds, bool allowInteraction)
{
    if (!allowInteraction)
        return;
    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, bounds))
        return;

    const float wheel = GetMouseWheelMove();
    if (std::fabs(wheel) > 0.001f)
        scrollY = std::max(0.0f, scrollY - wheel * 28.0f);
}

void DialogNodeDetails::draw(Rectangle bounds)
{
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);

    DrawRectangleRec(bounds, kPanelFill);
    DrawRectangleLinesEx(bounds, 1.0f, kPanelInnerEdge);

    DrawTextEx(
        font,
        "Dialog details",
        {bounds.x + kPad, bounds.y + 8.0f},
        kFontLabel,
        1.0f,
        kTextMuted);

    if (flow == nullptr)
    {
        DrawTextEx(
            font,
            "Flowchart not wired",
            {bounds.x + kPad, bounds.y + kHeader},
            kFontBody,
            1.0f,
            kTextMuted);
        return;
    }

    const int sel = flow->selectedNodeId;
    if (sel != lastSelectedId)
    {
        lastSelectedId = sel;
        scrollY = 0.0f;
    }

    std::vector<DetailRow> rows;

    if (sel < 0)
    {
        rows.push_back({"", "Select a node on the flowchart", true});
    }
    else if (sel == 0)
    {
        rows.push_back({"Kind", "Start", false});
        rows.push_back(
            {"", "Immobile entry compass — one child port down into the speak graph.", true});
    }
    else if (const DialogFlowNode* n = flow->findNode(sel))
    {
        rows.push_back({"Kind", dialogNodeKindLabel(n->kind), false});
        rows.push_back({"", dialogNodeKindBrief(n->kind), true});

        switch (n->kind)
        {
        case DialogNodeKind::ActorDialog:
            if (!n->dialogText.empty())
                appendRow(rows, "Text", firstLinePreview(n->dialogText));
            else
                appendEmptyHint(rows, "Text", "(empty)");
            if (!n->dialogTts.empty())
                appendRow(rows, "TTS", firstLinePreview(n->dialogTts));
            appendRow(
                rows,
                "Voice",
                n->defaultVoice.empty() ? "Off" : n->defaultVoice);
            appendRow(rows, "Audio", n->dialogTtsAudio);
            if (!n->resumeIntroText.empty())
            {
                appendRow(rows, "Resume", firstLinePreview(n->resumeIntroText));
                if (!n->resumeIntroTts.empty())
                    appendRow(rows, "Resume TTS", firstLinePreview(n->resumeIntroTts));
                appendRow(
                    rows,
                    "Resume voice",
                    n->resumeIntroVoice.empty() ? "Off" : n->resumeIntroVoice);
                rows.push_back(
                    {"",
                     "Resume plays only when revisiting remaining top-level choices — not on first Speak.",
                     true});
            }
            break;
        case DialogNodeKind::PlayerDialog:
            if (!n->playerDialogText.empty())
                appendRow(rows, "Line", firstLinePreview(n->playerDialogText));
            else
                appendEmptyHint(rows, "Line", "(empty)");
            appendRow(
                rows,
                "Voice",
                n->defaultVoice.empty() ? "Off" : n->defaultVoice);
            appendRow(rows, "Audio", n->dialogTtsAudio);
            break;
        case DialogNodeKind::Event:
            if (!n->eventId.empty())
                appendRow(rows, "Gate", n->eventId);
            else
                appendEmptyHint(rows, "Gate", "(unset event id)");
            rows.push_back(
                {"Ports", "pass (0) / fail (1)", true});
            break;
        case DialogNodeKind::GetItem:
            if (!n->itemId.empty())
                appendRow(rows, "Item", n->itemId);
            else
                appendEmptyHint(rows, "Item", "(unset)");
            break;
        case DialogNodeKind::Attack:
            if (!n->combatantId.empty())
                appendRow(rows, "Combatant", n->combatantId);
            else
                appendEmptyHint(rows, "Combatant", "(unset)");
            appendRow(
                rows,
                "Death",
                n->playerDeathPossible ? "possible" : "10% HP floor");
            if (!n->winModifiers.empty())
            {
                std::ostringstream oss;
                oss << n->winModifiers.size() << " modifier"
                    << (n->winModifiers.size() == 1 ? "" : "s");
                appendRow(rows, "Win", oss.str());
            }
            break;
        case DialogNodeKind::TriggerEvent:
            if (!n->eventId.empty())
                appendRow(rows, "Event", n->eventId);
            else
                appendEmptyHint(rows, "Event", "(unset)");
            break;
        default:
            break;
        }

        appendRow(rows, "Phase", n->sourcePhaseId);
        appendRow(rows, "Choice", n->sourceChoiceId);
        appendRow(rows, "Pointer", n->jsonPointer);
    }
    else
    {
        rows.push_back({"", "Select a node on the flowchart", true});
    }

    const Rectangle clip = {
        bounds.x + 1.0f,
        bounds.y + kHeader,
        bounds.width - 2.0f,
        std::max(1.0f, bounds.height - kHeader - 4.0f)};
    BeginScissorMode(
        static_cast<int>(clip.x),
        static_cast<int>(clip.y),
        static_cast<int>(clip.width),
        static_cast<int>(clip.height));

    const float maxW = clip.width - kPad * 2.0f;
    float y = clip.y + 4.0f - scrollY;
    float contentH = 0.0f;

    // Kind icon badge when a real node is selected.
    if (sel > 0)
    {
        if (const DialogFlowNode* n = flow->findNode(sel))
        {
            flow->ensureIconsLoaded();
            const float icon = 36.0f;
            const Rectangle tile = {clip.x + kPad, y, icon, icon};
            DrawRectangleRounded(tile, 0.16f, 6, Color{40, 36, 48, 255});
            DrawRectangleRoundedLines(tile, 0.16f, 6, kPanelAccent);
            const Texture2D tex = flow->iconForKind(n->kind);
            if (tex.id != 0)
            {
                const float pad = 3.0f;
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
                const char* g = dialogNodeKindGlyph(n->kind);
                const float fs = 16.0f;
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
            y += icon + 8.0f;
            contentH += icon + 8.0f;
        }
    }

    for (const DetailRow& row : rows)
    {
        if (row.label.empty())
        {
            drawWrappedText(
                font,
                row.value,
                {clip.x + kPad, y},
                maxW,
                kFontSmall,
                kLineGap,
                row.muted ? kTextMuted : kTextPrimary);
            // Approximate height for scroll (wrapped lines ~ font + gap).
            const float approxLines = std::max(
                1.0f,
                std::ceil(
                    measureUiTextWidth(font, row.value, kFontSmall)
                    / std::max(1.0f, maxW)));
            const float h = approxLines * (kFontSmall + kLineGap) + 6.0f;
            y += h;
            contentH += h;
            continue;
        }

        const std::string label = row.label + ": ";
        const float labelW = measureUiTextWidth(font, label, kFontSmall);
        DrawTextEx(
            font,
            label.c_str(),
            {clip.x + kPad, y},
            kFontSmall,
            1.0f,
            kTextMuted);

        const float valueMax = std::max(20.0f, maxW - labelW);
        const std::string value =
            ellipsize(font, row.value, valueMax, kFontSmall);
        DrawTextEx(
            font,
            value.c_str(),
            {clip.x + kPad + labelW, y},
            kFontSmall,
            1.0f,
            row.muted ? kTextMuted : kTextPrimary);
        y += kFontSmall + 8.0f;
        contentH += kFontSmall + 8.0f;
    }

    EndScissorMode();

    const float maxScroll = std::max(0.0f, contentH - clip.height + 8.0f);
    if (scrollY > maxScroll)
        scrollY = maxScroll;
}

} // namespace timberline_editor
