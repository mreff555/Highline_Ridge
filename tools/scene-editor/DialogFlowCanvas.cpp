/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Conversations flowchart canvas (Phase 1 shell): Start compass + stub nodes.
 ******************************************************************************/

#include "DialogFlowCanvas.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "PlatformPath.h"
#include "TtsVoiceMarkup.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <sstream>
#include <vector>

using timberline_engine::builtinVoiceIds;
using timberline_engine::isKnownBuiltinVoiceId;
using timberline_engine::normalizeVoiceId;

namespace timberline_editor
{

namespace
{
constexpr float kStartWorldX = 120.0f;
constexpr float kStartWorldY = 48.0f;
constexpr float kPortHitSlop = 12.0f;
constexpr float kContextRowH = 26.0f;
constexpr float kVoiceRowH = 22.0f;
}

void DialogFlowCanvas::clearGraph()
{
    nodes.clear();
    edges.clear();
    nextNodeId = 1;
    selectedNodeId = -1;
    placingFromPalette = false;
    wiring = false;
    draggingNode = false;
    dragNodeId = -1;
    closeContextMenu();
    status.clear();
    migratedScope.clear();
    scrollX = 0.0f;
    scrollY = 0.0f;
}

void DialogFlowCanvas::beginPlaceFromPalette(DialogNodeKind kind)
{
    placingFromPalette = true;
    placeKind = kind;
    wiring = false;
    draggingNode = false;
    closeContextMenu();
    status = std::string("Drop ") + dialogNodeKindLabel(kind) + " on canvas";
}

void DialogFlowCanvas::ensureIconsLoaded()
{
    if (docs == nullptr)
        return;
    const std::string dir = docs->resourceDir;
    if (iconsLoaded && iconsResourceDir == dir)
        return;
    unloadIcons();
    iconsResourceDir = dir;
    using timberline_engine::pathJoin;
    for (int i = 0; i < static_cast<int>(DialogNodeKind::Count); ++i)
    {
        const DialogNodeKind kind = static_cast<DialogNodeKind>(i);
        const char* file = dialogNodeKindIconFile(kind);
        if (file == nullptr || file[0] == '\0')
            continue;
        const std::string path =
            pathJoin(pathJoin(dir, "ui/editor/dialog_nodes"), file);
        if (FileExists(path.c_str()))
        {
            kindIcons[static_cast<size_t>(i)] = LoadTexture(path.c_str());
            if (kindIcons[static_cast<size_t>(i)].id != 0)
                SetTextureFilter(kindIcons[static_cast<size_t>(i)], TEXTURE_FILTER_BILINEAR);
        }
    }
    iconsLoaded = true;
}

void DialogFlowCanvas::unloadIcons()
{
    for (Texture2D& t : kindIcons)
    {
        if (t.id != 0)
            UnloadTexture(t);
        t = {};
    }
    iconsLoaded = false;
}

Texture2D DialogFlowCanvas::iconForKind(DialogNodeKind kind) const
{
    const size_t i = static_cast<size_t>(kind);
    if (i >= kindIcons.size())
        return {};
    return kindIcons[i];
}

DialogFlowNode* DialogFlowCanvas::findNode(int id)
{
    for (DialogFlowNode& n : nodes)
    {
        if (n.id == id)
            return &n;
    }
    return nullptr;
}

const DialogFlowNode* DialogFlowCanvas::findNode(int id) const
{
    for (const DialogFlowNode& n : nodes)
    {
        if (n.id == id)
            return &n;
    }
    return nullptr;
}

Vector2 DialogFlowCanvas::worldFromScreen(Vector2 screen) const
{
    return {
        screen.x - lastBounds.x + scrollX,
        screen.y - lastBounds.y + scrollY};
}

Vector2 DialogFlowCanvas::screenFromWorld(Vector2 world) const
{
    return {
        world.x - scrollX + lastBounds.x,
        world.y - scrollY + lastBounds.y};
}

Rectangle DialogFlowCanvas::startCardWorld() const
{
    return {kStartWorldX, kStartWorldY, kStartSize, kStartSize};
}

Rectangle DialogFlowCanvas::nodeCardWorld(const DialogFlowNode& n) const
{
    return {n.x, n.y, kNodeW, kNodeH};
}

Vector2 DialogFlowCanvas::startChildPortWorld() const
{
    const Rectangle card = startCardWorld();
    return {card.x + card.width * 0.5f, card.y + card.height + 2.0f};
}

Vector2 DialogFlowCanvas::parentPortWorld(const DialogFlowNode& n) const
{
    const Rectangle card = nodeCardWorld(n);
    return {card.x + card.width * 0.5f, card.y - 2.0f};
}

Vector2 DialogFlowCanvas::childPortWorld(int nodeId, int childIndex) const
{
    if (nodeId == 0)
        return startChildPortWorld();

    const DialogFlowNode* node = findNode(nodeId);
    if (node == nullptr)
        return {0, 0};

    const Rectangle card = nodeCardWorld(*node);
    const DialogChildSlot slot = dialogChildSlotForIndex(childIndex);
    switch (slot)
    {
    case DialogChildSlot::Bottom:
        return {card.x + card.width * 0.5f, card.y + card.height + 2.0f};
    case DialogChildSlot::BottomLeft:
        return {card.x + 10.0f, card.y + card.height + 2.0f};
    case DialogChildSlot::BottomRight:
        return {card.x + card.width - 10.0f, card.y + card.height + 2.0f};
    case DialogChildSlot::Left:
        return {card.x - 2.0f, card.y + card.height * 0.55f};
    case DialogChildSlot::Right:
        return {card.x + card.width + 2.0f, card.y + card.height * 0.55f};
    }
    return {card.x + card.width * 0.5f, card.y + card.height + 2.0f};
}

void DialogFlowCanvas::drawCompassRose(Vector2 center, float radius) const
{
    const Color ring = {168, 138, 72, 255};
    const Color fill = {36, 32, 42, 255};
    const Color needle = {210, 120, 90, 255};
    DrawCircleV(center, radius, fill);
    DrawCircleLinesV(center, radius, ring);
    DrawCircleLinesV(center, radius * 0.72f, Color{110, 92, 52, 200});

    for (int i = 0; i < 4; ++i)
    {
        const float a = static_cast<float>(i) * (3.14159265f * 0.5f) - 3.14159265f * 0.5f;
        const Vector2 outer = {
            center.x + std::cos(a) * radius,
            center.y + std::sin(a) * radius};
        const Vector2 inner = {
            center.x + std::cos(a) * radius * 0.55f,
            center.y + std::sin(a) * radius * 0.55f};
        DrawLineEx(inner, outer, 2.0f, ring);
    }

    {
        const Vector2 tip = {center.x, center.y + radius * 0.92f};
        const Vector2 left = {center.x - 6.0f, center.y + radius * 0.45f};
        const Vector2 right = {center.x + 6.0f, center.y + radius * 0.45f};
        DrawTriangle(tip, left, right, needle);
    }

    DrawCircleV(center, 4.0f, ring);

    const Font font = (uiFontBold.texture.id != 0 ? uiFontBold
                      : (uiFont.texture.id != 0 ? uiFont : GetFontDefault()));
    const char* label = "START";
    const float fs = kFontTiny;
    const float tw = measureUiTextWidth(font, label, fs);
    DrawTextEx(
        font,
        label,
        {center.x - tw * 0.5f, center.y - radius - 16.0f},
        fs,
        1.0f,
        kPanelBorder);
}

void DialogFlowCanvas::drawKindIcon(DialogNodeKind kind, Rectangle dest) const
{
    const Texture2D tex = iconForKind(kind);
    if (tex.id != 0)
    {
        DrawTexturePro(
            tex,
            {0, 0, static_cast<float>(tex.width), static_cast<float>(tex.height)},
            dest,
            {0, 0},
            0.0f,
            WHITE);
        return;
    }
    // Fallback glyph
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold
                     : (uiFont.texture.id != 0 ? uiFont : GetFontDefault()));
    DrawRectangleRounded(dest, 0.2f, 4, Color{48, 42, 54, 255});
    const char* g = dialogNodeKindGlyph(kind);
    const float fs = dest.height * 0.45f;
    const float gw = measureUiTextWidth(bold, g, fs);
    DrawTextEx(
        bold,
        g,
        {dest.x + (dest.width - gw) * 0.5f, dest.y + (dest.height - fs) * 0.5f},
        fs,
        1.0f,
        kTextPrimary);
}

bool DialogFlowCanvas::hitPort(
    Vector2 screen,
    int& outNodeId,
    int& outChildIndex,
    bool& outIsParent) const
{
    outNodeId = -1;
    outChildIndex = -1;
    outIsParent = false;

    auto nearPort = [&](Vector2 worldPort) {
        const Vector2 s = screenFromWorld(worldPort);
        const float dx = s.x - screen.x;
        const float dy = s.y - screen.y;
        return (dx * dx + dy * dy) <= (kPortHitSlop * kPortHitSlop);
    };

    if (nearPort(startChildPortWorld()))
    {
        outNodeId = 0;
        outChildIndex = 0;
        outIsParent = false;
        return true;
    }

    for (const DialogFlowNode& n : nodes)
    {
        if (nearPort(parentPortWorld(n)))
        {
            outNodeId = n.id;
            outChildIndex = -1;
            outIsParent = true;
            return true;
        }
        const int kids = dialogNodeChildCount(n.kind);
        for (int c = 0; c < kids; ++c)
        {
            if (nearPort(childPortWorld(n.id, c)))
            {
                outNodeId = n.id;
                outChildIndex = c;
                outIsParent = false;
                return true;
            }
        }
    }
    return false;
}

int DialogFlowCanvas::hitNodeCard(Vector2 screen) const
{
    const Vector2 world = worldFromScreen(screen);
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i)
    {
        const Rectangle card = nodeCardWorld(nodes[static_cast<size_t>(i)]);
        if (CheckCollisionPointRec(world, card))
            return nodes[static_cast<size_t>(i)].id;
    }
    const Rectangle start = startCardWorld();
    if (CheckCollisionPointRec(world, start))
        return 0;
    return -1;
}

void DialogFlowCanvas::tryFinishWire(Vector2 screen)
{
    int toId = -1;
    int child = -1;
    bool isParent = false;
    if (!hitPort(screen, toId, child, isParent) || !isParent || toId <= 0)
    {
        status = "Wire cancelled (drop on a parent port)";
        wiring = false;
        return;
    }
    if (toId == wireFromId)
    {
        status = "Cannot wire a node to itself";
        wiring = false;
        return;
    }

    edges.erase(
        std::remove_if(
            edges.begin(),
            edges.end(),
            [toId](const DialogFlowEdge& e) { return e.toId == toId; }),
        edges.end());

    DialogFlowEdge edge;
    edge.fromId = wireFromId;
    edge.fromChildIndex = wireFromChild;
    edge.toId = toId;
    edges.push_back(edge);
    wiring = false;
    status = "Connected";
}

void DialogFlowCanvas::closeVoiceMenu()
{
    voiceMenuOpen = false;
    voiceMenuBounds = {0, 0, 0, 0};
    voiceMenuScroll = 0.0f;
    voiceMenuAnchorRow = -1;
}

void DialogFlowCanvas::closeContextMenu()
{
    closeVoiceMenu();
    contextMenuOpen = false;
    contextMenuNodeId = -1;
    contextMenuItems.clear();
    contextMenuActions.clear();
    contextMenuBounds = {0, 0, 0, 0};
}

std::string DialogFlowCanvas::formatDefaultVoiceLabel(const std::string& voiceId)
{
    if (voiceId.empty())
        return "Off";
    return voiceId;
}

void DialogFlowCanvas::buildContextMenuItems(const DialogFlowNode& n)
{
    contextMenuItems.clear();
    contextMenuActions.clear();
    auto add = [&](const std::string& label, int action) {
        contextMenuItems.push_back(label);
        contextMenuActions.push_back(action);
    };

    switch (n.kind)
    {
    case DialogNodeKind::ActorDialog:
        add("Edit text...", kFlowMenuEditText);
        add("Edit TTS...", kFlowMenuEditTts);
        add(
            "Default voice: " + formatDefaultVoiceLabel(n.defaultVoice) + "  ▸",
            kFlowMenuOpenDefaultVoice);
        break;
    case DialogNodeKind::PlayerDialog:
        add("Edit text...", kFlowMenuEditPlayerText);
        add("Edit TTS...", kFlowMenuEditTts);
        add(
            "Default voice: " + formatDefaultVoiceLabel(n.defaultVoice) + "  ▸",
            kFlowMenuOpenDefaultVoice);
        break;
    case DialogNodeKind::Event:
        add("Edit event id...", kFlowMenuEditEventId);
        break;
    case DialogNodeKind::GetItem:
        add("Edit item id...", kFlowMenuEditItemId);
        break;
    case DialogNodeKind::ActorInventory:
        add("Edit actor id...", kFlowMenuEditInventoryActor);
        add("Edit opening text...", kFlowMenuEditText);
        add("Edit opening TTS...", kFlowMenuEditTts);
        break;
    case DialogNodeKind::Attack:
        add("Edit combatant...", kFlowMenuEditCombatant);
        add(n.playerDeathPossible ? "Death possible: ON" : "Death possible: off",
            kFlowMenuToggleDeathPossible);
        break;
    case DialogNodeKind::TriggerEvent:
        add("Edit event id...", kFlowMenuEditEventId);
        break;
    default:
        break;
    }
    add("Delete node", kFlowMenuDelete);
}

void DialogFlowCanvas::openContextMenu(int nodeId, Vector2 screen)
{
    DialogFlowNode* n = findNode(nodeId);
    if (n == nullptr)
        return;
    selectedNodeId = nodeId;
    contextMenuNodeId = nodeId;
    closeVoiceMenu();
    buildContextMenuItems(*n);
    contextMenuOpen = true;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    float maxW = 120.0f;
    for (const std::string& s : contextMenuItems)
        maxW = std::max(maxW, measureUiTextWidth(font, s, kFontSmall) + 24.0f);
    const float h =
        8.0f + kContextRowH * static_cast<float>(contextMenuItems.size());
    contextMenuBounds = {screen.x, screen.y, maxW, h};
    // Keep on screen
    const float sw = static_cast<float>(GetScreenWidth());
    const float sh = static_cast<float>(GetScreenHeight());
    if (contextMenuBounds.x + contextMenuBounds.width > sw - 4.0f)
        contextMenuBounds.x = sw - contextMenuBounds.width - 4.0f;
    if (contextMenuBounds.y + contextMenuBounds.height > sh - 4.0f)
        contextMenuBounds.y = sh - contextMenuBounds.height - 4.0f;
}

void DialogFlowCanvas::layoutVoiceMenu()
{
    if (!voiceMenuOpen || voiceMenuAnchorRow < 0)
    {
        voiceMenuBounds = {0, 0, 0, 0};
        return;
    }
    const std::vector<std::string>& voices = builtinVoiceIds();
    // Row 0 = Off, then each builtin voice.
    const int optionCount = 1 + static_cast<int>(voices.size());
    const int visible = std::min(kVoiceMenuVisibleRows, optionCount);
    const float menuH = static_cast<float>(visible) * kVoiceRowH + 4.0f;
    const float menuW = 148.0f;
    float x = contextMenuBounds.x + contextMenuBounds.width + 2.0f;
    float y = contextMenuBounds.y + 4.0f
        + kContextRowH * static_cast<float>(voiceMenuAnchorRow);
    const float sw = static_cast<float>(GetScreenWidth());
    const float sh = static_cast<float>(GetScreenHeight());
    if (x + menuW > sw - 4.0f)
        x = contextMenuBounds.x - menuW - 2.0f;
    if (y + menuH > sh - 4.0f)
        y = std::max(4.0f, sh - menuH - 4.0f);
    if (y < 4.0f)
        y = 4.0f;
    const float maxScroll =
        static_cast<float>(std::max(0, optionCount - visible));
    voiceMenuScroll = std::clamp(voiceMenuScroll, 0.0f, maxScroll);
    voiceMenuBounds = {x, y, menuW, menuH};
}

void DialogFlowCanvas::openVoiceMenu(int contextRow)
{
    voiceMenuOpen = true;
    voiceMenuAnchorRow = contextRow;
    voiceMenuScroll = 0.0f;
    // Scroll so the current selection is visible.
    if (DialogFlowNode* n = findNode(contextMenuNodeId))
    {
        const std::vector<std::string>& voices = builtinVoiceIds();
        int selectedIndex = 0; // Off
        if (!n->defaultVoice.empty())
        {
            for (size_t i = 0; i < voices.size(); ++i)
            {
                if (voices[i] == n->defaultVoice)
                {
                    selectedIndex = 1 + static_cast<int>(i);
                    break;
                }
            }
        }
        const int optionCount = 1 + static_cast<int>(voices.size());
        const int visible = std::min(kVoiceMenuVisibleRows, optionCount);
        const float maxScroll =
            static_cast<float>(std::max(0, optionCount - visible));
        voiceMenuScroll = std::clamp(
            static_cast<float>(selectedIndex - visible / 2), 0.0f, maxScroll);
    }
    layoutVoiceMenu();
}

bool DialogFlowCanvas::handleVoiceMenuClick(Vector2 mouse)
{
    if (!voiceMenuOpen)
        return false;
    layoutVoiceMenu();
    if (voiceMenuBounds.width < 1.0f)
        return false;

    if (CheckCollisionPointRec(mouse, voiceMenuBounds))
    {
        const std::vector<std::string>& voices = builtinVoiceIds();
        const int optionCount = 1 + static_cast<int>(voices.size());
        const int first =
            static_cast<int>(std::floor(voiceMenuScroll + 0.001f));
        const int i = first
            + static_cast<int>((mouse.y - voiceMenuBounds.y - 2.0f) / kVoiceRowH);
        if (i >= 0 && i < optionCount)
        {
            if (DialogFlowNode* n = findNode(contextMenuNodeId))
            {
                if (i == 0)
                    n->defaultVoice.clear();
                else
                {
                    n->defaultVoice =
                        normalizeVoiceId(voices[static_cast<size_t>(i - 1)]);
                    if (!isKnownBuiltinVoiceId(n->defaultVoice))
                        n->defaultVoice.clear();
                }
                status = "Default voice: "
                    + formatDefaultVoiceLabel(n->defaultVoice);
                persistNodeTtsToJson(*n);
                // Refresh parent menu label while keeping menus open briefly —
                // then close both so the choice feels final.
                buildContextMenuItems(*n);
                const Font font =
                    (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
                float maxW = contextMenuBounds.width;
                for (const std::string& s : contextMenuItems)
                    maxW = std::max(
                        maxW, measureUiTextWidth(font, s, kFontSmall) + 24.0f);
                contextMenuBounds.width = maxW;
                contextMenuBounds.height =
                    8.0f
                    + kContextRowH * static_cast<float>(contextMenuItems.size());
            }
            closeContextMenu();
        }
        return true;
    }

    // Click on parent context menu may switch rows; don't consume here.
    if (CheckCollisionPointRec(mouse, contextMenuBounds))
        return false;

    closeContextMenu();
    return true;
}

void DialogFlowCanvas::drawVoiceMenu() const
{
    if (!voiceMenuOpen || voiceMenuBounds.width < 1.0f)
        return;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const std::vector<std::string>& voices = builtinVoiceIds();
    const int optionCount = 1 + static_cast<int>(voices.size());
    const int visible = std::min(kVoiceMenuVisibleRows, optionCount);
    const int first = static_cast<int>(std::floor(voiceMenuScroll + 0.001f));

    DrawRectangleRec(voiceMenuBounds, Color{36, 32, 44, 255});
    DrawRectangleLinesEx(voiceMenuBounds, 1.0f, kPanelBorder);

    std::string current;
    if (const DialogFlowNode* n = findNode(contextMenuNodeId))
        current = n->defaultVoice;

    const Vector2 mouse = GetMousePosition();
    float my = voiceMenuBounds.y + 2.0f;
    for (int row = 0; row < visible; ++row)
    {
        const int i = first + row;
        if (i < 0 || i >= optionCount)
            break;
        const std::string label =
            (i == 0) ? std::string("Off") : voices[static_cast<size_t>(i - 1)];
        const bool selected =
            (i == 0) ? current.empty() : (label == current);
        const Rectangle r = {
            voiceMenuBounds.x + 2.0f,
            my,
            voiceMenuBounds.width - 4.0f,
            kVoiceRowH - 2.0f};
        if (selected)
            DrawRectangleRec(r, kSelection);
        else if (CheckCollisionPointRec(mouse, r))
            DrawRectangleRec(r, Color{60, 54, 72, 220});
        DrawTextEx(
            font,
            label.c_str(),
            {r.x + 8.0f, r.y + 3.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);
        my += kVoiceRowH;
    }
}

std::string DialogFlowCanvas::sceneDefaultVoice() const
{
    if (docs == nullptr || selectionSceneId == nullptr || selectionSceneId->empty())
        return {};
    if (!docs->scenes.hasScene(*selectionSceneId))
        return {};
    const nlohmann::json* scene = docs->scenes.sceneJson(*selectionSceneId);
    if (scene == nullptr || !scene->is_object())
        return {};
    const std::string v = scene->value("ttsDefaultVoice", std::string());
    if (!v.empty() && isKnownBuiltinVoiceId(v))
        return normalizeVoiceId(v);
    return {};
}

void DialogFlowCanvas::persistNodeTtsToJson(DialogFlowNode& n)
{
    if (docs == nullptr || n.jsonPointer.empty())
        return;
    if (n.kind != DialogNodeKind::ActorDialog && n.kind != DialogNodeKind::PlayerDialog)
        return;

    const bool resumeLeaf = n.jsonPointer.size() >= 12
        && n.jsonPointer.compare(n.jsonPointer.size() - 12, 12, "/resumeIntro") == 0;
    const bool introLeaf = n.jsonPointer.size() >= 6
        && n.jsonPointer.compare(n.jsonPointer.size() - 6, 6, "/intro") == 0;
    const bool textLeaf = n.jsonPointer.size() >= 5
        && n.jsonPointer.compare(n.jsonPointer.size() - 5, 5, "/text") == 0;

    std::string bagPtr = n.jsonPointer;
    if (resumeLeaf || introLeaf || textLeaf)
    {
        const size_t slash = n.jsonPointer.rfind('/');
        if (slash != std::string::npos)
            bagPtr = n.jsonPointer.substr(0, slash);
    }

    nlohmann::json* obj = docs->conversationJsonAt(bagPtr);
    if (obj == nullptr || !obj->is_object())
        return;

    if (resumeLeaf)
    {
        (*obj)["resumeTtsText"] = n.dialogTts;
        if (!n.defaultVoice.empty())
            (*obj)["resumeTtsVoice"] = n.defaultVoice;
        else
            obj->erase("resumeTtsVoice");
        if (!n.dialogTtsAudio.empty())
            (*obj)["resumeTtsAudio"] = n.dialogTtsAudio;
        // Runtime resume bags do not yet load segment arrays; keep single file.
        obj->erase("resumeTtsTextSha256");
    }
    else
    {
        (*obj)["ttsText"] = n.dialogTts;
        (*obj)["tts"] = !n.dialogTts.empty() || !n.dialogTtsAudio.empty();
        if (!n.defaultVoice.empty())
            (*obj)["ttsVoice"] = n.defaultVoice;
        else
            obj->erase("ttsVoice");
        if (!n.dialogTtsAudio.empty())
            (*obj)["ttsAudio"] = n.dialogTtsAudio;
        if (n.dialogTtsAudioSegments.size() > 1)
            (*obj)["ttsAudioSegments"] = n.dialogTtsAudioSegments;
        else
            obj->erase("ttsAudioSegments");
        obj->erase("ttsTextSha256");
    }
    docs->markDirty();
}

void DialogFlowCanvas::openParchmentForField(
    std::string* field,
    bool tts,
    const std::string& label,
    const std::string& companionPlain,
    const std::string& bakeVoiceId,
    const std::string& bakeAudioRelPath,
    std::string* bakeAudioBind,
    std::vector<std::string>* bakeAudioSegmentsBind)
{
    if (parchment == nullptr || docs == nullptr || field == nullptr)
        return;
    // Snapshot before closeContextMenu() clears contextMenuNodeId.
    const int nodeId =
        contextMenuNodeId > 0 ? contextMenuNodeId : selectedNodeId;
    closeContextMenu();

    std::string voice = bakeVoiceId;
    if (tts && voice.empty())
        voice = sceneDefaultVoice();

    parchment->openEditor(
        field,
        tts,
        label,
        docs->resourceDir,
        docs->assetRoot,
        companionPlain,
        voice,
        bakeAudioRelPath,
        bakeAudioBind,
        bakeAudioSegmentsBind);

    parchment->onClosed = [this, nodeId, tts]() {
        if (tts)
        {
            if (DialogFlowNode* n = findNode(nodeId))
            {
                persistNodeTtsToJson(*n);
                status = "TTS saved to conversations.json";
                return;
            }
        }
        status = "Field updated (editor-local)";
    };
}

void DialogFlowCanvas::deleteNode(int id)
{
    if (id <= 0)
        return;
    edges.erase(
        std::remove_if(
            edges.begin(),
            edges.end(),
            [id](const DialogFlowEdge& e) {
                return e.fromId == id || e.toId == id;
            }),
        edges.end());
    nodes.erase(
        std::remove_if(
            nodes.begin(),
            nodes.end(),
            [id](const DialogFlowNode& n) { return n.id == id; }),
        nodes.end());
    if (selectedNodeId == id)
        selectedNodeId = -1;
    status = "Deleted node";
}

void DialogFlowCanvas::applyContextMenuAction(int action)
{
    DialogFlowNode* n = findNode(contextMenuNodeId);
    if (n == nullptr)
    {
        closeContextMenu();
        return;
    }

    switch (action)
    {
    case kFlowMenuEditText:
        openParchmentForField(&n->dialogText, false, "Actor dialog text");
        return;
    case kFlowMenuEditTts:
    {
        const std::string& companion =
            n->kind == DialogNodeKind::PlayerDialog ? n->playerDialogText
                                                    : n->dialogText;
        std::string voice = n->defaultVoice;
        if (voice.empty())
            voice = sceneDefaultVoice();
        // Prefer the authored bag path so Generate voice overwrites the same
        // file Play TTS resolves (never fall back to parchment_voice.mp3).
        if (n->dialogTtsAudio.empty() && docs != nullptr && !n->jsonPointer.empty())
        {
            const bool resumeLeaf = n->jsonPointer.size() >= 12
                && n->jsonPointer.compare(
                       n->jsonPointer.size() - 12, 12, "/resumeIntro")
                    == 0;
            const bool introLeaf = n->jsonPointer.size() >= 6
                && n->jsonPointer.compare(n->jsonPointer.size() - 6, 6, "/intro")
                    == 0;
            const bool textLeaf = n->jsonPointer.size() >= 5
                && n->jsonPointer.compare(n->jsonPointer.size() - 5, 5, "/text")
                    == 0;
            std::string bagPtr = n->jsonPointer;
            if (resumeLeaf || introLeaf || textLeaf)
            {
                const size_t slash = n->jsonPointer.rfind('/');
                if (slash != std::string::npos)
                    bagPtr = n->jsonPointer.substr(0, slash);
            }
            if (const nlohmann::json* obj = docs->conversationJsonAt(bagPtr))
            {
                if (obj->is_object())
                {
                    n->dialogTtsAudio = resumeLeaf
                        ? obj->value("resumeTtsAudio", std::string())
                        : obj->value("ttsAudio", std::string());
                }
            }
        }
        if (n->dialogTtsAudio.empty() && selectionSceneId != nullptr
            && !selectionSceneId->empty())
        {
            std::string leaf = n->sourceChoiceId.empty()
                ? (n->sourcePhaseId.empty() ? ("node_" + std::to_string(n->id))
                                            : (n->sourcePhaseId + "_intro"))
                : n->sourceChoiceId;
            for (char& ch : leaf)
            {
                if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'
                      || ch == '-'))
                    ch = '_';
            }
            n->dialogTtsAudio =
                "resources/audio/tts/" + *selectionSceneId + "/" + leaf + ".mp3";
        }
        openParchmentForField(
            &n->dialogTts,
            true,
            n->kind == DialogNodeKind::PlayerDialog ? "Player dialog TTS"
                                                    : "Actor dialog TTS",
            companion,
            voice,
            n->dialogTtsAudio,
            &n->dialogTtsAudio,
            &n->dialogTtsAudioSegments);
        return;
    }
    case kFlowMenuEditPlayerText:
        openParchmentForField(&n->playerDialogText, false, "Player dialog text");
        return;
    case kFlowMenuEditEventId:
        openParchmentForField(&n->eventId, false, "Event id");
        return;
    case kFlowMenuEditItemId:
        openParchmentForField(&n->itemId, false, "Item id");
        return;
    case kFlowMenuEditInventoryActor:
        openParchmentForField(&n->inventoryActorId, false, "Inventory actor id");
        return;
    case kFlowMenuEditCombatant:
        openParchmentForField(&n->combatantId, false, "Combatant id");
        return;
    case kFlowMenuToggleDeathPossible:
        n->playerDeathPossible = !n->playerDeathPossible;
        status = n->playerDeathPossible ? "Death possible: ON" : "Death possible: off";
        closeContextMenu();
        return;
    case kFlowMenuOpenDefaultVoice:
    {
        int row = -1;
        for (size_t i = 0; i < contextMenuActions.size(); ++i)
        {
            if (contextMenuActions[i] == kFlowMenuOpenDefaultVoice)
            {
                row = static_cast<int>(i);
                break;
            }
        }
        if (row >= 0)
            openVoiceMenu(row);
        return;
    }
    case kFlowMenuDelete:
        deleteNode(n->id);
        closeContextMenu();
        return;
    default:
        closeContextMenu();
        break;
    }
}

void DialogFlowCanvas::contentScrollLimits(
    float& minX,
    float& minY,
    float& maxX,
    float& maxY) const
{
    // Always allow a little room around Start.
    minX = 0.0f;
    minY = 0.0f;
    maxX = 400.0f;
    maxY = 300.0f;

    auto expand = [&](float x, float y, float w, float h) {
        minX = std::min(minX, x - 64.0f);
        minY = std::min(minY, y - 64.0f);
        maxX = std::max(maxX, x + w + 128.0f);
        maxY = std::max(maxY, y + h + 128.0f);
    };

    const Rectangle start = startCardWorld();
    expand(start.x, start.y, start.width, start.height);
    for (const DialogFlowNode& n : nodes)
        expand(n.x, n.y, kNodeW, kNodeH);

    // Scroll range: how far we can pan so content stays reachable in the view.
    const float viewW = std::max(1.0f, lastBounds.width);
    const float viewH = std::max(1.0f, lastBounds.height);
    // scroll maps world → screen via world - scroll + bounds.origin
    // To show world point W at left of view: scrollX = W - 0 … allow up to maxX - viewW
    minX = std::min(0.0f, minX);
    minY = std::min(0.0f, minY);
    maxX = std::max(0.0f, maxX - viewW * 0.35f);
    maxY = std::max(0.0f, maxY - viewH * 0.35f);
}

void DialogFlowCanvas::clampScroll()
{
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    contentScrollLimits(minX, minY, maxX, maxY);
    if (scrollX < minX)
        scrollX = minX;
    if (scrollY < minY)
        scrollY = minY;
    if (scrollX > maxX)
        scrollX = maxX;
    if (scrollY > maxY)
        scrollY = maxY;
}

void DialogFlowCanvas::applyEdgeAutoPan(Rectangle bounds)
{
    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, bounds))
        return;

    // Progressive bands from the view edge (user request).
    constexpr float kOuterBand = 64.0f;
    constexpr float kInnerBand = 16.0f;
    constexpr float kOuterSpeed = 220.0f; // px/sec when inside 64px band
    constexpr float kInnerSpeed = 480.0f; // px/sec when inside 16px band

    auto bandSpeed = [](float distFromEdge) -> float {
        if (distFromEdge >= kOuterBand)
            return 0.0f;
        if (distFromEdge <= kInnerBand)
            return kInnerSpeed;
        // Smooth ramp between outer and inner.
        const float t =
            (kOuterBand - distFromEdge) / (kOuterBand - kInnerBand); // 0 at 64 → 1 at 16
        return kOuterSpeed + (kInnerSpeed - kOuterSpeed) * t;
    };

    const float leftDist = mouse.x - bounds.x;
    const float rightDist = bounds.x + bounds.width - mouse.x;
    const float topDist = mouse.y - bounds.y;
    const float bottomDist = bounds.y + bounds.height - mouse.y;

    const float dt = GetFrameTime();
    // Cursor near left → reveal content to the left (decrease scrollX).
    // Cursor near right → reveal content to the right (increase scrollX).
    scrollX -= bandSpeed(leftDist) * dt;
    scrollX += bandSpeed(rightDist) * dt;
    scrollY -= bandSpeed(topDist) * dt;
    scrollY += bandSpeed(bottomDist) * dt;

    clampScroll();
}

void DialogFlowCanvas::handleInput(Rectangle bounds, bool allowInteraction)
{
    lastBounds = bounds;
    ensureIconsLoaded();

    // Top-right Clean up (mirrors Scenes map chrome).
    {
        const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
        const char* cleanLabel = "Clean up";
        const EditorButtonConfig& btnCfg = editorButtons().config;
        const float cleanTextW =
            MeasureTextEx(font, cleanLabel, btnCfg.fontSize, 1.0f).x;
        const float cleanW = std::clamp(
            cleanTextW + btnCfg.padX * 2.0f + 8.0f,
            std::max(btnCfg.minWidth, 96.0f),
            btnCfg.maxWidth);
        const float cleanH = std::max(24.0f, btnCfg.minHeight);
        cleanUpBtn = {
            bounds.x + bounds.width - cleanW - 10.0f,
            bounds.y + 6.0f,
            cleanW,
            cleanH};
    }

    if (!allowInteraction)
    {
        placingFromPalette = false;
        wiring = false;
        draggingNode = false;
        return;
    }

    const Vector2 mouse = GetMousePosition();

    const bool canClean = !nodes.empty() && !contextMenuOpen
        && (parchment == nullptr || !parchment->blocksInput());
    if (canClean && editorMousePressed(MOUSE_BUTTON_LEFT)
        && CheckCollisionPointRec(mouse, cleanUpBtn))
    {
        placingFromPalette = false;
        wiring = false;
        draggingNode = false;
        closeContextMenu();
        relayoutGraph();
        status = "Graph cleaned up";
        return;
    }

    if (contextMenuOpen)
    {
        if (voiceMenuOpen)
        {
            layoutVoiceMenu();
            if (CheckCollisionPointRec(mouse, voiceMenuBounds))
            {
                const float wheel = GetMouseWheelMove();
                if (wheel != 0.0f)
                {
                    const int optionCount =
                        1 + static_cast<int>(builtinVoiceIds().size());
                    const int visible =
                        std::min(kVoiceMenuVisibleRows, optionCount);
                    const float maxScroll = static_cast<float>(
                        std::max(0, optionCount - visible));
                    voiceMenuScroll = std::clamp(
                        voiceMenuScroll - wheel, 0.0f, maxScroll);
                }
            }
        }

        if (editorMousePressed(MOUSE_BUTTON_LEFT)
            || editorMousePressed(MOUSE_BUTTON_RIGHT))
        {
            if (voiceMenuOpen && handleVoiceMenuClick(mouse))
                return;

            if (CheckCollisionPointRec(mouse, contextMenuBounds))
            {
                const int idx = static_cast<int>(
                    (mouse.y - contextMenuBounds.y - 4.0f) / kContextRowH);
                if (idx >= 0 && idx < static_cast<int>(contextMenuActions.size()))
                {
                    const int action =
                        contextMenuActions[static_cast<size_t>(idx)];
                    // Re-clicking Default voice toggles the flyout.
                    if (action == kFlowMenuOpenDefaultVoice && voiceMenuOpen
                        && voiceMenuAnchorRow == idx)
                    {
                        closeVoiceMenu();
                        return;
                    }
                    if (action != kFlowMenuOpenDefaultVoice)
                        closeVoiceMenu();
                    applyContextMenuAction(action);
                }
                else
                    closeContextMenu();
            }
            else
            {
                closeContextMenu();
            }
        }
        return;
    }

    const bool over = CheckCollisionPointRec(mouse, bounds);

    // Edge auto-pan whenever the cursor is over the flowchart (find off-screen nodes).
    if (over)
        applyEdgeAutoPan(bounds);

    if (over)
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            // Shift+wheel pans horizontally; plain wheel pans vertically.
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                scrollX -= wheel * 48.0f;
            else
                scrollY -= wheel * 48.0f;
            clampScroll();
        }
    }

    if (placingFromPalette)
    {
        if (editorMouseReleased(MOUSE_BUTTON_LEFT))
        {
            if (over)
            {
                const Vector2 world = worldFromScreen(mouse);
                DialogFlowNode n;
                n.id = nextNodeId++;
                n.kind = placeKind;
                n.x = world.x - kNodeW * 0.5f;
                n.y = world.y - kNodeH * 0.5f;
                if (n.x < 8.0f)
                    n.x = 8.0f;
                if (n.y < 8.0f)
                    n.y = 8.0f;
                nodes.push_back(n);
                selectedNodeId = n.id;
                status = std::string("Placed ") + dialogNodeKindLabel(n.kind);
            }
            else
            {
                status = "Place cancelled";
            }
            placingFromPalette = false;
        }
        else if (editorMousePressed(MOUSE_BUTTON_RIGHT))
        {
            placingFromPalette = false;
            status = "Place cancelled";
        }
        return;
    }

    if (wiring)
    {
        if (editorMouseReleased(MOUSE_BUTTON_LEFT))
        {
            if (over)
                tryFinishWire(mouse);
            else
            {
                wiring = false;
                status = "Wire cancelled";
            }
        }
        else if (editorMousePressed(MOUSE_BUTTON_RIGHT))
        {
            wiring = false;
            status = "Wire cancelled";
        }
        return;
    }

    if (draggingNode && dragNodeId > 0)
    {
        if (editorMouseDown(MOUSE_BUTTON_LEFT) && over)
        {
            const Vector2 world = worldFromScreen(mouse);
            if (DialogFlowNode* n = findNode(dragNodeId))
            {
                n->x = world.x - dragGrabDX;
                n->y = world.y - dragGrabDY;
            }
        }
        if (editorMouseReleased(MOUSE_BUTTON_LEFT) || !editorMouseDown(MOUSE_BUTTON_LEFT))
            draggingNode = false;
        return;
    }

    if (!over)
        return;

    if (editorMousePressed(MOUSE_BUTTON_RIGHT))
    {
        const int hit = hitNodeCard(mouse);
        if (hit > 0)
        {
            openContextMenu(hit, mouse);
            return;
        }
        closeContextMenu();
        return;
    }

    if (editorMousePressed(MOUSE_BUTTON_LEFT))
    {
        int portNode = -1;
        int portChild = -1;
        bool isParent = false;
        if (hitPort(mouse, portNode, portChild, isParent) && !isParent)
        {
            wiring = true;
            wireFromId = portNode;
            wireFromChild = portChild;
            selectedNodeId = portNode;
            status = "Drag to a parent (top) port";
            return;
        }

        const int hit = hitNodeCard(mouse);
        selectedNodeId = hit;
        if (hit > 0)
        {
            const Vector2 world = worldFromScreen(mouse);
            if (const DialogFlowNode* n = findNode(hit))
            {
                draggingNode = true;
                dragNodeId = hit;
                dragGrabDX = world.x - n->x;
                dragGrabDY = world.y - n->y;
            }
        }
    }

    if (selectedNodeId > 0
        && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)))
    {
        deleteNode(selectedNodeId);
    }
}

void DialogFlowCanvas::drawContextMenu()
{
    if (!contextMenuOpen || contextMenuItems.empty())
        return;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    DrawRectangleRec(contextMenuBounds, Color{32, 30, 40, 250});
    DrawRectangleLinesEx(contextMenuBounds, 1.0f, kPanelBorder);

    const Vector2 mouse = GetMousePosition();
    for (size_t i = 0; i < contextMenuItems.size(); ++i)
    {
        const Rectangle row = {
            contextMenuBounds.x + 2.0f,
            contextMenuBounds.y + 4.0f + kContextRowH * static_cast<float>(i),
            contextMenuBounds.width - 4.0f,
            kContextRowH - 1.0f};
        const bool isVoiceRow =
            i < contextMenuActions.size()
            && contextMenuActions[i] == kFlowMenuOpenDefaultVoice;
        if ((voiceMenuOpen && isVoiceRow && static_cast<int>(i) == voiceMenuAnchorRow)
            || CheckCollisionPointRec(mouse, row))
            DrawRectangleRec(row, kSelection);
        DrawTextEx(
            font,
            contextMenuItems[i].c_str(),
            {row.x + 8.0f, row.y + 5.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);
    }

    layoutVoiceMenu();
    drawVoiceMenu();
}

void DialogFlowCanvas::draw(Rectangle bounds)
{
    lastBounds = bounds;
    ensureIconsLoaded();
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());

    DrawRectangleRec(bounds, kCanvasBg);
    DrawRectangleLinesEx(bounds, 1.0f, kPanelInnerEdge);

    BeginScissorMode(
        static_cast<int>(bounds.x),
        static_cast<int>(bounds.y),
        static_cast<int>(bounds.width),
        static_cast<int>(bounds.height));

    {
        const float step = 32.0f;
        const Color grid = {32, 30, 38, 255};
        const float ox = bounds.x - std::fmod(scrollX, step);
        const float oy = bounds.y - std::fmod(scrollY, step);
        for (float x = ox; x < bounds.x + bounds.width; x += step)
            DrawLineEx({x, bounds.y}, {x, bounds.y + bounds.height}, 1.0f, grid);
        for (float y = oy; y < bounds.y + bounds.height; y += step)
            DrawLineEx({bounds.x, y}, {bounds.x + bounds.width, y}, 1.0f, grid);
    }

    auto drawWire = [&](Vector2 fromW, Vector2 toW) {
        const Vector2 a = screenFromWorld(fromW);
        const Vector2 b = screenFromWorld(toW);
        const float midY = (a.y + b.y) * 0.5f;
        DrawLineEx(a, {a.x, midY}, 2.0f, kDialogFlowWire);
        DrawLineEx({a.x, midY}, {b.x, midY}, 2.0f, kDialogFlowWire);
        DrawLineEx({b.x, midY}, b, 2.0f, kDialogFlowWire);
        DrawCircleV(b, 3.0f, kDialogFlowWire);
    };

    for (const DialogFlowEdge& e : edges)
    {
        const Vector2 from = childPortWorld(e.fromId, e.fromChildIndex);
        if (const DialogFlowNode* n = findNode(e.toId))
            drawWire(from, parentPortWorld(*n));
    }

    if (wiring)
    {
        const Vector2 from = childPortWorld(wireFromId, wireFromChild);
        DrawLineEx(screenFromWorld(from), GetMousePosition(), 2.0f, Color{200, 90, 100, 160});
    }

    {
        const Rectangle card = startCardWorld();
        const Vector2 center = screenFromWorld(
            {card.x + card.width * 0.5f, card.y + card.height * 0.5f});
        if (selectedNodeId == 0)
            DrawCircleV(center, kStartSize * 0.58f, Color{120, 96, 48, 80});
        drawCompassRose(center, kStartSize * 0.42f);
        const Vector2 child = screenFromWorld(startChildPortWorld());
        DrawCircleV(child, kPortR, kDialogChildPort);
        DrawCircleLinesV(child, kPortR, Color{40, 30, 28, 255});
    }

    for (const DialogFlowNode& n : nodes)
    {
        const Rectangle world = nodeCardWorld(n);
        const Vector2 tl = screenFromWorld({world.x, world.y});
        const Rectangle card = {tl.x, tl.y, world.width, world.height};
        const bool sel = selectedNodeId == n.id;
        // Icon plate only — no title / brief / field text on the map.
        // Right-click opens the edit menu for structure fields.
        DrawRectangleRounded(card, 0.18f, 6, Color{34, 32, 42, 255});
        DrawRectangleRoundedLines(
            card,
            0.18f,
            6,
            sel ? kPanelBorder : kPanelInnerEdge);

        const float iconPad = 4.0f;
        drawKindIcon(
            n.kind,
            {card.x + iconPad,
             card.y + iconPad,
             card.width - iconPad * 2.0f,
             card.height - iconPad * 2.0f});

        {
            const Vector2 p = screenFromWorld(parentPortWorld(n));
            DrawCircleV(p, kPortR, kDialogParentPort);
            DrawCircleLinesV(p, kPortR, Color{20, 30, 40, 255});
        }
        const int kids = dialogNodeChildCount(n.kind);
        for (int c = 0; c < kids; ++c)
        {
            const Vector2 p = screenFromWorld(childPortWorld(n.id, c));
            DrawCircleV(p, kPortR, kDialogChildPort);
            DrawCircleLinesV(p, kPortR, Color{40, 30, 28, 255});
        }
    }

    if (placingFromPalette && CheckCollisionPointRec(GetMousePosition(), bounds))
    {
        const Vector2 m = GetMousePosition();
        const Rectangle ghost = {
            m.x - kNodeW * 0.5f, m.y - kNodeH * 0.5f, kNodeW, kNodeH};
        DrawRectangleLinesEx(ghost, 1.5f, kPanelBorder);
        drawKindIcon(
            placeKind,
            {ghost.x + 4.0f, ghost.y + 4.0f, ghost.width - 8.0f, ghost.height - 8.0f});
    }

    EndScissorMode();

    DrawTextEx(
        font,
        "Dialog flow",
        {bounds.x + 10.0f, bounds.y + 8.0f},
        kFontLabel,
        1.0f,
        kTextMuted);

    const bool canClean = !nodes.empty()
        && (parchment == nullptr || !parchment->blocksInput());
    if (cleanUpBtn.width > 1.0f)
        drawEditorButton(font, cleanUpBtn, "Clean up", false, canClean);

    if (!status.empty())
    {
        DrawTextEx(
            font,
            status.c_str(),
            {bounds.x + 10.0f, bounds.y + bounds.height - 20.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
    }

    // Context menu after scissor so it is never clipped by the canvas.
    drawContextMenu();
}

int DialogFlowCanvas::allocNode(DialogNodeKind kind, float x, float y)
{
    DialogFlowNode n;
    n.id = nextNodeId++;
    n.kind = kind;
    n.x = x;
    n.y = y;
    nodes.push_back(n);
    return n.id;
}

void DialogFlowCanvas::addEdge(int fromId, int fromChild, int toId)
{
    // One inbound edge per node for the shell.
    edges.erase(
        std::remove_if(
            edges.begin(),
            edges.end(),
            [toId](const DialogFlowEdge& e) { return e.toId == toId; }),
        edges.end());
    DialogFlowEdge e;
    e.fromId = fromId;
    e.fromChildIndex = fromChild;
    e.toId = toId;
    edges.push_back(e);
}

int DialogFlowCanvas::migrateActorLine(
    const std::string& text,
    const std::string& tts,
    const std::string& pointer,
    float x,
    float y,
    const std::string& voice)
{
    const int id = allocNode(DialogNodeKind::ActorDialog, x, y);
    if (DialogFlowNode* n = findNode(id))
    {
        n->dialogText = text;
        n->dialogTts = tts;
        n->jsonPointer = pointer;
        if (!voice.empty() && isKnownBuiltinVoiceId(voice))
            n->defaultVoice = normalizeVoiceId(voice);
        else
            n->defaultVoice.clear(); // Off
        if (!pointer.empty() && docs != nullptr)
        {
            // /intro, /text, /resumeIntro are string leaves — TTS bags live on the
            // parent phase object. Choice pointers already target the object.
            std::string bagPtr = pointer;
            const bool resumeLeaf = pointer.size() >= 12
                && pointer.compare(pointer.size() - 12, 12, "/resumeIntro") == 0;
            const bool introLeaf = pointer.size() >= 6
                && pointer.compare(pointer.size() - 6, 6, "/intro") == 0;
            const bool textLeaf = pointer.size() >= 5
                && pointer.compare(pointer.size() - 5, 5, "/text") == 0;
            if (resumeLeaf || introLeaf || textLeaf)
            {
                const size_t slash = pointer.rfind('/');
                if (slash != std::string::npos)
                    bagPtr = pointer.substr(0, slash);
            }
            if (const nlohmann::json* obj = docs->conversationJsonAt(bagPtr))
            {
                if (obj->is_object())
                {
                    if (resumeLeaf)
                    {
                        n->dialogTtsAudio = obj->value("resumeTtsAudio", std::string());
                        n->dialogTtsAudioSegments.clear();
                        if (obj->contains("resumeTtsAudioSegments")
                            && (*obj)["resumeTtsAudioSegments"].is_array())
                        {
                            for (const auto& s : (*obj)["resumeTtsAudioSegments"])
                            {
                                if (s.is_string())
                                    n->dialogTtsAudioSegments.push_back(
                                        s.get<std::string>());
                            }
                        }
                        if (n->dialogTts.empty())
                            n->dialogTts = obj->value("resumeTtsText", std::string());
                        const std::string rv = obj->value("resumeTtsVoice", std::string());
                        if (!rv.empty() && isKnownBuiltinVoiceId(rv))
                            n->defaultVoice = normalizeVoiceId(rv);
                    }
                    else
                    {
                        n->dialogTtsAudio = obj->value("ttsAudio", std::string());
                        n->dialogTtsAudioSegments.clear();
                        if (obj->contains("ttsAudioSegments")
                            && (*obj)["ttsAudioSegments"].is_array())
                        {
                            for (const auto& s : (*obj)["ttsAudioSegments"])
                            {
                                if (s.is_string())
                                    n->dialogTtsAudioSegments.push_back(
                                        s.get<std::string>());
                            }
                        }
                        if (n->dialogTts.empty())
                            n->dialogTts = obj->value("ttsText", std::string());
                        if (n->defaultVoice.empty())
                        {
                            const std::string vv = obj->value("ttsVoice", std::string());
                            if (!vv.empty() && isKnownBuiltinVoiceId(vv))
                                n->defaultVoice = normalizeVoiceId(vv);
                        }
                    }
                }
            }
        }
    }
    return id;
}

int DialogFlowCanvas::migratePlayerLine(
    const std::string& label,
    const std::string& pointer,
    const std::string& choiceId,
    float x,
    float y,
    const std::string& voice)
{
    const int id = allocNode(DialogNodeKind::PlayerDialog, x, y);
    if (DialogFlowNode* n = findNode(id))
    {
        n->playerDialogText = label;
        n->jsonPointer = pointer;
        n->sourceChoiceId = choiceId;
        // Player dialog defaults to Off unless an explicit voice was authored.
        if (!voice.empty() && isKnownBuiltinVoiceId(voice))
            n->defaultVoice = normalizeVoiceId(voice);
        else
            n->defaultVoice.clear();
    }
    return id;
}

void DialogFlowCanvas::connectWithPorts(int fromId, int toId)
{
    if (toId <= 0)
        return;

    int maxChildren = 1;
    if (fromId == 0)
        maxChildren = 1;
    else if (const DialogFlowNode* from = findNode(fromId))
        maxChildren = dialogNodeChildCount(from->kind);

    int used = 0;
    for (const DialogFlowEdge& e : edges)
    {
        if (e.fromId == fromId)
            ++used;
    }

    int attachFrom = fromId;
    if (used >= maxChildren && fromId != 0)
    {
        // Spill: chain an Actor Dialog hub so remaining outs can attach.
        DialogFlowNode* from = findNode(fromId);
        const float hx = from ? from->x + 90.0f : 200.0f;
        const float hy = from ? from->y + 90.0f : 200.0f;
        const int hub = allocNode(DialogNodeKind::ActorDialog, hx, hy);
        if (DialogFlowNode* h = findNode(hub))
        {
            h->dialogText = "(more branches)";
            h->title = "More";
        }
        // Use last child port of the original for the hub link.
        addEdge(fromId, std::max(0, maxChildren - 1), hub);
        attachFrom = hub;
        used = 0;
        for (const DialogFlowEdge& e : edges)
        {
            if (e.fromId == attachFrom)
                ++used;
        }
        maxChildren = dialogNodeChildCount(DialogNodeKind::ActorDialog);
    }

    const int childIndex = std::min(used, std::max(0, maxChildren - 1));
    addEdge(attachFrom, childIndex, toId);
}

namespace
{

bool almostEqualMoney(float a, float b)
{
    return std::fabs(a - b) < 0.001f;
}

std::string formatMoneyAmount(float amount)
{
    std::ostringstream oss;
    if (std::fabs(amount - std::round(amount)) < 0.001f)
        oss << static_cast<int>(std::lround(amount));
    else
    {
        oss.setf(std::ios::fixed);
        oss.precision(2);
        oss << amount;
    }
    return oss.str();
}

std::string formatMoneyGateEventId(float amount)
{
    return "walletCash >= " + formatMoneyAmount(amount);
}

std::string formatInsufficientMoneyGateEventId(float amount)
{
    return "walletCash < " + formatMoneyAmount(amount);
}

std::string formatActorTabGateEventId(const std::string& actor)
{
    if (actor.empty())
        return "walletCash >= actorTab";
    return "walletCash >= actorTab:" + actor;
}

std::string formatPositiveActorTabEventId(const std::string& actor)
{
    if (actor.empty())
        return "actorTab > 0";
    return "actorTab > 0:" + actor;
}

/** Same player label, complementary wallet thresholds (room cash vs tab/comp). */
bool isMoneyAvailabilityPair(const nlohmann::json& a, const nlohmann::json& b)
{
    if (!a.is_object() || !b.is_object())
        return false;
    if (a.value("label", std::string()) != b.value("label", std::string()))
        return false;
    const float aNeed = a.value("requiresMoney", 0.0f);
    const float aInsuff = a.value("requiresInsufficientMoney", 0.0f);
    const float bNeed = b.value("requiresMoney", 0.0f);
    const float bInsuff = b.value("requiresInsufficientMoney", 0.0f);
    if (aNeed > 0.0f && bInsuff > 0.0f && almostEqualMoney(aNeed, bInsuff))
        return true;
    if (bNeed > 0.0f && aInsuff > 0.0f && almostEqualMoney(bNeed, aInsuff))
        return true;
    return false;
}

/**
 * Complementary tab settlement: can pay in full vs cannot (different labels).
 * Prefer matching tabActor when both set.
 */
bool isActorTabPayAvailabilityPair(const nlohmann::json& a, const nlohmann::json& b)
{
    if (!a.is_object() || !b.is_object())
        return false;
    const bool aPay = a.value("requiresPayActorTabInFull", false);
    const bool aInsuff = a.value("requiresInsufficientForActorTab", false);
    const bool bPay = b.value("requiresPayActorTabInFull", false);
    const bool bInsuff = b.value("requiresInsufficientForActorTab", false);
    if (!((aPay && bInsuff) || (bPay && aInsuff)))
        return false;
    const std::string aActor = a.value("tabActor", std::string());
    const std::string bActor = b.value("tabActor", std::string());
    if (!aActor.empty() && !bActor.empty() && aActor != bActor)
        return false;
    return true;
}

/** First matching solo gate condition on a choice, or empty if none. */
std::string soloAvailabilityEventId(const nlohmann::json& choice)
{
    if (!choice.is_object())
        return {};
    const float need = choice.value("requiresMoney", 0.0f);
    if (need > 0.0f)
        return formatMoneyGateEventId(need);
    const float insuff = choice.value("requiresInsufficientMoney", 0.0f);
    if (insuff > 0.0f)
        return formatInsufficientMoneyGateEventId(insuff);
    const std::string actor = choice.value("tabActor", std::string());
    if (choice.value("requiresPayActorTabInFull", false))
        return formatActorTabGateEventId(actor);
    if (choice.value("requiresInsufficientForActorTab", false))
    {
        if (actor.empty())
            return "walletCash < actorTab";
        return "walletCash < actorTab:" + actor;
    }
    if (choice.value("requiresPositiveActorTab", false))
        return formatPositiveActorTabEventId(actor);
    if (choice.value("requiresSaloonRoomAvailable", false))
        return "saloonRoomAvailable";
    return {};
}

} // namespace

void DialogFlowCanvas::migrateChoiceTree(
    const nlohmann::json& choice,
    const std::string& pointer,
    int parentId,
    float x,
    float y,
    int depth,
    std::map<std::string, int>& phaseIntroIds,
    std::map<std::string, int>& choicePlayerIds,
    std::vector<std::pair<int, std::string>>& deferredStartPhase,
    std::vector<std::pair<int, std::string>>& deferredResume,
    bool skipPlayerNode)
{
    if (!choice.is_object())
        return;

    const std::string label = choice.value("label", std::string());
    const std::string choiceId = choice.value("id", std::string());
    const std::string response = choice.value("response", std::string());
    const std::string responseTts = choice.value("ttsText", choice.value("response", std::string()));
    const std::string responseVoice = choice.value("ttsVoice", std::string());
    // Prefer dedicated tts field if present as string content elsewhere — many
    // entries only store spoken audio path; response is the on-screen text.

    const float dx = 100.0f;
    const float dy = 100.0f;

    int playerId = -1;
    if (!skipPlayerNode && (!label.empty() || !choiceId.empty()))
    {
        // Player options stay Off unless a dedicated player voice is authored later.
        playerId = migratePlayerLine(label, pointer, choiceId, x, y);
        if (!choiceId.empty())
            choicePlayerIds[choiceId] = playerId;
        connectWithPorts(parentId, playerId);
    }
    else if (!choiceId.empty() && choicePlayerIds.count(choiceId) == 0)
    {
        // Under a shared money-gate ask — keep id → player map if already set.
    }

    int responseParent = (playerId > 0) ? playerId : parentId;
    int responseId = -1;
    const std::string openInventoryEarly =
        choice.value("openActorInventory", std::string());
    // When opening actor inventory, the shop pitch lives on the ActorInventory
    // node — skip a separate response Actor that would duplicate it (#60).
    if (!response.empty() && openInventoryEarly.empty())
    {
        responseId = migrateActorLine(
            response,
            responseTts == response ? std::string() : responseTts,
            pointer,
            x,
            y + (skipPlayerNode ? 0.0f : dy),
            responseVoice);
        if (DialogFlowNode* n = findNode(responseId))
            n->sourceChoiceId = choiceId;
        connectWithPorts(responseParent, responseId);
        responseParent = responseId;
    }

    // Side-effect nodes after the response (or player if no response).
    int chainParent = responseParent;
    if (choice.contains("grantItem") && choice["grantItem"].is_string())
    {
        const int itemNode = allocNode(
            DialogNodeKind::GetItem, x + dx * 0.5f, y + dy * (responseId > 0 ? 2.0f : 1.0f));
        if (DialogFlowNode* n = findNode(itemNode))
        {
            n->itemId = choice["grantItem"].get<std::string>();
            n->jsonPointer = pointer + "/grantItem";
        }
        connectWithPorts(chainParent, itemNode);
        chainParent = itemNode;
    }
    if (choice.contains("grantStoryFlag") && choice["grantStoryFlag"].is_string())
    {
        const int flagNode = allocNode(
            DialogNodeKind::TriggerEvent,
            x + dx * 0.5f,
            y + dy * (responseId > 0 ? 2.2f : 1.2f));
        if (DialogFlowNode* n = findNode(flagNode))
        {
            n->eventId = choice["grantStoryFlag"].get<std::string>();
            n->jsonPointer = pointer + "/grantStoryFlag";
        }
        connectWithPorts(chainParent, flagNode);
        chainParent = flagNode;
    }

    // Actor inventory (#60): openActorInventory collapses the shop catalog into one
    // node instead of migrating nested browse_*/buy_* trees.
    const std::string& openInventory = openInventoryEarly;
    if (!openInventory.empty())
    {
        const float invY = y + dy * (skipPlayerNode ? 0.0f : 1.0f) + 20.0f;
        const int invId = allocNode(DialogNodeKind::ActorInventory, x, invY);
        if (DialogFlowNode* n = findNode(invId))
        {
            n->inventoryActorId = openInventory;
            n->dialogText = response;
            n->dialogTts = responseTts == response ? std::string() : responseTts;
            if (!responseVoice.empty() && isKnownBuiltinVoiceId(responseVoice))
                n->defaultVoice = normalizeVoiceId(responseVoice);
            n->dialogTtsAudio = choice.value("ttsAudio", std::string());
            n->jsonPointer = pointer + "/openActorInventory";
            n->sourceChoiceId = choiceId;
            n->title = "Inventory: " + openInventory;
        }
        connectWithPorts(chainParent, invId);
        chainParent = invId;
    }

    if (choice.contains("startPhase") && choice["startPhase"].is_string())
        deferredStartPhase.push_back({chainParent, choice["startPhase"].get<std::string>()});
    if (choice.contains("resumeChoiceId") && choice["resumeChoiceId"].is_string())
        deferredResume.push_back({chainParent, choice["resumeChoiceId"].get<std::string>()});

    // Nested choices fan out under the response (or player), including money gates.
    // Skip when openActorInventory owns the catalog (runtime synthesizes stock).
    if (openInventory.empty()
        && choice.contains("choices")
        && choice["choices"].is_array())
    {
        const float childY = y + dy * (responseId > 0 ? 2.0f : 1.0f) + 20.0f;
        migrateChoicesArray(
            choice["choices"],
            pointer + "/choices",
            chainParent,
            x,
            childY,
            depth + 1,
            phaseIntroIds,
            choicePlayerIds,
            deferredStartPhase,
            deferredResume);
    }

    (void)phaseIntroIds;
}

void DialogFlowCanvas::migrateCollapsedMoneyGatePair(
    const nlohmann::json& cashChoice,
    const std::string& cashPointer,
    const nlohmann::json& insuffChoice,
    const std::string& insuffPointer,
    float amount,
    int parentId,
    float x,
    float y,
    int depth,
    std::map<std::string, int>& phaseIntroIds,
    std::map<std::string, int>& choicePlayerIds,
    std::vector<std::pair<int, std::string>>& deferredStartPhase,
    std::vector<std::pair<int, std::string>>& deferredResume)
{
    const std::string label = cashChoice.value(
        "label", insuffChoice.value("label", std::string()));
    const std::string cashId = cashChoice.value("id", std::string());
    const std::string insuffId = insuffChoice.value("id", std::string());

    const float dy = 100.0f;
    const float branchDx = 120.0f;

    // One shared player ask — runtime shows only one of the two choices.
    const int playerId =
        migratePlayerLine(label, cashPointer, cashId, x, y);
    if (!cashId.empty())
        choicePlayerIds[cashId] = playerId;
    if (!insuffId.empty())
        choicePlayerIds[insuffId] = playerId;
    connectWithPorts(parentId, playerId);

    // Event gate: pass = can pay, fail = insufficient (comp / tab path).
    const int gateId = allocNode(DialogNodeKind::Event, x, y + dy);
    if (DialogFlowNode* n = findNode(gateId))
    {
        n->eventId = formatMoneyGateEventId(amount);
        n->title = n->eventId;
        n->jsonPointer = cashPointer;
        n->sourceChoiceId = cashId;
    }
    connectWithPorts(playerId, gateId);

    migrateChoiceTree(
        cashChoice,
        cashPointer,
        gateId,
        x - branchDx * 0.5f,
        y + dy * 2.0f,
        depth + 1,
        phaseIntroIds,
        choicePlayerIds,
        deferredStartPhase,
        deferredResume,
        true);
    migrateChoiceTree(
        insuffChoice,
        insuffPointer,
        gateId,
        x + branchDx * 0.5f,
        y + dy * 2.0f,
        depth + 1,
        phaseIntroIds,
        choicePlayerIds,
        deferredStartPhase,
        deferredResume,
        true);
}

void DialogFlowCanvas::migrateBranchingAvailabilityPair(
    const nlohmann::json& passChoice,
    const std::string& passPointer,
    const nlohmann::json& failChoice,
    const std::string& failPointer,
    const std::string& eventId,
    int parentId,
    float x,
    float y,
    int depth,
    std::map<std::string, int>& phaseIntroIds,
    std::map<std::string, int>& choicePlayerIds,
    std::vector<std::pair<int, std::string>>& deferredStartPhase,
    std::vector<std::pair<int, std::string>>& deferredResume)
{
    const float dy = 100.0f;
    const float branchDx = 130.0f;

    const int gateId = allocNode(DialogNodeKind::Event, x, y);
    if (DialogFlowNode* n = findNode(gateId))
    {
        n->eventId = eventId;
        n->title = eventId;
        n->jsonPointer = passPointer;
        n->sourceChoiceId = passChoice.value("id", std::string());
    }
    connectWithPorts(parentId, gateId);

    // Keep distinct player labels under pass / fail.
    migrateChoiceTree(
        passChoice,
        passPointer,
        gateId,
        x - branchDx * 0.5f,
        y + dy,
        depth + 1,
        phaseIntroIds,
        choicePlayerIds,
        deferredStartPhase,
        deferredResume,
        false);
    migrateChoiceTree(
        failChoice,
        failPointer,
        gateId,
        x + branchDx * 0.5f,
        y + dy,
        depth + 1,
        phaseIntroIds,
        choicePlayerIds,
        deferredStartPhase,
        deferredResume,
        false);
}

void DialogFlowCanvas::migrateChoiceWithSoloAvailabilityGate(
    const nlohmann::json& choice,
    const std::string& pointer,
    const std::string& eventId,
    int parentId,
    float x,
    float y,
    int depth,
    std::map<std::string, int>& phaseIntroIds,
    std::map<std::string, int>& choicePlayerIds,
    std::vector<std::pair<int, std::string>>& deferredStartPhase,
    std::vector<std::pair<int, std::string>>& deferredResume)
{
    const std::string label = choice.value("label", std::string());
    const std::string choiceId = choice.value("id", std::string());
    const float dy = 100.0f;

    int attachParent = parentId;
    if (!label.empty() || !choiceId.empty())
    {
        const int playerId =
            migratePlayerLine(label, pointer, choiceId, x, y);
        if (!choiceId.empty())
            choicePlayerIds[choiceId] = playerId;
        connectWithPorts(parentId, playerId);
        attachParent = playerId;
    }

    const int gateId = allocNode(DialogNodeKind::Event, x, y + dy);
    if (DialogFlowNode* n = findNode(gateId))
    {
        n->eventId = eventId;
        n->title = eventId;
        n->jsonPointer = pointer;
        n->sourceChoiceId = choiceId;
    }
    connectWithPorts(attachParent, gateId);

    // Pass only — fail port stays empty (choice hidden when condition fails).
    migrateChoiceTree(
        choice,
        pointer,
        gateId,
        x,
        y + dy * 2.0f,
        depth + 1,
        phaseIntroIds,
        choicePlayerIds,
        deferredStartPhase,
        deferredResume,
        true);
}

void DialogFlowCanvas::migrateChoicesArray(
    const nlohmann::json& choices,
    const std::string& arrayPointer,
    int parentId,
    float originX,
    float originY,
    int depth,
    std::map<std::string, int>& phaseIntroIds,
    std::map<std::string, int>& choicePlayerIds,
    std::vector<std::pair<int, std::string>>& deferredStartPhase,
    std::vector<std::pair<int, std::string>>& deferredResume)
{
    if (!choices.is_array())
        return;

    const int count = static_cast<int>(choices.size());
    if (count <= 0)
        return;

    enum class PairKind
    {
        None,
        MoneyCollapse,
        ActorTabBranch
    };

    const float dx = 110.0f;
    std::vector<bool> consumed(static_cast<size_t>(count), false);

    auto findPair = [&](int i, PairKind& outKind, int& outJ) {
        outKind = PairKind::None;
        outJ = -1;
        for (int j = i + 1; j < count; ++j)
        {
            if (consumed[static_cast<size_t>(j)])
                continue;
            const nlohmann::json& a = choices[static_cast<size_t>(i)];
            const nlohmann::json& b = choices[static_cast<size_t>(j)];
            if (isMoneyAvailabilityPair(a, b))
            {
                outKind = PairKind::MoneyCollapse;
                outJ = j;
                return;
            }
            if (isActorTabPayAvailabilityPair(a, b))
            {
                outKind = PairKind::ActorTabBranch;
                outJ = j;
                return;
            }
        }
    };

    int slotCount = 0;
    for (int i = 0; i < count; ++i)
    {
        if (consumed[static_cast<size_t>(i)])
            continue;
        PairKind kind = PairKind::None;
        int j = -1;
        findPair(i, kind, j);
        if (j >= 0)
        {
            consumed[static_cast<size_t>(i)] = true;
            consumed[static_cast<size_t>(j)] = true;
        }
        else
            consumed[static_cast<size_t>(i)] = true;
        ++slotCount;
    }
    std::fill(consumed.begin(), consumed.end(), false);

    const float spread = dx * std::max(1.0f, static_cast<float>(slotCount));
    float childX = originX - spread * 0.5f + dx * 0.5f;
    const float childY = originY;

    for (int i = 0; i < count; ++i)
    {
        if (consumed[static_cast<size_t>(i)])
            continue;

        PairKind kind = PairKind::None;
        int pairJ = -1;
        findPair(i, kind, pairJ);

        if (kind == PairKind::MoneyCollapse && pairJ >= 0)
        {
            const nlohmann::json& a = choices[static_cast<size_t>(i)];
            const nlohmann::json& b = choices[static_cast<size_t>(pairJ)];
            const bool aIsCash = a.value("requiresMoney", 0.0f) > 0.0f;
            const nlohmann::json& cash = aIsCash ? a : b;
            const nlohmann::json& insuff = aIsCash ? b : a;
            const int cashIdx = aIsCash ? i : pairJ;
            const int insuffIdx = aIsCash ? pairJ : i;
            migrateCollapsedMoneyGatePair(
                cash,
                arrayPointer + "/" + std::to_string(cashIdx),
                insuff,
                arrayPointer + "/" + std::to_string(insuffIdx),
                cash.value("requiresMoney", 0.0f),
                parentId,
                childX,
                childY,
                depth,
                phaseIntroIds,
                choicePlayerIds,
                deferredStartPhase,
                deferredResume);
            consumed[static_cast<size_t>(i)] = true;
            consumed[static_cast<size_t>(pairJ)] = true;
        }
        else if (kind == PairKind::ActorTabBranch && pairJ >= 0)
        {
            const nlohmann::json& a = choices[static_cast<size_t>(i)];
            const nlohmann::json& b = choices[static_cast<size_t>(pairJ)];
            const bool aIsPass = a.value("requiresPayActorTabInFull", false);
            const nlohmann::json& pass = aIsPass ? a : b;
            const nlohmann::json& fail = aIsPass ? b : a;
            const int passIdx = aIsPass ? i : pairJ;
            const int failIdx = aIsPass ? pairJ : i;
            const std::string actor = pass.value(
                "tabActor", fail.value("tabActor", std::string()));
            migrateBranchingAvailabilityPair(
                pass,
                arrayPointer + "/" + std::to_string(passIdx),
                fail,
                arrayPointer + "/" + std::to_string(failIdx),
                formatActorTabGateEventId(actor),
                parentId,
                childX,
                childY,
                depth,
                phaseIntroIds,
                choicePlayerIds,
                deferredStartPhase,
                deferredResume);
            consumed[static_cast<size_t>(i)] = true;
            consumed[static_cast<size_t>(pairJ)] = true;
        }
        else
        {
            const nlohmann::json& choice = choices[static_cast<size_t>(i)];
            const std::string pointer =
                arrayPointer + "/" + std::to_string(i);
            const std::string gateId = soloAvailabilityEventId(choice);
            if (!gateId.empty())
            {
                migrateChoiceWithSoloAvailabilityGate(
                    choice,
                    pointer,
                    gateId,
                    parentId,
                    childX,
                    childY,
                    depth,
                    phaseIntroIds,
                    choicePlayerIds,
                    deferredStartPhase,
                    deferredResume);
            }
            else
            {
                migrateChoiceTree(
                    choice,
                    pointer,
                    parentId,
                    childX,
                    childY,
                    depth,
                    phaseIntroIds,
                    choicePlayerIds,
                    deferredStartPhase,
                    deferredResume);
            }
            consumed[static_cast<size_t>(i)] = true;
        }
        childX += dx;
    }
}

void DialogFlowCanvas::migratePhase(
    const nlohmann::json& phase,
    size_t phaseIndex,
    const std::string& sceneId,
    bool connectFromStart,
    float originX,
    float originY,
    std::map<std::string, int>& phaseIntroIds,
    std::map<std::string, int>& choicePlayerIds,
    std::vector<std::pair<int, std::string>>& deferredStartPhase,
    std::vector<std::pair<int, std::string>>& deferredResume)
{
    if (!phase.is_object())
        return;

    const std::string phasePtr =
        "/" + sceneId + "/speakPhases/" + std::to_string(phaseIndex);
    const std::string phaseId = phase.value("id", std::string());
    const std::string intro = phase.value("intro", std::string());
    const std::string text = phase.value("text", std::string());
    const std::string opener = !intro.empty() ? intro : text;
    const std::string openerTts = phase.value("ttsText", std::string());
    const std::string openerVoice = phase.value("ttsVoice", std::string());
    const std::string resumeVoice = phase.value("resumeTtsVoice", std::string());

    int openerId = -1;
    if (!opener.empty())
    {
        openerId = migrateActorLine(
            opener,
            openerTts,
            phasePtr + (intro.empty() ? "/text" : "/intro"),
            originX,
            originY,
            openerVoice);
        if (DialogFlowNode* n = findNode(openerId))
            n->sourcePhaseId = phaseId;
        if (!phaseId.empty())
            phaseIntroIds[phaseId] = openerId;
        if (connectFromStart)
            addEdge(0, 0, openerId);
    }
    else
    {
        // Phase with only choices — synthetic hub under Start.
        openerId = allocNode(DialogNodeKind::ActorDialog, originX, originY);
        if (DialogFlowNode* n = findNode(openerId))
        {
            n->dialogText = phaseId.empty() ? "(phase)" : phaseId;
            n->sourcePhaseId = phaseId;
            n->jsonPointer = phasePtr;
            if (!openerVoice.empty() && isKnownBuiltinVoiceId(openerVoice))
                n->defaultVoice = normalizeVoiceId(openerVoice);
        }
        if (!phaseId.empty())
            phaseIntroIds[phaseId] = openerId;
        if (connectFromStart)
            addEdge(0, 0, openerId);
    }

    // resumeIntro is a revisit-only line (ConversationManager::resumeScriptedPhase).
    // Stash it on the opener Actor — do not wire a first-visit sibling that races
    // "What do you have on offer?" on the flowchart (#60).
    if (openerId > 0 && phase.contains("resumeIntro") && phase["resumeIntro"].is_string()
        && !phase["resumeIntro"].get<std::string>().empty())
    {
        if (DialogFlowNode* n = findNode(openerId))
        {
            n->resumeIntroText = phase["resumeIntro"].get<std::string>();
            n->resumeIntroTts = phase.value("resumeTtsText", std::string());
            if (!resumeVoice.empty() && isKnownBuiltinVoiceId(resumeVoice))
                n->resumeIntroVoice = normalizeVoiceId(resumeVoice);
            else if (!openerVoice.empty())
                n->resumeIntroVoice = openerVoice;
            n->resumeIntroTtsAudio = phase.value("resumeTtsAudio", std::string());
        }
    }

    if (phase.contains("choices") && phase["choices"].is_array())
    {
        migrateChoicesArray(
            phase["choices"],
            phasePtr + "/choices",
            openerId,
            originX,
            originY + 110.0f,
            0,
            phaseIntroIds,
            choicePlayerIds,
            deferredStartPhase,
            deferredResume);
    }

    // Random lines pool — each line as Actor Dialog under opener.
    if (phase.contains("lines") && phase["lines"].is_array())
    {
        const auto& lines = phase["lines"];
        float lx = originX - 40.0f * static_cast<float>(lines.size());
        const float ly = originY + 110.0f;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const nlohmann::json& line = lines[i];
            if (!line.is_object())
                continue;
            const std::string linePtr = phasePtr + "/lines/" + std::to_string(i);
            const std::string body = line.value("text", line.value("response", std::string()));
            const std::string lineTts = line.value("ttsText", std::string());
            const std::string lineVoice = line.value("ttsVoice", std::string());
            const int lineId = migrateActorLine(
                body, lineTts, linePtr, lx, ly, lineVoice);
            connectWithPorts(openerId, lineId);
            if (line.value("allowAttack", false))
            {
                const int atk = allocNode(DialogNodeKind::Attack, lx, ly + 100.0f);
                if (DialogFlowNode* n = findNode(atk))
                {
                    n->combatantId = line.value("attackEncounterId", std::string());
                    n->jsonPointer = linePtr;
                }
                connectWithPorts(lineId, atk);
            }
            if (line.contains("choices") && line["choices"].is_array())
            {
                float cx = lx - 40.0f;
                for (size_t c = 0; c < line["choices"].size(); ++c)
                {
                    migrateChoiceTree(
                        line["choices"][c],
                        linePtr + "/choices/" + std::to_string(c),
                        lineId,
                        cx,
                        ly + 100.0f,
                        1,
                        phaseIntroIds,
                        choicePlayerIds,
                        deferredStartPhase,
                        deferredResume);
                    cx += 100.0f;
                }
            }
            lx += 100.0f;
        }
    }
}

void DialogFlowCanvas::migrateFromTreeSelection(
    const std::string& treeKey,
    const std::string& sceneId)
{
    if (docs == nullptr || sceneId.empty() || !docs->conversationsLoaded)
        return;
    if (!docs->conversationsRoot.contains(sceneId)
        || !docs->conversationsRoot[sceneId].is_object())
        return;

    const nlohmann::json& sceneNode = docs->conversationsRoot[sceneId];
    if (!sceneNode.contains("speakPhases") || !sceneNode["speakPhases"].is_array()
        || sceneNode["speakPhases"].empty())
    {
        clearGraph();
        migratedScope = sceneId + "|empty";
        status = "No speakPhases for " + sceneId;
        return;
    }

    const nlohmann::json& phases = sceneNode["speakPhases"];

    // Resolve scope: whole scene, one actor, or one phase.
    int onlyPhaseIndex = -1;
    std::string onlyActor;
    std::string scope = "scene:" + sceneId;

    if (treeKey.rfind("phase:/", 0) == 0)
    {
        // phase:/sceneId/speakPhases/N
        const std::string marker = "/speakPhases/";
        const size_t at = treeKey.find(marker);
        if (at != std::string::npos)
        {
            const std::string rest = treeKey.substr(at + marker.size());
            onlyPhaseIndex = std::atoi(rest.c_str());
            scope = "phase:" + sceneId + ":" + std::to_string(onlyPhaseIndex);
        }
    }
    else if (treeKey.rfind("choice:/", 0) == 0 || treeKey.rfind("narrative-conv:/", 0) == 0)
    {
        const std::string marker = "/speakPhases/";
        const size_t at = treeKey.find(marker);
        if (at != std::string::npos)
        {
            const std::string rest = treeKey.substr(at + marker.size());
            onlyPhaseIndex = std::atoi(rest.c_str());
            scope = "phase:" + sceneId + ":" + std::to_string(onlyPhaseIndex);
        }
    }
    else if (treeKey.rfind("actor:", 0) == 0)
    {
        // actor:<scene>:<actorId>
        const std::string prefix = "actor:" + sceneId + ":";
        if (treeKey.rfind(prefix, 0) == 0)
        {
            onlyActor = treeKey.substr(prefix.size());
            scope = "actor:" + sceneId + ":" + onlyActor;
        }
    }

    // Bump when migrate topology changes so an already-open scope remigrates.
    constexpr const char* kMigrateRev = "|mgr5-actorInventoryNode";
    const std::string scoped = scope + kMigrateRev;
    if (scoped == migratedScope && !nodes.empty())
        return; // already showing this graph

    clearGraph();
    migratedScope = scoped;

    std::map<std::string, int> phaseIntroIds;
    std::map<std::string, int> choicePlayerIds;
    std::vector<std::pair<int, std::string>> deferredStartPhase;
    std::vector<std::pair<int, std::string>> deferredResume;

    auto phaseActor = [](const nlohmann::json& phase) -> std::string {
        if (phase.contains("actorId") && phase["actorId"].is_string())
            return phase["actorId"].get<std::string>();
        return phase.value("id", std::string());
    };

    bool startUsed = false;
    float colX = 120.0f;
    const float baseY = 180.0f;
    int migratedPhases = 0;

    for (size_t i = 0; i < phases.size(); ++i)
    {
        const nlohmann::json& phase = phases[i];
        if (!phase.is_object())
            continue;
        if (onlyPhaseIndex >= 0 && static_cast<int>(i) != onlyPhaseIndex)
            continue;
        if (!onlyActor.empty() && phaseActor(phase) != onlyActor)
            continue;

        const bool fromStart = !startUsed;
        migratePhase(
            phase,
            i,
            sceneId,
            fromStart,
            colX,
            baseY,
            phaseIntroIds,
            choicePlayerIds,
            deferredStartPhase,
            deferredResume);
        if (fromStart)
            startUsed = true;
        ++migratedPhases;
        colX += 420.0f;
    }

    // Resolve cross-phase links.
    for (const auto& link : deferredStartPhase)
    {
        auto it = phaseIntroIds.find(link.second);
        if (it != phaseIntroIds.end())
            connectWithPorts(link.first, it->second);
    }
    for (const auto& link : deferredResume)
    {
        auto it = choicePlayerIds.find(link.second);
        if (it != choicePlayerIds.end())
            connectWithPorts(link.first, it->second);
    }

    // Topology first, then a proper layered layout (stops the pile-up in the screenshot).
    relayoutGraph();

    selectedNodeId = 0;
    status = "Migrated " + std::to_string(migratedPhases) + " phase(s), "
        + std::to_string(nodes.size()) + " nodes from " + sceneId
        + " (editor-local)";
}

void DialogFlowCanvas::relayoutGraph()
{
    if (nodes.empty())
        return;

    // Slightly roomier than the old 100×112 so sibling choice rows and Event
    // gates read as a tree (issue #61), closer to Scenes map gaps.
    constexpr float kPitchX = 120.0f;
    constexpr float kPitchY = 128.0f;
    constexpr float kStartWorldX = 120.0f; // keep in sync with anonymous start pos
    const float anchorX = kStartWorldX + kStartSize * 0.5f;
    const float originY = 176.0f;

    std::map<int, std::vector<int>> children;
    std::map<int, std::vector<int>> parents;
    for (const DialogFlowEdge& e : edges)
    {
        children[e.fromId].push_back(e.toId);
        parents[e.toId].push_back(e.fromId);
    }

    // Longest-path layers from Start (0). Back-edges (resume) do not pull a
    // node upward once placed — that keeps the tree readable.
    std::map<int, int> layer;
    layer[0] = 0;
    std::queue<int> q;
    q.push(0);
    std::map<int, int> visits;
    while (!q.empty())
    {
        const int u = q.front();
        q.pop();
        if (++visits[u] > static_cast<int>(nodes.size()) + 2)
            continue; // cycle guard
        const int lu = layer[u];
        for (int v : children[u])
        {
            auto it = layer.find(v);
            if (it == layer.end())
            {
                layer[v] = lu + 1;
                q.push(v);
            }
            else if (it->second < lu + 1)
            {
                // Only deepen (never shorten) — ignore pure back-edges.
                it->second = lu + 1;
                q.push(v);
            }
        }
    }

    // Orphans / unreachable: park in fresh layers to the side.
    int maxLayer = 0;
    for (const auto& kv : layer)
        maxLayer = std::max(maxLayer, kv.second);
    for (const DialogFlowNode& n : nodes)
    {
        if (layer.find(n.id) == layer.end())
            layer[n.id] = ++maxLayer;
        else
            maxLayer = std::max(maxLayer, layer[n.id]);
    }

    std::vector<std::vector<int>> layers(static_cast<size_t>(maxLayer) + 1);
    for (const DialogFlowNode& n : nodes)
    {
        const int L = layer[n.id];
        if (L >= 0 && L <= maxLayer)
            layers[static_cast<size_t>(L)].push_back(n.id);
    }

    // Stable initial order by id, then barycenter sweeps to reduce crossings.
    for (auto& row : layers)
        std::sort(row.begin(), row.end());

    auto indexInLayer = [&](int nodeId, int L) -> float {
        if (L < 0 || L > maxLayer)
            return 0.0f;
        const auto& row = layers[static_cast<size_t>(L)];
        for (size_t i = 0; i < row.size(); ++i)
        {
            if (row[i] == nodeId)
                return static_cast<float>(i);
        }
        return static_cast<float>(row.size()) * 0.5f;
    };

    for (int pass = 0; pass < 4; ++pass)
    {
        // Downward: order by average parent index.
        for (int L = 1; L <= maxLayer; ++L)
        {
            auto& row = layers[static_cast<size_t>(L)];
            std::vector<std::pair<float, int>> keyed;
            keyed.reserve(row.size());
            for (int id : row)
            {
                float sum = 0.0f;
                int count = 0;
                for (int p : parents[id])
                {
                    const int pL = layer.count(p) ? layer[p] : (L - 1);
                    if (pL == L - 1 || p == 0)
                    {
                        sum += (p == 0) ? 0.0f : indexInLayer(p, pL);
                        ++count;
                    }
                }
                const float key = count > 0 ? (sum / static_cast<float>(count))
                                           : static_cast<float>(id);
                keyed.push_back({key, id});
            }
            std::stable_sort(keyed.begin(), keyed.end(),
                [](const auto& a, const auto& b) {
                    if (a.first != b.first)
                        return a.first < b.first;
                    return a.second < b.second;
                });
            for (size_t i = 0; i < keyed.size(); ++i)
                row[i] = keyed[i].second;
        }

        // Upward: order by average child index.
        for (int L = maxLayer - 1; L >= 1; --L)
        {
            auto& row = layers[static_cast<size_t>(L)];
            std::vector<std::pair<float, int>> keyed;
            keyed.reserve(row.size());
            for (int id : row)
            {
                float sum = 0.0f;
                int count = 0;
                for (int c : children[id])
                {
                    if (!layer.count(c))
                        continue;
                    const int cL = layer[c];
                    if (cL == L + 1)
                    {
                        sum += indexInLayer(c, cL);
                        ++count;
                    }
                }
                const float key = count > 0 ? (sum / static_cast<float>(count))
                                           : static_cast<float>(id);
                keyed.push_back({key, id});
            }
            std::stable_sort(keyed.begin(), keyed.end(),
                [](const auto& a, const auto& b) {
                    if (a.first != b.first)
                        return a.first < b.first;
                    return a.second < b.second;
                });
            for (size_t i = 0; i < keyed.size(); ++i)
                row[i] = keyed[i].second;
        }
    }

    // Assign coordinates: each layer centered under Start, generous pitch.
    for (int L = 1; L <= maxLayer; ++L)
    {
        const auto& row = layers[static_cast<size_t>(L)];
        if (row.empty())
            continue;
        const float totalW = static_cast<float>(row.size() - 1) * kPitchX;
        const float left = anchorX - totalW * 0.5f;
        for (size_t i = 0; i < row.size(); ++i)
        {
            if (DialogFlowNode* n = findNode(row[i]))
            {
                n->x = left + static_cast<float>(i) * kPitchX - kNodeW * 0.5f;
                n->y = originY + static_cast<float>(L - 1) * kPitchY;
            }
        }
    }

    // Re-index child ports left-to-right by child X so wires leave in order.
    std::map<int, std::vector<DialogFlowEdge*>> outsByFrom;
    for (DialogFlowEdge& e : edges)
        outsByFrom[e.fromId].push_back(&e);
    for (auto& kv : outsByFrom)
    {
        auto& outs = kv.second;
        std::sort(outs.begin(), outs.end(), [&](DialogFlowEdge* a, DialogFlowEdge* b) {
            const DialogFlowNode* na = findNode(a->toId);
            const DialogFlowNode* nb = findNode(b->toId);
            const float ax = na ? na->x : 0.0f;
            const float bx = nb ? nb->x : 0.0f;
            return ax < bx;
        });
        int maxChildren = 1;
        if (kv.first == 0)
            maxChildren = 1;
        else if (const DialogFlowNode* from = findNode(kv.first))
            maxChildren = dialogNodeChildCount(from->kind);
        for (size_t i = 0; i < outs.size(); ++i)
        {
            outs[i]->fromChildIndex = static_cast<int>(
                std::min(i, static_cast<size_t>(std::max(0, maxChildren - 1))));
        }
    }

    scrollX = 0.0f;
    scrollY = 0.0f;
    clampScroll();
}

} // namespace timberline_editor
