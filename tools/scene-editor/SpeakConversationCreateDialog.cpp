/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SpeakConversationCreateDialog.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cstring>

namespace timberline_editor
{
namespace
{

void appendUtf8Codepoint(std::string& buffer, int codepoint)
{
    if (codepoint <= 0)
        return;
    char utf8[5] = {};
    int nbytes = 0;
    if (codepoint < 0x80)
    {
        utf8[0] = static_cast<char>(codepoint);
        nbytes = 1;
    }
    else if (codepoint <= 0x7FF)
    {
        utf8[0] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
        utf8[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        nbytes = 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        utf8[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
        utf8[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        nbytes = 3;
    }
    else
    {
        utf8[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        utf8[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        nbytes = 4;
    }
    buffer.append(utf8, static_cast<size_t>(nbytes));
}

void drawField(
    Font font,
    const char* label,
    const Rectangle& rect,
    const std::string& value,
    bool focused)
{
    DrawTextEx(
        font,
        label,
        {rect.x, rect.y - 18.0f},
        kFontTiny,
        1.0f,
        kTextMuted);
    DrawRectangleRec(rect, focused ? Color{48, 44, 58, 255} : Color{36, 32, 44, 255});
    DrawRectangleLinesEx(rect, focused ? 2.0f : 1.0f, focused ? kPanelAccent : kPanelBorder);
    const std::string shown =
        value.empty() ? std::string("(empty)") : value;
    DrawTextEx(
        font,
        shown.c_str(),
        {rect.x + 8.0f, rect.y + 8.0f},
        kFontBody,
        1.0f,
        value.empty() ? kTextMuted : kTextPrimary);
}

} // namespace

void SpeakConversationCreateDialog::openDialog(const std::string& sceneId)
{
    payload = SpeakConversationCreatePayload{};
    fillSpeakConversationCreateDefaults(payload, sceneId);
    openActorDialogAfter = true;
    open = true;
    ignoreInputFrames = 1;
    waitMouseRelease = true;
    focusField = kFocusActorId;
    error.clear();
    status = "Confirm creates speakPhases in memory — Save (Cmd/Ctrl+S) to write disk.";
}

void SpeakConversationCreateDialog::closeDialog(bool apply)
{
    if (apply)
    {
        if (!tryConfirm())
            return;
    }
    open = false;
    waitMouseRelease = true;
    focusField = kFocusActorId;
}

std::string* SpeakConversationCreateDialog::focusedString()
{
    switch (focusField)
    {
    case kFocusActorId:
        return &payload.actorId;
    case kFocusActorName:
        return &payload.actorName;
    case kFocusPhaseId:
        return &payload.phaseId;
    case kFocusIntro:
        return &payload.intro;
    case kFocusChoiceLabel:
        return &payload.starterChoiceLabel;
    default:
        return nullptr;
    }
}

bool SpeakConversationCreateDialog::tryConfirm()
{
    error.clear();
    if (docs == nullptr)
    {
        error = "No document workspace.";
        return false;
    }
    // K15: load conversations from disk before first mutate.
    if (!docs->conversationsLoaded)
    {
        if (!docs->loadConversationsDocument())
        {
            error = docs->loadError.empty()
                ? "Failed to load conversations.json."
                : docs->loadError;
            return false;
        }
    }

    payload.openActorDialogAfter = openActorDialogAfter;
    if (payload.actorId != sanitizeSpeakId(payload.actorId))
        payload.actorId = sanitizeSpeakId(payload.actorId);
    if (payload.phaseId != sanitizeSpeakId(payload.phaseId))
        payload.phaseId = sanitizeSpeakId(payload.phaseId);
    // Keep actor name in sync when user only edited id and left default name.
    if (payload.actorName.empty()
        || payload.actorName == formatSpeakActorDisplayName("npc"))
    {
        // If name still looks like a default for previous id, refresh from actor id.
    }
    if (payload.actorName.empty())
        payload.actorName = formatSpeakActorDisplayName(payload.actorId);

    std::string err;
    const SpeakConversationCreateStatus st =
        createSpeakConversationScaffold(*docs, payload, err);
    if (st != SpeakConversationCreateStatus::Ok)
    {
        error = err.empty() ? speakConversationCreateStatusMessage(st) : err;
        return false;
    }

    status = "Created Speak conversation for " + payload.sceneId + " (unsaved)";
    if (onCreated)
        onCreated(payload);
    return true;
}

void SpeakConversationCreateDialog::layout(int screenW, int screenH)
{
    const float dialogW = 520.0f;
    const float dialogH = 460.0f;
    dialogRect = {
        (static_cast<float>(screenW) - dialogW) * 0.5f,
        (static_cast<float>(screenH) - dialogH) * 0.5f,
        dialogW,
        dialogH};

    const float pad = 20.0f;
    const float fieldH = 32.0f;
    const float fieldW = dialogW - pad * 2.0f;
    float y = dialogRect.y + 88.0f;

    actorIdRect = {dialogRect.x + pad, y, fieldW * 0.48f, fieldH};
    actorNameRect = {
        dialogRect.x + pad + fieldW * 0.52f, y, fieldW * 0.48f, fieldH};
    y += 56.0f;
    phaseIdRect = {dialogRect.x + pad, y, fieldW, fieldH};
    y += 56.0f;
    introRect = {dialogRect.x + pad, y, fieldW, 56.0f};
    y += 80.0f;
    choiceLabelRect = {dialogRect.x + pad, y, fieldW, fieldH};
    y += 48.0f;
    openActorCheckRect = {dialogRect.x + pad, y, 22.0f, 22.0f};

    const float btnW = 120.0f;
    const float btnH = 34.0f;
    const float btnY = dialogRect.y + dialogH - btnH - 16.0f;
    confirmBtnRect = {
        dialogRect.x + dialogW - btnW * 2.0f - 28.0f, btnY, btnW, btnH};
    cancelBtnRect = {dialogRect.x + dialogW - btnW - 16.0f, btnY, btnW, btnH};
}

void SpeakConversationCreateDialog::typeIntoFocused()
{
    std::string* target = focusedString();
    if (target == nullptr)
        return;

    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
            *target = clip;
        return;
    }

    if (IsKeyPressed(KEY_BACKSPACE) && !target->empty())
    {
        // Erase one UTF-8 codepoint roughly (byte backspace is fine for ASCII ids).
        target->pop_back();
        return;
    }

    int codepoint = GetCharPressed();
    while (codepoint > 0)
    {
        if (codepoint >= 32 && codepoint != 127)
            appendUtf8Codepoint(*target, codepoint);
        codepoint = GetCharPressed();
    }
}

void SpeakConversationCreateDialog::handleInput(int screenW, int screenH)
{
    if (!open)
        return;

    layout(screenW, screenH);

    if (waitMouseRelease)
    {
        if (!editorMouseDown(MOUSE_BUTTON_LEFT))
            waitMouseRelease = false;
        return;
    }
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        while (GetCharPressed() > 0)
        {
        }
        while (GetKeyPressed() != 0)
        {
        }
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        closeDialog(false);
        return;
    }
    if (IsKeyPressed(KEY_TAB))
    {
        focusField = (focusField + 1) % kFocusCount;
        return;
    }
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
         || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
        && IsKeyPressed(KEY_ENTER))
    {
        closeDialog(true);
        return;
    }

    const Vector2 mouse = GetMousePosition();
    if (editorMousePressed(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(mouse, confirmBtnRect))
        {
            closeDialog(true);
            return;
        }
        if (CheckCollisionPointRec(mouse, cancelBtnRect))
        {
            closeDialog(false);
            return;
        }
        if (CheckCollisionPointRec(mouse, openActorCheckRect))
        {
            openActorDialogAfter = !openActorDialogAfter;
            return;
        }
        if (CheckCollisionPointRec(mouse, actorIdRect))
        {
            focusField = kFocusActorId;
            return;
        }
        if (CheckCollisionPointRec(mouse, actorNameRect))
        {
            focusField = kFocusActorName;
            return;
        }
        if (CheckCollisionPointRec(mouse, phaseIdRect))
        {
            focusField = kFocusPhaseId;
            return;
        }
        if (CheckCollisionPointRec(mouse, introRect))
        {
            focusField = kFocusIntro;
            return;
        }
        if (CheckCollisionPointRec(mouse, choiceLabelRect))
        {
            focusField = kFocusChoiceLabel;
            return;
        }
        if (!CheckCollisionPointRec(mouse, dialogRect))
        {
            closeDialog(false);
            return;
        }
    }

    typeIntoFocused();
}

void SpeakConversationCreateDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;

    layout(screenW, screenH);

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);
    DrawRectangleRounded(dialogRect, 0.03f, 8, kModalFill);
    DrawRectangleLinesEx(dialogRect, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        "Create Speak conversation",
        {dialogRect.x + 20.0f, dialogRect.y + 16.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);

    const std::string sceneLine = "Scene: " + payload.sceneId;
    DrawTextEx(
        font,
        sceneLine.c_str(),
        {dialogRect.x + 20.0f, dialogRect.y + 46.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    drawField(font, "Actor id", actorIdRect, payload.actorId, focusField == kFocusActorId);
    drawField(
        font, "Actor name", actorNameRect, payload.actorName, focusField == kFocusActorName);
    drawField(font, "Phase id", phaseIdRect, payload.phaseId, focusField == kFocusPhaseId);
    drawField(font, "Intro", introRect, payload.intro, focusField == kFocusIntro);
    drawField(
        font,
        "Starter choice label",
        choiceLabelRect,
        payload.starterChoiceLabel,
        focusField == kFocusChoiceLabel);

    DrawRectangleRec(
        openActorCheckRect,
        openActorDialogAfter ? kPanelAccent : Color{36, 32, 44, 255});
    DrawRectangleLinesEx(openActorCheckRect, 1.0f, kPanelBorder);
    if (openActorDialogAfter)
    {
        DrawTextEx(
            bold,
            "X",
            {openActorCheckRect.x + 5.0f, openActorCheckRect.y + 2.0f},
            kFontBody,
            1.0f,
            kTextPrimary);
    }
    DrawTextEx(
        font,
        "Open Edit Actor Dialog after create",
        {openActorCheckRect.x + 30.0f, openActorCheckRect.y + 2.0f},
        kFontBody,
        1.0f,
        kTextPrimary);

    DrawRectangleRec(confirmBtnRect, kPanelAccent);
    DrawRectangleLinesEx(confirmBtnRect, 1.0f, kPanelBorder);
    DrawTextEx(
        bold,
        "Create",
        {confirmBtnRect.x + 30.0f, confirmBtnRect.y + 8.0f},
        kFontBody,
        1.0f,
        kTextPrimary);

    DrawRectangleRec(cancelBtnRect, Color{36, 32, 44, 255});
    DrawRectangleLinesEx(cancelBtnRect, 1.0f, kPanelBorder);
    DrawTextEx(
        font,
        "Cancel",
        {cancelBtnRect.x + 32.0f, cancelBtnRect.y + 8.0f},
        kFontBody,
        1.0f,
        kTextPrimary);

    const std::string footer = error.empty() ? status : error;
    if (!footer.empty())
    {
        DrawTextEx(
            font,
            footer.c_str(),
            {dialogRect.x + 20.0f, confirmBtnRect.y - 28.0f},
            kFontTiny,
            1.0f,
            error.empty() ? kTextMuted : Color{220, 90, 90, 255});
    }
}

} // namespace timberline_editor
