/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneUseTransitionDialog.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>

namespace timberline_editor
{

void SceneUseTransitionDialog::refreshRows()
{
    rows.clear();
    if (!graph || fromId.empty())
        return;
    rows = graph->enumerateUseBindings(fromId);
    if (selectedBinding.empty() && !preferredBinding.empty())
        selectedBinding = preferredBinding;
    if (selectedBinding.empty() && !rows.empty())
        selectedBinding = rows.front().binding;
    // Keep selection if still present.
    bool found = false;
    for (const auto& row : rows)
    {
        if (row.binding == selectedBinding)
        {
            found = true;
            break;
        }
    }
    if (!found)
        selectedBinding = rows.empty() ? std::string{} : rows.front().binding;
}

void SceneUseTransitionDialog::openForLink(
    const std::string& fromMapNode,
    const std::string& toMapNode,
    const std::string& bindingHint)
{
    open = true;
    ignoreInputFrames = 2;
    waitMouseRelease = true;
    status.clear();
    error.clear();
    listScroll = 0.0f;
    fromId = fromMapNode;
    toId = toMapNode;
    preferredBinding = bindingHint;
    selectedBinding = bindingHint;
    refreshRows();
}

void SceneUseTransitionDialog::closeDialog()
{
    open = false;
    waitMouseRelease = false;
    ignoreInputFrames = 0;
    status.clear();
    error.clear();
    rows.clear();
    selectedBinding.clear();
    preferredBinding.clear();
}

bool SceneUseTransitionDialog::applySelected()
{
    if (!graph || fromId.empty() || toId.empty() || selectedBinding.empty())
    {
        error = "Select a Use binding first.";
        return false;
    }
    if (!graph->setUseBindingTarget(fromId, selectedBinding, toId))
    {
        error = "Failed to update binding.";
        return false;
    }
    status = "Bound " + selectedBinding + " → " + toId;
    error.clear();
    refreshRows();
    if (onSaved)
        onSaved();
    return true;
}

bool SceneUseTransitionDialog::clearSelected()
{
    if (!graph || fromId.empty() || selectedBinding.empty())
    {
        error = "Select a Use binding first.";
        return false;
    }
    if (!graph->clearUseBinding(fromId, selectedBinding))
    {
        error = "Failed to clear binding.";
        return false;
    }
    status = "Cleared " + selectedBinding;
    error.clear();
    selectedBinding.clear();
    preferredBinding.clear();
    refreshRows();
    if (onSaved)
        onSaved();
    return true;
}

bool SceneUseTransitionDialog::createNewBinding()
{
    if (!graph || fromId.empty() || toId.empty())
    {
        error = "Missing endpoints.";
        return false;
    }
    // Prefer filling empty useExit first.
    const auto existing = graph->enumerateUseBindings(fromId);
    bool hasUseExit = false;
    for (const auto& row : existing)
    {
        if (row.binding == "useExit")
        {
            hasUseExit = true;
            break;
        }
    }
    std::string binding;
    if (!hasUseExit)
    {
        if (!graph->setUseBindingTarget(fromId, "useExit", toId))
        {
            error = "Failed to set useExit.";
            return false;
        }
        binding = "useExit";
    }
    else
    {
        binding = graph->createUseInteractionBinding(fromId, toId);
        if (binding.empty())
        {
            error = "Failed to create interaction.";
            return false;
        }
    }
    selectedBinding = binding;
    preferredBinding = binding;
    status = "Created " + binding + " → " + toId;
    error.clear();
    refreshRows();
    if (onSaved)
        onSaved();
    return true;
}

void SceneUseTransitionDialog::handleInput(int /*screenW*/, int /*screenH*/)
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

void SceneUseTransitionDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool canClick = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !waitMouseRelease
        && ignoreInputFrames <= 0;

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);

    const float dialogW = std::min(560.0f, screenW - 40.0f);
    const float dialogH = std::min(480.0f, screenH - 40.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        "Manage Use Transition",
        {dialog.x + 18.0f, dialog.y + 14.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    DrawTextEx(
        font,
        ("From " + fromId + "  →  " + toId).c_str(),
        {dialog.x + 18.0f, dialog.y + 42.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float listTop = dialog.y + 72.0f;
    const float footerH = 56.0f;
    const Rectangle list = {
        dialog.x + 16.0f,
        listTop,
        dialog.width - 32.0f,
        dialogH - (listTop - dialog.y) - footerH - 36.0f};
    DrawRectangleRec(list, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(list, 1.0f, kPanelInnerEdge);

    const float rowH = 28.0f;
    const float contentH = std::max(rowH, static_cast<float>(rows.size()) * rowH + 8.0f);
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
        DrawTextEx(
            font,
            "No Use bindings yet. Create one to write useExit or a stub interaction.",
            {list.x + 10.0f, y + 8.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
    }
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        Rectangle rowRect = {list.x + 4.0f, y, list.width - 8.0f, rowH - 2.0f};
        const bool selected = row.binding == selectedBinding;
        if (selected)
            DrawRectangleRec(rowRect, Color{70, 52, 28, 220});
        else if (CheckCollisionPointRec(mouse, rowRect))
            DrawRectangleRec(rowRect, Color{40, 36, 48, 200});
        const std::string line = row.label + "  [" + row.binding + "]  →  " + row.target;
        DrawTextEx(
            font,
            line.c_str(),
            {rowRect.x + 8.0f, rowRect.y + 6.0f},
            kFontTiny,
            1.0f,
            selected ? kTextPrimary : kTextMuted);
        if (canClick && CheckCollisionPointRec(mouse, rowRect))
            selectedBinding = row.binding;
        y += rowH;
    }
    EndScissorMode();

    DrawTextEx(
        font,
        "Selecting Apply sets the chosen binding's target to the wire destination.",
        {dialog.x + 18.0f, list.y + list.height + 8.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float btnW = 110.0f;
    const float btnH = 32.0f;
    const float btnY = dialog.y + dialogH - btnH - 14.0f;
    Rectangle createBtn = {dialog.x + 18.0f, btnY, btnW + 20.0f, btnH};
    Rectangle applyBtn = {dialog.x + dialogW - btnW * 3.0f - 48.0f, btnY, btnW, btnH};
    Rectangle clearBtn = {dialog.x + dialogW - btnW * 2.0f - 32.0f, btnY, btnW, btnH};
    Rectangle closeBtn = {dialog.x + dialogW - btnW - 16.0f, btnY, btnW, btnH};

    drawEditorButton(font, createBtn, "Create new", true, true);
    drawEditorButton(font, applyBtn, "Apply", true, !selectedBinding.empty());
    drawEditorButton(font, clearBtn, "Clear", true, !selectedBinding.empty());
    drawEditorButton(font, closeBtn, "Close", false, true);

    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, createBtn))
            createNewBinding();
        else if (CheckCollisionPointRec(mouse, applyBtn) && !selectedBinding.empty())
            applySelected();
        else if (CheckCollisionPointRec(mouse, clearBtn) && !selectedBinding.empty())
            clearSelected();
        else if (CheckCollisionPointRec(mouse, closeBtn))
            closeDialog();
        else if (!CheckCollisionPointRec(mouse, dialog))
            closeDialog();
    }

    if (!status.empty() || !error.empty())
    {
        const std::string& msg = !error.empty() ? error : status;
        const Color col = !error.empty()
            ? Color{220, 100, 90, 255}
            : Color{120, 180, 120, 255};
        DrawTextEx(
            font,
            msg.c_str(),
            {createBtn.x + createBtn.width + 12.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            col);
    }
}

} // namespace timberline_editor
