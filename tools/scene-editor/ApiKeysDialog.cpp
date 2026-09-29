/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Options → Configure API keys dialog (#56).
 ******************************************************************************/

#include "ApiKeysDialog.h"
#include "EditorButton.h"
#include "EditorInput.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <string>

namespace timberline_editor
{

namespace
{

std::string maskKey(const std::string& key)
{
    if (key.empty())
        return {};
    return std::string(std::min<size_t>(key.size(), 40), '*');
}

void appendUtf8Codepoint(std::string& buffer, int codepoint)
{
    if (codepoint <= 0)
        return;
    char utf8[8] = {};
    int nbytes = 0;
    if (codepoint <= 0x7F)
    {
        utf8[0] = static_cast<char>(codepoint);
        nbytes = 1;
    }
    else if (codepoint <= 0x7FF)
    {
        utf8[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        utf8[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        nbytes = 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        utf8[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        utf8[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        nbytes = 3;
    }
    else
    {
        utf8[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        utf8[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        nbytes = 4;
    }
    buffer.append(utf8, static_cast<size_t>(nbytes));
}

} // namespace

void ApiKeysDialog::openDialog()
{
    open = true;
    ignoreInputFrames = 1;
    waitMouseRelease = true;
    error.clear();
    status = "Keys stay in memory for this editor session only.";
    draftXai = keys != nullptr ? keys->xaiKey : "";
    draftElevenLabs = keys != nullptr ? keys->elevenLabsKey : "";
    focusField = 0;
}

void ApiKeysDialog::closeDialog(bool apply)
{
    if (apply && keys != nullptr)
    {
        keys->applySessionKeys(draftXai, draftElevenLabs);
        status = "API keys updated (session only).";
    }
    open = false;
    waitMouseRelease = true;
    focusField = 0;
}

void ApiKeysDialog::typeIntoFocused()
{
    std::string* target = focusField == 1 ? &draftElevenLabs : &draftXai;
    if (target == nullptr)
        return;

    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            std::string pasted = clip;
            while (!pasted.empty()
                   && (pasted.back() == '\n' || pasted.back() == '\r'
                       || pasted.back() == ' ' || pasted.back() == '\t'))
                pasted.pop_back();
            size_t start = 0;
            while (start < pasted.size()
                   && (pasted[start] == ' ' || pasted[start] == '\t'
                       || pasted[start] == '\n' || pasted[start] == '\r'))
                ++start;
            *target = pasted.substr(start);
            error.clear();
            status = "Pasted (session draft — Confirm to apply).";
        }
        return;
    }

    int codepoint = GetCharPressed();
    while (codepoint > 0)
    {
        if (codepoint >= 32 && codepoint != 127)
            appendUtf8Codepoint(*target, codepoint);
        codepoint = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !target->empty())
        target->pop_back();
}

void ApiKeysDialog::handleInput(int screenW, int screenH)
{
    if (!open)
        return;
    (void)screenW;
    (void)screenH;

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
        focusField = focusField == 0 ? 1 : 0;

    typeIntoFocused();
}

void ApiKeysDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool canClick =
        !waitMouseRelease && ignoreInputFrames <= 0
        && editorMousePressed(MOUSE_BUTTON_LEFT);

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);

    const float dialogW = std::min(560.0f, screenW - 40.0f);
    const float dialogH = 320.0f;
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRounded(dialog, 0.03f, 8, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        "Configure API keys",
        {dialog.x + 20.0f, dialog.y + 16.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    DrawTextEx(
        font,
        "Session only — not written to disk. Paste with Cmd/Ctrl+V.",
        {dialog.x + 20.0f, dialog.y + 44.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float fieldX = dialog.x + 20.0f;
    const float fieldW = dialogW - 40.0f;
    float y = dialog.y + 78.0f;

    auto drawKeyRow = [&](const char* label,
                          const std::string& draft,
                          int fieldId,
                          ApiKeyValidity liveValidity)
    {
        DrawTextEx(font, label, {fieldX, y}, kFontTiny, 1.0f, kTextMuted);
        y += 16.0f;
        const Rectangle field = {fieldX, y, fieldW, 32.0f};
        const bool focused = focusField == fieldId;
        DrawRectangleRec(field, Color{18, 16, 24, 255});
        DrawRectangleLinesEx(
            field, 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
        const std::string show = draft.empty() ? std::string() : maskKey(draft);
        const char* hint = fieldId == 0
            ? "xAI key — console.x.ai"
            : "ElevenLabs key — elevenlabs.io/app/settings/api-keys";
        DrawTextEx(
            font,
            show.empty() ? hint : show.c_str(),
            {field.x + 10.0f, field.y + 8.0f},
            kFontSmall,
            1.0f,
            show.empty() ? kTextMuted : kTextPrimary);

        Rectangle icon = {
            field.x + field.width - 28.0f, field.y + 5.0f, 22.0f, 22.0f};
        // Live validity for applied keys; drafts show Missing until Confirm.
        ApiKeyValidity iconV = ApiKeyValidity::Missing;
        if (!draft.empty())
        {
            if (keys != nullptr && draft == keys->key(
                    fieldId == 0 ? ApiKeyProvider::Xai
                                 : ApiKeyProvider::ElevenLabs))
                iconV = liveValidity;
            else
                iconV = ApiKeyValidity::Unknown;
        }
        drawApiKeyStatusIcon(bold, icon, iconV);

        if (canClick && CheckCollisionPointRec(mouse, field))
            focusField = fieldId;
        y += 40.0f;
    };

    drawKeyRow(
        "xAI (images, ambient, TTS)",
        draftXai,
        0,
        keys != nullptr ? keys->xaiValidity : ApiKeyValidity::Missing);
    drawKeyRow(
        "ElevenLabs (period music)",
        draftElevenLabs,
        1,
        keys != nullptr ? keys->elevenLabsValidity : ApiKeyValidity::Missing);

    if (!error.empty())
    {
        DrawTextEx(
            font,
            error.c_str(),
            {fieldX, y},
            kFontTiny,
            1.0f,
            Color{220, 120, 100, 255});
    }
    else if (!status.empty())
    {
        DrawTextEx(
            font, status.c_str(), {fieldX, y}, kFontTiny, 1.0f, kTextMuted);
    }

    const float btnW = 120.0f;
    const float btnH = 34.0f;
    const float btnY = dialog.y + dialogH - btnH - 16.0f;
    const Rectangle confirmBtn = {dialog.x + dialogW - btnW * 2.0f - 28.0f, btnY, btnW, btnH};
    const Rectangle cancelBtn = {dialog.x + dialogW - btnW - 16.0f, btnY, btnW, btnH};
    drawEditorButton(font, confirmBtn, "Confirm", true, true);
    drawEditorButton(font, cancelBtn, "Cancel", false, true);

    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, confirmBtn))
            closeDialog(true);
        else if (CheckCollisionPointRec(mouse, cancelBtn))
            closeDialog(false);
        else if (!CheckCollisionPointRec(mouse, dialog))
            closeDialog(false);
    }
}

} // namespace timberline_editor
