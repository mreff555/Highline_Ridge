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
#include <cmath>

namespace timberline_editor
{

namespace
{

std::vector<std::string> wrapUiText(
    Font font,
    const std::string& text,
    float fontSize,
    float maxWidth)
{
    std::vector<std::string> lines;
    if (text.empty() || maxWidth < 8.0f)
    {
        lines.push_back(text);
        return lines;
    }

    std::string word;
    std::string line;
    auto flushWord = [&]() {
        if (word.empty())
            return;
        const std::string candidate = line.empty() ? word : (line + " " + word);
        if (MeasureTextEx(font, candidate.c_str(), fontSize, 1.0f).x <= maxWidth
            || line.empty())
        {
            line = candidate;
        }
        else
        {
            lines.push_back(line);
            line = word;
        }
        word.clear();
    };

    for (char ch : text)
    {
        if (ch == '\n')
        {
            flushWord();
            lines.push_back(line);
            line.clear();
            continue;
        }
        if (ch == ' ' || ch == '\t')
        {
            flushWord();
            continue;
        }
        word.push_back(ch);
    }
    flushWord();
    if (!line.empty() || lines.empty())
        lines.push_back(line);
    return lines;
}

std::string clipUiLine(Font font, const std::string& text, float fontSize, float maxWidth)
{
    if (text.empty() || MeasureTextEx(font, text.c_str(), fontSize, 1.0f).x <= maxWidth)
        return text;
    std::string out = text;
    const std::string ellipsis = "...";
    while (!out.empty()
           && MeasureTextEx(font, (out + ellipsis).c_str(), fontSize, 1.0f).x > maxWidth)
        out.pop_back();
    return out + ellipsis;
}

} // namespace

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
    status = "Bound " + selectedBinding + " -> " + toId;
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
    status = "Created " + binding + " -> " + toId;
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
    const float dialogH = std::min(520.0f, screenH - 40.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    const float pad = 18.0f;
    const float textMaxW = dialog.width - pad * 2.0f;
    float y = dialog.y + 14.0f;

    DrawTextEx(bold, "Manage Use Transition", {dialog.x + pad, y}, kFontHeading, 1.0f, kTextPrimary);
    y += 28.0f;

    const std::string endpoint = "From " + fromId + "  ->  " + toId;
    for (const std::string& line : wrapUiText(font, endpoint, kFontTiny, textMaxW))
    {
        DrawTextEx(font, line.c_str(), {dialog.x + pad, y}, kFontTiny, 1.0f, kTextMuted);
        y += 16.0f;
    }
    y += 8.0f;

    // Footer reserves: help (wrapped) + status + button row.
    const std::string help =
        "Select a binding, then Accept to point it at the wire destination. "
        "Create new adds useExit (if free) or a stub interaction.";
    const auto helpLines = wrapUiText(font, help, kFontTiny, textMaxW);
    const float helpH = static_cast<float>(helpLines.size()) * 16.0f;
    const float statusH = 20.0f;
    const float btnH = 32.0f;
    const float footerGap = 10.0f;
    const float footerH = helpH + footerGap + statusH + footerGap + btnH + 14.0f;

    const Rectangle list = {
        dialog.x + 16.0f,
        y,
        dialog.width - 32.0f,
        std::max(80.0f, dialogH - (y - dialog.y) - footerH)};
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
    float rowY = list.y + 4.0f - listScroll;
    if (rows.empty())
    {
        const std::string emptyHelp =
            "No Use bindings yet. Create one to write useExit or a stub interaction.";
        for (const std::string& line :
             wrapUiText(font, emptyHelp, kFontTiny, list.width - 20.0f))
        {
            DrawTextEx(
                font,
                line.c_str(),
                {list.x + 10.0f, rowY + 8.0f},
                kFontTiny,
                1.0f,
                kTextMuted);
            rowY += 16.0f;
        }
    }
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        Rectangle rowRect = {list.x + 4.0f, rowY, list.width - 8.0f, rowH - 2.0f};
        const bool selected = row.binding == selectedBinding;
        if (selected)
            DrawRectangleRec(rowRect, Color{70, 52, 28, 220});
        else if (CheckCollisionPointRec(mouse, rowRect))
            DrawRectangleRec(rowRect, Color{40, 36, 48, 200});
        const std::string line =
            row.label + "  [" + row.binding + "]  ->  " + row.target;
        const std::string clipped = clipUiLine(font, line, kFontTiny, rowRect.width - 16.0f);
        DrawTextEx(
            font,
            clipped.c_str(),
            {rowRect.x + 8.0f, rowRect.y + 6.0f},
            kFontTiny,
            1.0f,
            selected ? kTextPrimary : kTextMuted);
        if (canClick && CheckCollisionPointRec(mouse, rowRect))
            selectedBinding = row.binding;
        rowY += rowH;
    }
    EndScissorMode();

    float footY = list.y + list.height + 8.0f;
    for (const std::string& line : helpLines)
    {
        DrawTextEx(font, line.c_str(), {dialog.x + pad, footY}, kFontTiny, 1.0f, kTextMuted);
        footY += 16.0f;
    }
    footY += footerGap;

    if (!status.empty() || !error.empty())
    {
        const std::string& msg = !error.empty() ? error : status;
        const Color col = !error.empty()
            ? Color{220, 100, 90, 255}
            : Color{120, 180, 120, 255};
        const std::string clipped = clipUiLine(font, msg, kFontTiny, textMaxW);
        DrawTextEx(font, clipped.c_str(), {dialog.x + pad, footY}, kFontTiny, 1.0f, col);
    }
    footY += statusH + footerGap;

    const float btnW = 110.0f;
    const float btnY = dialog.y + dialogH - btnH - 14.0f;
    // Keep Accept/Cancel on the right; Create/Clear on the left.
    Rectangle createBtn = {dialog.x + pad, btnY, btnW + 16.0f, btnH};
    Rectangle clearBtn = {createBtn.x + createBtn.width + 10.0f, btnY, btnW, btnH};
    Rectangle cancelBtn = {dialog.x + dialogW - btnW - pad, btnY, btnW, btnH};
    Rectangle acceptBtn = {cancelBtn.x - btnW - 10.0f, btnY, btnW, btnH};

    drawEditorButton(font, createBtn, "Create new", true, true);
    drawEditorButton(font, clearBtn, "Clear", true, !selectedBinding.empty());
    drawEditorButton(font, acceptBtn, "Accept", true, !selectedBinding.empty());
    drawEditorButton(font, cancelBtn, "Cancel", false, true);

    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, createBtn))
            createNewBinding();
        else if (CheckCollisionPointRec(mouse, clearBtn) && !selectedBinding.empty())
            clearSelected();
        else if (CheckCollisionPointRec(mouse, acceptBtn) && !selectedBinding.empty())
        {
            if (applySelected())
                closeDialog();
        }
        else if (CheckCollisionPointRec(mouse, cancelBtn))
            closeDialog();
        else if (!CheckCollisionPointRec(mouse, dialog))
            closeDialog();
    }
}

} // namespace timberline_editor
