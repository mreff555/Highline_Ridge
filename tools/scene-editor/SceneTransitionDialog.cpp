/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneTransitionDialog.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorPaths.h"
#include "EditorTheme.h"
#include "EditorTypes.h"
#include "EditorUiDraw.h"
#include "PlatformPath.h"

#include <algorithm>
#include <cctype>

using timberline_engine::listDirectoryFileNames;
using timberline_engine::pathJoin;

namespace timberline_editor
{

namespace
{

void insertUtf8(std::string& buffer, int codepoint)
{
    if (codepoint <= 0)
        return;
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
    buffer.append(bytes, bytes + size);
}

void backspaceUtf8(std::string& buffer)
{
    if (buffer.empty())
        return;
    int i = static_cast<int>(buffer.size()) - 1;
    while (i > 0
           && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        --i;
    buffer.erase(static_cast<size_t>(i));
}

bool endsWithIgnoreCase(const std::string& value, const std::string& suffix)
{
    if (suffix.size() > value.size())
        return false;
    for (size_t i = 0; i < suffix.size(); ++i)
    {
        const unsigned char a = static_cast<unsigned char>(value[value.size() - suffix.size() + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

std::string stripTrailingXz(std::string name)
{
    if (endsWithIgnoreCase(name, ".xz"))
        name.resize(name.size() - 3);
    return name;
}

} // namespace

void SceneTransitionDialog::refreshSfxFileList()
{
    sfxFiles.clear();
    if (!docs)
        return;
    const std::string dir = pathJoin(docs->resourceDir, "audio/sfx");
    std::vector<std::string> names = listDirectoryFileNames(dir);
    for (std::string& name : names)
    {
        name = stripTrailingXz(std::move(name));
        if (!endsWithIgnoreCase(name, ".mp3") && !endsWithIgnoreCase(name, ".wav")
            && !endsWithIgnoreCase(name, ".ogg"))
            continue;
        const std::string rel = "resources/audio/sfx/" + name;
        if (std::find(sfxFiles.begin(), sfxFiles.end(), rel) == sfxFiles.end())
            sfxFiles.push_back(rel);
    }
    std::sort(sfxFiles.begin(), sfxFiles.end());
}

void SceneTransitionDialog::loadPathsFromOwner()
{
    enterPath.clear();
    exitPath.clear();
    if (!graph || ownerId.empty() || neighborId.empty())
        return;
    const SceneGraphModel::TransitionSfxPaths paths =
        graph->readConstrainedTransitionSfx(ownerId, neighborId);
    enterPath = paths.enterPath;
    exitPath = paths.exitPath;
}

void SceneTransitionDialog::openForLink(
    const std::string& fromId,
    const std::string& toId,
    const std::string& preferOwnerId)
{
    open = true;
    ignoreInputFrames = 2;
    waitMouseRelease = true;
    status.clear();
    error.clear();
    focusField = 0;
    sfxListScroll = 0.0f;
    sceneA = fromId;
    sceneB = toId;
    ownerId = graph
        ? graph->preferTransitionSfxOwner(fromId, toId, preferOwnerId)
        : (preferOwnerId.empty() ? toId : preferOwnerId);
    neighborId = (ownerId == sceneA) ? sceneB : sceneA;
    refreshSfxFileList();
    loadPathsFromOwner();
}

void SceneTransitionDialog::closeDialog()
{
    open = false;
    waitMouseRelease = false;
    ignoreInputFrames = 0;
    focusField = 0;
    status.clear();
    error.clear();
}

std::string* SceneTransitionDialog::focusedPath()
{
    return focusField == 1 ? &exitPath : &enterPath;
}

void SceneTransitionDialog::cycleOwner()
{
    if (ownerId == sceneA)
        ownerId = sceneB;
    else
        ownerId = sceneA;
    neighborId = (ownerId == sceneA) ? sceneB : sceneA;
    loadPathsFromOwner();
    status = "Store on: " + ownerId;
    error.clear();
}

void SceneTransitionDialog::typeIntoFocusedField()
{
    std::string* path = focusedPath();
    if (path == nullptr)
        return;

    int codepoint = 0;
    while ((codepoint = GetCharPressed()) > 0)
    {
        if (codepoint == '\n' || codepoint == '\r')
            continue;
        insertUtf8(*path, codepoint);
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        backspaceUtf8(*path);

    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
         || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
        && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
            path->append(clip);
    }
}

bool SceneTransitionDialog::applyChanges()
{
    if (!graph)
    {
        error = "No scene graph.";
        return false;
    }
    auto trim = [](std::string s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            ++i;
        return s.substr(i);
    };
    const std::string enter = trim(enterPath);
    const std::string exit = trim(exitPath);
    if (!graph->upsertConstrainedTransitionSfx(ownerId, neighborId, enter, exit))
    {
        error = "Failed to write transition SFX.";
        return false;
    }
    enterPath = enter;
    exitPath = exit;
    status = "Saved transition sounds on " + ownerId;
    error.clear();
    if (onSaved)
        onSaved();
    return true;
}

void SceneTransitionDialog::handleInput(int screenW, int screenH)
{
    (void)screenW;
    (void)screenH;
    if (!open)
        return;
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }
    if (waitMouseRelease)
    {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            waitMouseRelease = false;
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        closeDialog();
        return;
    }

    typeIntoFocusedField();
    if (IsKeyPressed(KEY_TAB))
        focusField = focusField == 0 ? 1 : 0;
    if (IsKeyPressed(KEY_ENTER)
        && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
            || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)))
    {
        if (applyChanges())
            closeDialog();
    }
}

void SceneTransitionDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;

    // Input is owned by SceneEditorApp (topmost-modal dispatch).

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool canClick =
        !waitMouseRelease && ignoreInputFrames <= 0
        && editorMousePressed(MOUSE_BUTTON_LEFT);

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);

    const float dialogW = std::min(620.0f, static_cast<float>(screenW) - 40.0f);
    const float dialogH = std::min(520.0f, static_cast<float>(screenH) - 40.0f);
    const Rectangle dialog = {
        (static_cast<float>(screenW) - dialogW) * 0.5f,
        (static_cast<float>(screenH) - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    const float pad = 16.0f;
    float y = dialog.y + pad;
    const std::string title = "Transition: " + sceneA + " <-> " + sceneB;
    DrawTextEx(bold, title.c_str(), {dialog.x + pad, y}, kFontTitle, 1.0f, kTextPrimary);
    y += 28.0f;

    DrawTextEx(
        font,
        ("Store SFX on: " + ownerId).c_str(),
        {dialog.x + pad, y},
        kFontSmall,
        1.0f,
        kTextMuted);
    y += 22.0f;

    const Rectangle storeBtn = {dialog.x + pad, y, 140.0f, 28.0f};
    drawEditorButton(font, storeBtn, "Cycle owner", false, true);
    if (canClick && CheckCollisionPointRec(mouse, storeBtn))
        cycleOwner();
    y += 36.0f;

    auto drawPathRow = [&](const char* label, std::string& path, int fieldIndex) {
        DrawTextEx(font, label, {dialog.x + pad, y}, kFontSmall, 1.0f, kTextMuted);
        y += 18.0f;
        const Rectangle field = {
            dialog.x + pad,
            y,
            dialog.width - pad * 2.0f,
            28.0f};
        const bool focused = focusField == fieldIndex;
        DrawRectangleRec(field, Color{22, 20, 28, 255});
        DrawRectangleLinesEx(
            field,
            focused ? 2.0f : 1.0f,
            focused ? kPanelBorder : kPanelInnerEdge);
        const std::string shown = path.empty() ? "(none - clear to remove)" : path;
        DrawTextEx(
            font,
            shown.c_str(),
            {field.x + 8.0f, field.y + 6.0f},
            kFontSmall,
            1.0f,
            path.empty() ? kTextMuted : kTextPrimary);
        if (canClick && CheckCollisionPointRec(mouse, field))
            focusField = fieldIndex;
        y += 36.0f;
    };

    drawPathRow("Enter SFX (on_enter + from_room)", enterPath, 0);
    drawPathRow("Exit SFX (on_exit + to_room)", exitPath, 1);

    DrawTextEx(
        font,
        "Pick from resources/audio/sfx/ (fills focused field):",
        {dialog.x + pad, y},
        kFontTiny,
        1.0f,
        kTextMuted);
    y += 18.0f;

    const float listBottom = dialog.y + dialogH - 78.0f;
    const Rectangle list = {
        dialog.x + pad,
        y,
        dialog.width - pad * 2.0f,
        std::max(60.0f, listBottom - y)};
    DrawRectangleRec(list, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(list, 1.0f, kPanelInnerEdge);

    const float rowH = 20.0f;
    const float maxScroll = std::max(
        0.0f,
        static_cast<float>(sfxFiles.size()) * rowH - list.height);
    if (CheckCollisionPointRec(mouse, list))
    {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            sfxListScroll = std::clamp(sfxListScroll - wheel * rowH * 3.0f, 0.0f, maxScroll);
    }
    sfxListScroll = std::clamp(sfxListScroll, 0.0f, maxScroll);

    BeginScissorMode(
        static_cast<int>(list.x),
        static_cast<int>(list.y),
        static_cast<int>(list.width),
        static_cast<int>(list.height));
    float rowY = list.y - sfxListScroll;
    for (const std::string& rel : sfxFiles)
    {
        const Rectangle row = {list.x + 2.0f, rowY, list.width - 4.0f, rowH - 1.0f};
        if (row.y + row.height >= list.y && row.y <= list.y + list.height)
        {
            if (CheckCollisionPointRec(mouse, row))
            {
                DrawRectangleRec(row, Color{60, 54, 72, 220});
                if (canClick)
                {
                    if (std::string* path = focusedPath())
                        *path = rel;
                }
            }
            DrawTextEx(
                font,
                rel.c_str(),
                {row.x + 6.0f, row.y + 2.0f},
                kFontTiny,
                1.0f,
                kTextPrimary);
        }
        rowY += rowH;
    }
    EndScissorMode();

    const float btnY = dialog.y + dialogH - 48.0f;
    const Rectangle applyBtn = {dialog.x + pad, btnY, 110.0f, 32.0f};
    const Rectangle cancelBtn = {applyBtn.x + applyBtn.width + 10.0f, btnY, 110.0f, 32.0f};
    drawEditorButton(font, applyBtn, "Apply", true, true);
    drawEditorButton(font, cancelBtn, "Cancel", false, true);

    if (!status.empty())
        DrawTextEx(
            font,
            status.c_str(),
            {cancelBtn.x + cancelBtn.width + 12.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            Color{120, 180, 120, 255});
    if (!error.empty())
        DrawTextEx(
            font,
            error.c_str(),
            {cancelBtn.x + cancelBtn.width + 12.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            Color{220, 90, 80, 255});

    if (canClick && CheckCollisionPointRec(mouse, applyBtn))
    {
        if (applyChanges())
            closeDialog();
    }
    if (canClick && CheckCollisionPointRec(mouse, cancelBtn))
        closeDialog();

    if (canClick && !CheckCollisionPointRec(mouse, dialog))
        closeDialog();
}

} // namespace timberline_editor
