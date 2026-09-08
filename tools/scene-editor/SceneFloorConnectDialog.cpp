/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneFloorConnectDialog.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "SceneDocument.h"

#include <algorithm>
#include <cmath>

namespace timberline_editor
{

namespace
{

void drawLinkedIcon(float cx, float cy, Color color)
{
    // Two small ports with a short link — "already connected".
    const float r = 3.5f;
    const float gap = 10.0f;
    DrawCircleV({cx - gap * 0.5f, cy}, r, color);
    DrawCircleV({cx + gap * 0.5f, cy}, r, color);
    DrawLineEx({cx - gap * 0.5f + r, cy}, {cx + gap * 0.5f - r, cy}, 1.5f, color);
}

} // namespace

void SceneFloorConnectDialog::refreshRows()
{
    rows.clear();
    if (!docs || !graph || sourceId.empty())
        return;

    sourceLevel = docs->scenes.hasMapPlacement(sourceId)
        ? docs->scenes.getLayout(sourceId).level
        : 0;

    const std::string currentUp = graph->getExitTarget(sourceId, "up");
    const std::string currentDown = graph->getExitTarget(sourceId, "down");

    // Adjacent floor only: Above → sourceLevel+1, Below → sourceLevel-1.
    const int targetLevel = connectAbove ? (sourceLevel + 1) : (sourceLevel - 1);
    std::vector<std::string> candidates;
    for (const std::string& id : graph->scenesOnLevel(targetLevel))
    {
        if (id.find('#') != std::string::npos)
            continue; // vertical links are parent scenes only
        if (id == sourceId)
            continue;
        candidates.push_back(id);
    }

    std::sort(candidates.begin(), candidates.end());

    for (const std::string& id : candidates)
    {
        Row row;
        row.sceneId = id;
        row.level = docs->scenes.getLayout(id).level;
        row.isCurrentLink = connectAbove ? (id == currentUp) : (id == currentDown);

        // Occupied if the target's reciprocal slot is taken by someone else.
        const std::string tUp = graph->getExitTarget(id, "up");
        const std::string tDown = graph->getExitTarget(id, "down");
        row.occupied = false;
        if (connectAbove)
        {
            if (!tDown.empty() && tDown != sourceId)
                row.occupied = true;
        }
        else
        {
            if (!tUp.empty() && tUp != sourceId)
                row.occupied = true;
        }

        // Current partner is selectable too so Accept can confirm / no-op;
        // occupied others stay locked.
        row.selectable = !row.occupied;
        rows.push_back(row);
    }

    // Drop selection if it left the list (e.g. after flipping Above/Below).
    bool stillVisible = false;
    for (const Row& row : rows)
    {
        if (row.sceneId == selectedId)
        {
            stillVisible = true;
            break;
        }
    }
    if (!stillVisible)
        selectedId.clear();
}

void SceneFloorConnectDialog::openForScene(const std::string& mapNodeId)
{
    std::string parent;
    std::string sub;
    timberline_engine::SceneDocument::parseMapNodeId(mapNodeId, parent, sub);
    if (parent.empty())
        parent = mapNodeId;

    open = true;
    ignoreInputFrames = 2;
    waitMouseRelease = true;
    status.clear();
    error.clear();
    listScroll = 0.0f;
    selectedId.clear();
    sourceId = parent;
    connectAbove = true;
    refreshRows();
    // Pre-select current above partner when present.
    const std::string currentUp = graph ? graph->getExitTarget(sourceId, "up") : "";
    if (!currentUp.empty())
        selectedId = currentUp;
}

void SceneFloorConnectDialog::closeDialog()
{
    open = false;
    waitMouseRelease = false;
    ignoreInputFrames = 0;
    status.clear();
    error.clear();
    rows.clear();
    sourceId.clear();
    selectedId.clear();
}

bool SceneFloorConnectDialog::selectedIsAcceptable() const
{
    if (selectedId.empty())
        return false;
    for (const Row& row : rows)
    {
        if (row.sceneId == selectedId)
            return row.selectable;
    }
    return false;
}

bool SceneFloorConnectDialog::applySelected()
{
    if (!graph || sourceId.empty() || !selectedIsAcceptable())
    {
        error = "Select a scene on the adjacent floor first.";
        return false;
    }
    // connectAbove: selected target sits above source.
    if (!graph->connectFloorLink(sourceId, selectedId, /*targetIsAbove=*/connectAbove))
    {
        error = "Could not connect floors (target may be occupied).";
        return false;
    }
    status = connectAbove
        ? ("Connected above -> " + selectedId)
        : ("Connected below -> " + selectedId);
    error.clear();
    if (onSaved)
        onSaved();
    closeDialog();
    return true;
}

void SceneFloorConnectDialog::handleInput(int /*screenW*/, int /*screenH*/)
{
    if (!open)
        return;
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }
    if (waitMouseRelease)
    {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            return;
        waitMouseRelease = false;
    }
    if (IsKeyPressed(KEY_ESCAPE))
        closeDialog();
}

void SceneFloorConnectDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool canClick = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !waitMouseRelease
        && ignoreInputFrames <= 0;

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);

    const float dialogW = std::min(520.0f, screenW - 40.0f);
    const float dialogH = std::min(520.0f, screenH - 40.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        "Connect to floor",
        {dialog.x + 18.0f, dialog.y + 14.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    DrawTextEx(
        font,
        ("From " + sourceId + "  (floor " + std::to_string(sourceLevel) + ")").c_str(),
        {dialog.x + 18.0f, dialog.y + 42.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    // Above / Below switch with floor hint in parentheses.
    const int hintLevel = connectAbove ? (sourceLevel + 1) : (sourceLevel - 1);
    const std::string switchLabel = connectAbove
        ? ("Above (" + std::to_string(hintLevel) + ")")
        : ("Below (" + std::to_string(hintLevel) + ")");
    DrawTextEx(
        font,
        "Direction",
        {dialog.x + 18.0f, dialog.y + 68.0f},
        kFontTiny,
        1.0f,
        kTextMuted);
    aboveBelowSwitchTrack = {dialog.x + 18.0f, dialog.y + 86.0f, 64.0f, 26.0f};
    bool toggled = false;
    // Reuse the ON/OFF track visually; label carries Above/Below + floor.
    drawOnOffSwitch(
        font,
        aboveBelowSwitchTrack,
        connectAbove,
        nullptr,
        false,
        toggled);
    DrawTextEx(
        bold,
        switchLabel.c_str(),
        {aboveBelowSwitchTrack.x + aboveBelowSwitchTrack.width + 12.0f,
         aboveBelowSwitchTrack.y + 4.0f},
        kFontSmall,
        1.0f,
        kTextPrimary);
    if (canClick && CheckCollisionPointRec(mouse, aboveBelowSwitchTrack))
        toggled = true;
    if (toggled)
    {
        connectAbove = !connectAbove;
        listScroll = 0.0f;
        selectedId.clear();
        refreshRows();
        // Pre-select current partner for the new direction, if any.
        if (graph)
        {
            const std::string cur = connectAbove
                ? graph->getExitTarget(sourceId, "up")
                : graph->getExitTarget(sourceId, "down");
            if (!cur.empty())
                selectedId = cur;
        }
    }
    const std::string listHint =
        "Showing floor " + std::to_string(hintLevel)
        + " only. Busy targets are dimmed.";
    DrawTextEx(
        font,
        listHint.c_str(),
        {dialog.x + 18.0f, dialog.y + 118.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float listTop = dialog.y + 140.0f;
    const float footerH = 56.0f;
    const Rectangle list = {
        dialog.x + 16.0f,
        listTop,
        dialog.width - 32.0f,
        dialogH - (listTop - dialog.y) - footerH};
    DrawRectangleRec(list, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(list, 1.0f, kPanelInnerEdge);

    const float rowH = 28.0f;
    const float contentH =
        std::max(rowH, static_cast<float>(rows.size()) * rowH + 8.0f);
    const float maxScroll = std::max(0.0f, contentH - list.height);
    if (CheckCollisionPointRec(mouse, list))
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            listScroll = std::clamp(listScroll - wheel * 28.0f, 0.0f, maxScroll);
    }
    listScroll = std::clamp(listScroll, 0.0f, maxScroll);

    BeginScissorMode(
        static_cast<int>(list.x),
        static_cast<int>(list.y),
        static_cast<int>(list.width),
        static_cast<int>(list.height));

    float y = list.y + 4.0f - listScroll;
    if (rows.empty())
    {
        const std::string emptyMsg =
            "No scenes on floor " + std::to_string(hintLevel) + ".";
        DrawTextEx(
            font,
            emptyMsg.c_str(),
            {list.x + 10.0f, y + 8.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
    }

    for (const Row& row : rows)
    {
        Rectangle rowRect = {list.x + 4.0f, y, list.width - 8.0f, rowH - 2.0f};
        const bool hovered = CheckCollisionPointRec(mouse, rowRect);
        const bool selected = (row.sceneId == selectedId);
        if (selected)
            DrawRectangleRec(rowRect, Color{70, 58, 36, 230});
        else if (row.selectable && hovered)
            DrawRectangleRec(rowRect, Color{50, 46, 60, 220});
        else if (row.isCurrentLink)
            DrawRectangleRec(rowRect, Color{40, 48, 58, 180});

        const Color textCol = row.selectable
            ? kTextPrimary
            : (row.isCurrentLink ? Color{160, 170, 185, 220} : kTextDisabled);

        const std::string label =
            row.sceneId + "  (floor " + std::to_string(row.level) + ")";
        DrawTextEx(
            font,
            label.c_str(),
            {rowRect.x + 8.0f, rowRect.y + 6.0f},
            kFontTiny,
            1.0f,
            textCol);

        if (row.isCurrentLink)
            drawLinkedIcon(
                rowRect.x + rowRect.width - 22.0f,
                rowRect.y + rowH * 0.45f,
                kUseArrow);
        else if (row.occupied)
            DrawTextEx(
                font,
                "busy",
                {rowRect.x + rowRect.width - 40.0f, rowRect.y + 6.0f},
                kFontTiny,
                1.0f,
                kTextDisabled);

        if (canClick && row.selectable && hovered)
        {
            selectedId = row.sceneId;
            error.clear();
        }

        y += rowH;
    }
    EndScissorMode();

    const float btnW = 110.0f;
    const float btnH = 32.0f;
    const float btnY = dialog.y + dialogH - btnH - 14.0f;
    Rectangle acceptBtn = {
        dialog.x + dialogW - btnW * 2.0f - 28.0f, btnY, btnW, btnH};
    Rectangle cancelBtn = {dialog.x + dialogW - btnW - 16.0f, btnY, btnW, btnH};
    const bool canAccept = selectedIsAcceptable();
    drawEditorButton(font, acceptBtn, "Accept", true, canAccept);
    drawEditorButton(font, cancelBtn, "Cancel", false, true);

    if (!status.empty() || !error.empty())
    {
        const std::string& msg = !error.empty() ? error : status;
        const Color col = !error.empty()
            ? Color{220, 100, 90, 255}
            : Color{120, 180, 120, 255};
        DrawTextEx(
            font,
            msg.c_str(),
            {dialog.x + 18.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            col);
    }

    if (canClick)
    {
        if (canAccept && CheckCollisionPointRec(mouse, acceptBtn))
            applySelected();
        else if (CheckCollisionPointRec(mouse, cancelBtn))
            closeDialog();
        else if (!CheckCollisionPointRec(mouse, dialog)
                 && !CheckCollisionPointRec(mouse, aboveBelowSwitchTrack))
            closeDialog();
    }
}

} // namespace timberline_editor
