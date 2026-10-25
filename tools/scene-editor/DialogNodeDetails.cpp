/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations bottom-center pane: details for the selected flowchart node.
 ******************************************************************************/

#include "DialogNodeDetails.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
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

std::string signedEdgeToken(const char* key, int value)
{
    if (value == 0)
        return {};
    return std::string(key) + (value > 0 ? "+" : "") + std::to_string(value);
}

std::string fightEdgeSummary(const DialogFlowNode& n)
{
    std::string s;
    auto add = [&](const std::string& part) {
        if (part.empty())
            return;
        if (!s.empty())
            s += "  ";
        s += part;
    };
    add(signedEdgeToken("H", n.fightActorAdvHealth));
    add(signedEdgeToken("E", n.fightActorAdvEnergy));
    add(signedEdgeToken("R", n.fightActorAdvResolve));
    add(signedEdgeToken("L", n.fightActorAdvLucidity));
    add(signedEdgeToken("C", n.fightActorAdvCharisma));
    return s.empty() ? "even (all 0)" : s;
}

std::string signedFloatToken(const char* key, float value)
{
    if (value == 0.0f)
        return {};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(value));
    std::string num = buf;
    if (value > 0.0f)
        num.insert(num.begin(), '+');
    return std::string(key) + num;
}

std::string triggerEventDeltaPreview(const DialogFlowNode& n)
{
    std::string s;
    auto add = [&](const std::string& part) {
        if (part.empty())
            return;
        if (!s.empty())
            s += "  ";
        s += part;
    };
    add(signedFloatToken("H", n.eventStatusHealth));
    add(signedFloatToken("E", n.eventStatusEnergy));
    add(signedFloatToken("R", n.eventStatusResolve));
    add(signedFloatToken("L", n.eventStatusLucidity));
    add(signedFloatToken("C", n.eventStatusCharisma));
    add(signedFloatToken("$", n.eventStatusMoney));
    if (n.eventTabDelta != 0.0f && !n.eventTabActor.empty())
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(n.eventTabDelta));
        std::string num = buf;
        if (n.eventTabDelta > 0.0f)
            num.insert(num.begin(), '+');
        add("tab " + n.eventTabActor + " " + num);
    }
    int opinionCount = 0;
    for (const EventOpinionRow& row : n.eventOpinions)
    {
        if (!row.actorId.empty() && row.delta != 0)
            ++opinionCount;
    }
    if (opinionCount == 1)
    {
        for (const EventOpinionRow& row : n.eventOpinions)
        {
            if (row.actorId.empty() || row.delta == 0)
                continue;
            add(row.actorId + " " + (row.delta > 0 ? "+" : "") + std::to_string(row.delta));
            break;
        }
    }
    else if (opinionCount > 1)
        add(std::to_string(opinionCount) + " reputation");
    if (!n.eventStatusKey.empty())
        add("once " + n.eventStatusKey);
    if (!n.eventOnZeroLucidity.empty())
        add("on0L " + n.eventOnZeroLucidity);
    return s;
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

void DialogNodeDetails::clearHits()
{
    hitTargets.clear();
}

void DialogNodeDetails::addHit(HitKind kind, Rectangle rect, int nodeId)
{
    hitTargets.push_back({kind, rect, nodeId});
}

void DialogNodeDetails::handleInput(Rectangle bounds, bool allowInteraction)
{
    if (!allowInteraction || flow == nullptr)
        return;
    if (flow->parchment != nullptr && flow->parchment->blocksInput())
        return;

    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, bounds))
        return;

    const float wheel = GetMouseWheelMove();
    if (std::fabs(wheel) > 0.001f)
        scrollY = std::max(0.0f, scrollY - wheel * 28.0f);

    if (!CheckCollisionPointRec(mouse, lastClip) && lastClip.width > 1.0f)
        return;

    const bool right = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    const bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (!right && !left)
        return;

    for (const HitTarget& hit : hitTargets)
    {
        if (!CheckCollisionPointRec(mouse, hit.rect))
            continue;

        switch (hit.kind)
        {
        case HitKind::OpenText:
            if (right)
            {
                if (DialogFlowNode* n = flow->findNode(hit.nodeId))
                {
                    const char* label =
                        n->kind == DialogNodeKind::ActorInventory ? "Opening text"
                        : n->kind == DialogNodeKind::DialogEnd    ? "Dialog end text"
                                                                  : "Actor dialog text";
                    flow->openDetailsParchment(
                        hit.nodeId, &n->dialogText, false, label);
                }
            }
            return;
        case HitKind::OpenTts:
            if (right)
            {
                if (DialogFlowNode* n = flow->findNode(hit.nodeId))
                {
                    const char* label =
                        n->kind == DialogNodeKind::ActorInventory ? "Opening TTS"
                        : n->kind == DialogNodeKind::DialogEnd    ? "Dialog end TTS"
                                                                  : "Actor dialog TTS";
                    flow->openDetailsParchment(
                        hit.nodeId,
                        &n->dialogTts,
                        true,
                        label,
                        n->dialogText);
                }
            }
            return;
        case HitKind::OpenPlayerLine:
            if (right)
            {
                if (DialogFlowNode* n = flow->findNode(hit.nodeId))
                {
                    flow->openDetailsParchment(
                        hit.nodeId,
                        &n->playerDialogText,
                        false,
                        "Player dialog text");
                }
            }
            return;
        case HitKind::AddPlayerResponse:
            if (left)
                flow->addPlayerResponseUnder(hit.nodeId);
            return;
        case HitKind::RemovePlayerResponse:
            if (left)
                flow->removePlayerResponse(flow->selectedNodeId, hit.nodeId);
            return;
        case HitKind::OpenEditFight:
            if (left)
            {
                if (DialogFlowNode* n = flow->findNode(hit.nodeId))
                {
                    if (onEditFight)
                        onEditFight(n);
                    else if (flow->onEditFight)
                        flow->onEditFight(n);
                }
            }
            return;
        case HitKind::OpenEditActorDialog:
            if (left)
            {
                if (DialogFlowNode* n = flow->findNode(hit.nodeId))
                {
                    if (onEditActorDialog)
                        onEditActorDialog(n);
                    else if (flow->onEditActorDialog)
                        flow->onEditActorDialog(n);
                }
            }
            return;
        case HitKind::OpenEditEvent:
            if (left)
            {
                if (DialogFlowNode* n = flow->findNode(hit.nodeId))
                {
                    if (onEditEvent)
                        onEditEvent(n);
                    else if (flow->onEditEvent)
                        flow->onEditEvent(n);
                }
            }
            return;
        default:
            break;
        }
    }
}

void DialogNodeDetails::draw(Rectangle bounds)
{
    rebuildAndDraw(bounds);
}

void DialogNodeDetails::rebuildAndDraw(Rectangle bounds)
{
    clearHits();

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
        lastClip = {};
        return;
    }

    const int sel = flow->selectedNodeId;
    if (sel != lastSelectedId)
    {
        lastSelectedId = sel;
        scrollY = 0.0f;
    }

    const Rectangle clip = {
        bounds.x + 1.0f,
        bounds.y + kHeader,
        bounds.width - 2.0f,
        std::max(1.0f, bounds.height - kHeader - 4.0f)};
    lastClip = clip;

    BeginScissorMode(
        static_cast<int>(clip.x),
        static_cast<int>(clip.y),
        static_cast<int>(clip.width),
        static_cast<int>(clip.height));

    const float maxW = clip.width - kPad * 2.0f;
    float y = clip.y + 4.0f - scrollY;
    float contentH = 0.0f;

    auto advance = [&](float h) {
        y += h;
        contentH += h;
    };

    auto drawMutedLine = [&](const std::string& text) {
        drawWrappedText(
            font,
            text,
            {clip.x + kPad, y},
            maxW,
            kFontSmall,
            kLineGap,
            kTextMuted);
        const float approxLines = std::max(
            1.0f,
            std::ceil(
                measureUiTextWidth(font, text, kFontSmall)
                / std::max(1.0f, maxW)));
        advance(approxLines * (kFontSmall + kLineGap) + 6.0f);
    };

    auto drawLabeledRow =
        [&](const char* label,
            const std::string& value,
            bool muted,
            HitKind hitKind,
            int nodeId) {
            const std::string lab = std::string(label) + ": ";
            const float labelW = measureUiTextWidth(font, lab, kFontSmall);
            DrawTextEx(
                font,
                lab.c_str(),
                {clip.x + kPad, y},
                kFontSmall,
                1.0f,
                kTextMuted);

            const float valueMax = std::max(20.0f, maxW - labelW);
            const std::string shown =
                ellipsize(font, value, valueMax, kFontSmall);
            DrawTextEx(
                font,
                shown.c_str(),
                {clip.x + kPad + labelW, y},
                kFontSmall,
                1.0f,
                muted ? kTextMuted : kTextPrimary);

            if (hitKind != HitKind::None && nodeId > 0)
            {
                const Rectangle hit = {
                    clip.x + kPad,
                    y - 1.0f,
                    maxW,
                    kFontSmall + 4.0f};
                addHit(hitKind, hit, nodeId);
            }
            advance(kFontSmall + 8.0f);
        };

    if (sel < 0)
    {
        drawMutedLine("Select a node on the flowchart");
    }
    else if (sel == 0)
    {
        drawLabeledRow("Interaction type", "Start", false, HitKind::None, 0);
        drawMutedLine(
            "Immobile entry compass — one child port down into the speak graph.");
    }
    else if (const DialogFlowNode* nConst = flow->findNode(sel))
    {
        // Non-const for fight toggles during draw? We only read here.
        const DialogFlowNode* n = nConst;

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
            advance(icon + 8.0f);
        }

        drawLabeledRow(
            "Interaction type",
            dialogNodeKindLabel(n->kind),
            false,
            HitKind::None,
            0);
        {
            const char* brief = dialogNodeKindBrief(n->kind);
            if (brief != nullptr && brief[0] != '\0')
                drawMutedLine(brief);
        }

        switch (n->kind)
        {
        case DialogNodeKind::ActorDialog:
        {
            const std::string idLabel = n->lineId.empty()
                ? (n->sourcePhaseId.empty() ? "(unset)" : n->sourcePhaseId)
                : n->lineId;
            drawLabeledRow("Id", idLabel, n->lineId.empty() && n->sourcePhaseId.empty(),
                HitKind::None, 0);
            if (!n->actorId.empty() || !n->actorName.empty())
            {
                const std::string actor = n->actorName.empty()
                    ? n->actorId
                    : (n->actorId.empty() ? n->actorName
                                          : n->actorName + " (" + n->actorId + ")");
                drawLabeledRow("Actor", actor, false, HitKind::None, 0);
            }
            drawLabeledRow(
                "Text",
                n->dialogText.empty() ? "(empty)" : firstLinePreview(n->dialogText),
                n->dialogText.empty(),
                HitKind::None,
                0);
            drawLabeledRow(
                "Voice",
                n->defaultVoice.empty() ? "Off" : n->defaultVoice,
                false,
                HitKind::None,
                0);
            if (!n->resumeIntroText.empty())
                drawLabeledRow(
                    "Resume",
                    firstLinePreview(n->resumeIntroText),
                    false,
                    HitKind::None,
                    0);
            {
                const auto responses = flow->playerResponsesUnder(n->id);
                const std::string resp = n->playerResponsesEnabled
                    ? ("On (" + std::to_string(responses.size()) + " wired, 5 ports)")
                    : "Off (1 port)";
                drawLabeledRow("Player responses", resp, false, HitKind::None, 0);
            }
            const Rectangle editBtn = {
                clip.x + kPad, y, std::max(80.0f, maxW), 28.0f};
            drawEditorButton(font, editBtn, "Edit Actor Dialog...", true, true);
            addHit(HitKind::OpenEditActorDialog, editBtn, n->id);
            advance(36.0f);
            drawMutedLine(
                n->playerResponsesEnabled
                    ? "Ports: 5 player responses. Dual-writes conversations.json."
                    : "Ports: 1 out. Dual-writes conversations.json.");
            break;
        }
        case DialogNodeKind::DialogEnd:
        {
            const std::string textPrev =
                n->dialogText.empty() ? "(empty)"
                                      : firstLinePreview(n->dialogText);
            drawLabeledRow(
                "Text",
                textPrev,
                n->dialogText.empty(),
                HitKind::OpenText,
                n->id);
            drawLabeledRow(
                "TTS",
                n->dialogTts.empty() ? "(empty — right-click to edit)"
                                     : firstLinePreview(n->dialogTts),
                n->dialogTts.empty(),
                HitKind::OpenTts,
                n->id);
            drawLabeledRow(
                "Voice",
                n->defaultVoice.empty() ? "Off" : n->defaultVoice,
                false,
                HitKind::None,
                0);
            if (!n->dialogTtsAudio.empty())
                drawLabeledRow("Audio", n->dialogTtsAudio, false, HitKind::None, 0);
            drawMutedLine(
                "Ends the conversation after the optional line (0 out ports).");
            break;
        }
        case DialogNodeKind::PlayerDialog:
        {
            const std::string linePrev =
                n->playerDialogText.empty() ? "(empty)"
                                            : firstLinePreview(n->playerDialogText);
            drawLabeledRow(
                "Line",
                linePrev,
                n->playerDialogText.empty(),
                HitKind::OpenPlayerLine,
                n->id);
            drawLabeledRow(
                "TTS",
                n->dialogTts.empty() ? "(empty — right-click to edit)"
                                     : firstLinePreview(n->dialogTts),
                n->dialogTts.empty(),
                HitKind::OpenTts,
                n->id);
            drawLabeledRow(
                "Voice",
                n->defaultVoice.empty() ? "Off" : n->defaultVoice,
                false,
                HitKind::None,
                0);
            if (!n->dialogTtsAudio.empty())
                drawLabeledRow("Audio", n->dialogTtsAudio, false, HitKind::None, 0);
            if (!flow->nodeHasOutgoing(n->id))
                drawMutedLine("No follow-on wired from this player line.");
            break;
        }
        case DialogNodeKind::Event:
            if (!n->eventId.empty())
                drawLabeledRow("Gate", n->eventId, false, HitKind::None, 0);
            else
                drawLabeledRow("Gate", "(unset event id)", true, HitKind::None, 0);
            drawMutedLine("Ports: pass (0) / fail (1)");
            break;
        case DialogNodeKind::GiveItem:
        case DialogNodeKind::TakeItem:
        {
            drawLabeledRow(
                "Direction",
                n->kind == DialogNodeKind::GiveItem ? "Actor → player"
                                                    : "Player → actor",
                false,
                HitKind::None,
                0);
            if (!n->itemId.empty())
                drawLabeledRow("Item", n->itemId, false, HitKind::None, 0);
            else
                drawLabeledRow("Item", "(none — money-only ok)", true, HitKind::None, 0);
            drawLabeledRow(
                "Qty",
                std::to_string(std::max(1, n->exchangeQuantity)),
                false,
                HitKind::None,
                0);
            {
                std::ostringstream oss;
                oss << "$" << std::fixed << std::setprecision(2) << n->exchangeMoney;
                drawLabeledRow("Money", oss.str(), false, HitKind::None, 0);
            }
            drawLabeledRow(
                "Actor bag",
                n->useActorInventory ? "yes" : "no",
                false,
                HitKind::None,
                0);
            if (!n->dialogText.empty())
            {
                drawLabeledRow(
                    "Opening",
                    firstLinePreview(n->dialogText),
                    false,
                    HitKind::OpenText,
                    n->id);
            }
            drawMutedLine("Bottom ports: green = success, red = fail.");
            break;
        }
        case DialogNodeKind::ActorInventory:
            if (!n->inventoryActorId.empty())
                drawLabeledRow(
                    "Actor", n->inventoryActorId, false, HitKind::None, 0);
            else
                drawLabeledRow("Actor", "(unset bag id)", true, HitKind::None, 0);
            {
                const std::string openPrev =
                    n->dialogText.empty() ? "(empty)"
                                          : firstLinePreview(n->dialogText);
                drawLabeledRow(
                    "Opening",
                    openPrev,
                    n->dialogText.empty(),
                    HitKind::OpenText,
                    n->id);
                drawLabeledRow(
                    "TTS",
                    n->dialogTts.empty() ? "(empty)"
                                         : firstLinePreview(n->dialogTts),
                    n->dialogTts.empty(),
                    HitKind::OpenTts,
                    n->id);
            }
            if (!n->dialogTtsAudio.empty())
                drawLabeledRow("Audio", n->dialogTtsAudio, false, HitKind::None, 0);
            drawMutedLine(
                "Runtime synthesizes priced browse/buy choices from actorInventories.");
            break;
        case DialogNodeKind::Attack:
        {
            const std::string enc =
                n->fightEncounterId.empty() ? n->combatantId : n->fightEncounterId;
            drawLabeledRow(
                "Combatant",
                n->combatantId.empty() ? "(unset)" : n->combatantId,
                n->combatantId.empty(),
                HitKind::None,
                0);
            drawLabeledRow(
                "Encounter",
                enc.empty() ? "(unset)" : enc,
                enc.empty(),
                HitKind::None,
                0);
            drawLabeledRow(
                "Lead-up",
                n->fightLeadText.empty() ? "(empty)"
                                         : firstLinePreview(n->fightLeadText),
                n->fightLeadText.empty(),
                HitKind::None,
                0);
            drawLabeledRow(
                "Win",
                n->fightWinText.empty() ? "(empty)"
                                        : firstLinePreview(n->fightWinText),
                n->fightWinText.empty(),
                HitKind::None,
                0);
            drawLabeledRow(
                "Lose",
                n->fightLoseText.empty() ? "(empty)"
                                         : firstLinePreview(n->fightLoseText),
                n->fightLoseText.empty(),
                HitKind::None,
                0);
            drawLabeledRow(
                "Actor edge", fightEdgeSummary(*n), false, HitKind::None, 0);
            if (n->fightCannotLose)
                drawMutedLine("Cannot lose.");
            else if (n->fightPlayerDeath)
                drawMutedLine("Player death on lose.");
            if (n->fightReputationChange)
                drawLabeledRow(
                    "Reputation",
                    std::to_string(n->fightReputationDelta),
                    false,
                    HitKind::None,
                    0);
            const Rectangle editBtn = {
                clip.x + kPad, y, std::max(80.0f, maxW), 28.0f};
            drawEditorButton(font, editBtn, "Edit Fight...", true, true);
            addHit(HitKind::OpenEditFight, editBtn, n->id);
            advance(36.0f);
            drawMutedLine("Ports: win (0) / lose (1). Dual-writes attackEncounters.");
            break;
        }
        case DialogNodeKind::TriggerEvent:
        {
            if (!n->eventId.empty())
                drawLabeledRow("Flag", n->eventId, false, HitKind::None, 0);
            else
                drawLabeledRow("Flag", "(unset)", true, HitKind::None, 0);
            {
                const std::string preview = triggerEventDeltaPreview(*n);
                drawLabeledRow(
                    "Effects",
                    preview.empty() ? "(none)" : preview,
                    preview.empty(),
                    HitKind::None,
                    0);
            }
            const Rectangle editBtn = {
                clip.x + kPad, y, std::max(80.0f, maxW), 28.0f};
            drawEditorButton(font, editBtn, "Edit Event...", true, true);
            addHit(HitKind::OpenEditEvent, editBtn, n->id);
            advance(36.0f);
            drawMutedLine(
                "Fires grantStoryFlag and applies status on the parent choice/line.");
            break;
        }
        default:
            break;
        }

        if (!n->sourcePhaseId.empty())
            drawLabeledRow(
                "Source phase", n->sourcePhaseId, false, HitKind::None, 0);
        if (!n->sourceChoiceId.empty())
            drawLabeledRow("Choice", n->sourceChoiceId, false, HitKind::None, 0);
        if (!n->jsonPointer.empty())
            drawLabeledRow("Pointer", n->jsonPointer, false, HitKind::None, 0);
    }
    else
    {
        drawMutedLine("Select a node on the flowchart");
    }

    EndScissorMode();

    const float maxScroll = std::max(0.0f, contentH - clip.height + 8.0f);
    if (scrollY > maxScroll)
        scrollY = maxScroll;
}

} // namespace timberline_editor
