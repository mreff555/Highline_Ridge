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

void insertUtf8(std::string& buffer, int& cursor, int codepoint)
{
    if (codepoint <= 0)
        return;
    cursor = std::clamp(cursor, 0, static_cast<int>(buffer.size()));
    char bytes[5] = {};
    int size = 0;
    if (codepoint < 0x80)
    {
        bytes[0] = static_cast<char>(codepoint);
        size = 1;
    }
    else if (codepoint <= 0x7FF)
    {
        bytes[0] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
        bytes[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        bytes[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 3;
    }
    else
    {
        bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    }
    buffer.insert(static_cast<size_t>(cursor), bytes, static_cast<size_t>(size));
    cursor += size;
}

void backspaceUtf8(std::string& buffer, int& cursor)
{
    if (cursor <= 0 || buffer.empty())
        return;
    const int prev = utf8PrevIndex(buffer, cursor);
    buffer.erase(static_cast<size_t>(prev), static_cast<size_t>(cursor - prev));
    cursor = prev;
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
    loadDetailsDraftFromSelection();
}

void SceneUseTransitionDialog::loadDetailsDraftFromSelection()
{
    detailsDraft.clear();
    detailsCursor = 0;
    if (!graph || fromId.empty() || selectedBinding.empty())
        return;
    detailsDraft = graph->getUseBindingDetails(fromId, selectedBinding);
    detailsCursor = static_cast<int>(detailsDraft.size());
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
    detailsFocused = false;
    fromId = fromMapNode;
    toId = toMapNode;
    preferredBinding = bindingHint;
    selectedBinding = bindingHint;
    refreshRows();
    if (graph)
        graph->ensureUseExitTransitionDefaults(fromId);
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
    detailsDraft.clear();
    detailsCursor = 0;
    detailsFocused = false;
}

bool SceneUseTransitionDialog::applySelected()
{
    if (!graph || fromId.empty() || toId.empty() || selectedBinding.empty())
    {
        error = "Select a Use binding first.";
        return false;
    }
    if (!graph->setUseBindingDetails(fromId, selectedBinding, detailsDraft))
    {
        // Details write can fail for stale rows; still try target update.
    }
    if (!graph->setUseBindingTarget(fromId, selectedBinding, toId))
    {
        error = "Failed to update binding.";
        return false;
    }
    graph->ensureUseExitTransitionDefaults(fromId);
    status = "Accepted " + selectedBinding + " -> " + toId;
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
    {
        if (detailsFocused)
            detailsFocused = false;
        else
            closeDialog();
        return;
    }

    if (!detailsFocused)
        return;

    detailsCursor = std::clamp(detailsCursor, 0, static_cast<int>(detailsDraft.size()));
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT))
        detailsCursor = utf8PrevIndex(detailsDraft, detailsCursor);
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT))
        detailsCursor = utf8NextIndex(detailsDraft, detailsCursor);
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        backspaceUtf8(detailsDraft, detailsCursor);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        insertUtf8(detailsDraft, detailsCursor, '\n');

    int codepoint = GetCharPressed();
    while (codepoint > 0)
    {
        if (codepoint >= 32)
            insertUtf8(detailsDraft, detailsCursor, codepoint);
        codepoint = GetCharPressed();
    }
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

    const float dialogW = std::min(620.0f, screenW - 40.0f);
    const float dialogH = std::min(600.0f, screenH - 40.0f);
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
    y += 6.0f;

    const std::string help =
        "Create new: add Direct Use (useExit) if free, else a repeatable interaction. "
        "Clear: remove the selected binding. "
        "Accept: save description, point the binding at the wire destination, close. "
        "Cancel: close without changing the destination (keeps an already-created binding).";
    const auto helpLines = wrapUiText(font, help, kFontTiny, textMaxW);
    const float helpH = static_cast<float>(helpLines.size()) * 15.0f;

    const float detailsLabelH = 18.0f;
    const float detailsBoxH = 72.0f;
    const float statusH = 18.0f;
    const float btnH = 32.0f;
    const float footerGap = 8.0f;
    const float footerH = helpH + footerGap + detailsLabelH + detailsBoxH + footerGap
        + statusH + footerGap + btnH + 14.0f;

    const Rectangle list = {
        dialog.x + 16.0f,
        y,
        dialog.width - 32.0f,
        std::max(72.0f, dialogH - (y - dialog.y) - footerH)};
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
                font, line.c_str(), {list.x + 10.0f, rowY + 8.0f}, kFontTiny, 1.0f, kTextMuted);
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
            row.label + "  [" + row.binding + "]  ->  " + row.target
            + (row.repeat ? "" : "  (one-shot)");
        const std::string clipped = clipUiLine(font, line, kFontTiny, rowRect.width - 16.0f);
        DrawTextEx(
            font,
            clipped.c_str(),
            {rowRect.x + 8.0f, rowRect.y + 6.0f},
            kFontTiny,
            1.0f,
            selected ? kTextPrimary : kTextMuted);
        if (canClick && CheckCollisionPointRec(mouse, rowRect))
        {
            selectedBinding = row.binding;
            loadDetailsDraftFromSelection();
            detailsFocused = false;
        }
        rowY += rowH;
    }
    EndScissorMode();

    float footY = list.y + list.height + 8.0f;
    for (const std::string& line : helpLines)
    {
        DrawTextEx(font, line.c_str(), {dialog.x + pad, footY}, kFontTiny, 1.0f, kTextMuted);
        footY += 15.0f;
    }
    footY += footerGap;

    DrawTextEx(
        font,
        "Use description (shown when the player clicks Use):",
        {dialog.x + pad, footY},
        kFontTiny,
        1.0f,
        kTextPrimary);
    footY += detailsLabelH;

    const Rectangle detailsBox = {
        dialog.x + pad, footY, textMaxW, detailsBoxH};
    DrawRectangleRec(detailsBox, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(
        detailsBox, 1.0f, detailsFocused ? kPanelBorder : kPanelInnerEdge);

    BeginScissorMode(
        static_cast<int>(detailsBox.x),
        static_cast<int>(detailsBox.y),
        static_cast<int>(detailsBox.width),
        static_cast<int>(detailsBox.height));
    const float detailsFont = kFontTiny;
    const float lineH = detailsFont + 3.0f;
    const auto detailLines = layoutWrappedTextLines(
        font, detailsDraft, detailsBox.width - 12.0f, detailsFont);
    float dy = detailsBox.y + 6.0f;
    if (detailLines.empty())
    {
        DrawTextEx(
            font,
            detailsFocused ? "" : "(click to edit)",
            {detailsBox.x + 6.0f, dy},
            detailsFont,
            1.0f,
            kTextMuted);
    }
    else
    {
        for (const auto& visual : detailLines)
        {
            DrawTextEx(
                font,
                visual.text.c_str(),
                {detailsBox.x + 6.0f, dy},
                detailsFont,
                1.0f,
                kTextPrimary);
            dy += lineH;
        }
    }
    if (detailsFocused && caretBlinkVisible(2.0f))
    {
        const int lineIndex = visualLineIndexForCursor(
            detailLines, detailsCursor, static_cast<int>(detailsDraft.size()));
        const EditorVisualLine& line =
            detailLines.empty() ? EditorVisualLine{} : detailLines[static_cast<size_t>(lineIndex)];
        const float cx = detailsBox.x + 6.0f
            + caretXOnVisualLine(font, line, detailsCursor, detailsFont);
        const float cy = detailsBox.y + 6.0f + static_cast<float>(lineIndex) * lineH;
        DrawRectangleRec({cx, cy, 2.0f, detailsFont}, kPanelBorder);
    }
    EndScissorMode();

    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, detailsBox))
            detailsFocused = true;
        else if (!CheckCollisionPointRec(mouse, detailsBox))
            detailsFocused = false;
    }

    footY += detailsBoxH + footerGap;
    if (!status.empty() || !error.empty())
    {
        const std::string& msg = !error.empty() ? error : status;
        const Color col = !error.empty()
            ? Color{220, 100, 90, 255}
            : Color{120, 180, 120, 255};
        DrawTextEx(
            font,
            clipUiLine(font, msg, kFontTiny, textMaxW).c_str(),
            {dialog.x + pad, footY},
            kFontTiny,
            1.0f,
            col);
    }
    footY += statusH + footerGap;

    const float btnW = 110.0f;
    const float btnY = dialog.y + dialogH - btnH - 14.0f;
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
        {
            detailsFocused = false;
            createNewBinding();
        }
        else if (CheckCollisionPointRec(mouse, clearBtn) && !selectedBinding.empty())
        {
            detailsFocused = false;
            clearSelected();
        }
        else if (CheckCollisionPointRec(mouse, acceptBtn) && !selectedBinding.empty())
        {
            detailsFocused = false;
            if (applySelected())
                closeDialog();
        }
        else if (CheckCollisionPointRec(mouse, cancelBtn))
            closeDialog();
        else if (!CheckCollisionPointRec(mouse, dialog) && !detailsFocused)
            closeDialog();
    }
}

} // namespace timberline_editor
