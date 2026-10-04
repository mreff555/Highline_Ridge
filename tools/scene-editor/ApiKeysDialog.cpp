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

std::string maskKeyDisplay(const std::string& key)
{
    if (key.empty())
        return {};
    if (key.size() <= 4)
        return std::string(key.size(), '*');
    return std::string(std::min<size_t>(key.size() - 4, 36), '*') + key.substr(key.size() - 4);
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
    status = "Confirm writes ~/.config/highline-ridge/{xai,elevenlabs}_api_key (overwrite).";
    draftXai = keys != nullptr ? keys->xaiKey : "";
    draftElevenLabs = keys != nullptr ? keys->elevenLabsKey : "";
    // Prefer focusing an empty slot so a bootstrapped filled key isn't a trap.
    if (draftXai.empty() && !draftElevenLabs.empty())
        focusField = 0;
    else if (!draftXai.empty() && draftElevenLabs.empty())
        focusField = 1;
    else
        focusField = 0; // both empty or both filled — start on xAI; click/Tab to switch
    replaceOnNextEdit = focusField == 0 ? !draftXai.empty() : !draftElevenLabs.empty();
}

void ApiKeysDialog::closeDialog(bool apply)
{
    if (apply && keys != nullptr)
    {
        keys->applySessionKeys(draftXai, draftElevenLabs);
        status = "API keys saved to ~/.config/highline-ridge/.";
    }
    open = false;
    waitMouseRelease = true;
    focusField = 0;
    replaceOnNextEdit = false;
}

void ApiKeysDialog::typeIntoFocused()
{
    std::string* target = focusField == 1 ? &draftElevenLabs : &draftXai;

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
            replaceOnNextEdit = false;
            error.clear();
            status = focusField == 1
                ? "ElevenLabs draft updated — Confirm to apply."
                : "xAI draft updated — Confirm to apply.";
        }
        return;
    }
    if (mod && IsKeyPressed(KEY_A))
    {
        // Select-all → clear so the next paste/type replaces cleanly.
        target->clear();
        replaceOnNextEdit = false;
        status = "Field cleared — paste a new key.";
        return;
    }

    int codepoint = GetCharPressed();
    bool typed = false;
    while (codepoint > 0)
    {
        if (codepoint >= 32 && codepoint != 127)
        {
            if (replaceOnNextEdit)
            {
                target->clear();
                replaceOnNextEdit = false;
            }
            appendUtf8Codepoint(*target, codepoint);
            typed = true;
        }
        codepoint = GetCharPressed();
    }
    if (typed)
    {
        status = focusField == 1
            ? "Editing ElevenLabs draft — Confirm to apply."
            : "Editing xAI draft — Confirm to apply.";
    }
    if (IsKeyPressed(KEY_BACKSPACE))
    {
        if (replaceOnNextEdit)
        {
            target->clear();
            replaceOnNextEdit = false;
            status = "Field cleared — paste a new key.";
        }
        else if (!target->empty())
        {
            target->pop_back();
        }
    }
}

void ApiKeysDialog::layout(int screenW, int screenH)
{
    const float dialogW = std::min(560.0f, screenW - 40.0f);
    const float dialogH = 340.0f;
    dialogRect = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};

    const float fieldX = dialogRect.x + 20.0f;
    const float clearW = 64.0f;
    const float gap = 8.0f;
    const float fieldW = dialogW - 40.0f - clearW - gap;

    float y = dialogRect.y + 78.0f;
    y += 16.0f; // label
    xaiFieldRect = {fieldX, y, fieldW, 32.0f};
    xaiClearRect = {fieldX + fieldW + gap, y, clearW, 32.0f};
    y += 40.0f;
    y += 16.0f;
    elevenFieldRect = {fieldX, y, fieldW, 32.0f};
    elevenClearRect = {fieldX + fieldW + gap, y, clearW, 32.0f};

    const float btnW = 120.0f;
    const float btnH = 34.0f;
    const float btnY = dialogRect.y + dialogH - btnH - 16.0f;
    confirmBtnRect = {
        dialogRect.x + dialogW - btnW * 2.0f - 28.0f, btnY, btnW, btnH};
    cancelBtnRect = {dialogRect.x + dialogW - btnW - 16.0f, btnY, btnW, btnH};
}

void ApiKeysDialog::handleInput(int screenW, int screenH)
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
        focusField = focusField == 0 ? 1 : 0;
        replaceOnNextEdit =
            focusField == 0 ? !draftXai.empty() : !draftElevenLabs.empty();
        status = focusField == 1 ? "Editing ElevenLabs key." : "Editing xAI key.";
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
        if (CheckCollisionPointRec(mouse, xaiClearRect))
        {
            draftXai.clear();
            focusField = 0;
            replaceOnNextEdit = false;
            status = "xAI draft cleared.";
            return;
        }
        if (CheckCollisionPointRec(mouse, elevenClearRect))
        {
            draftElevenLabs.clear();
            focusField = 1;
            replaceOnNextEdit = false;
            status = "ElevenLabs draft cleared.";
            return;
        }
        if (CheckCollisionPointRec(mouse, xaiFieldRect))
        {
            focusField = 0;
            replaceOnNextEdit = !draftXai.empty();
            status = draftXai.empty()
                ? "xAI field focused — Cmd/Ctrl+V pastes."
                : "xAI focused — type/paste replaces; Clear empties.";
            return;
        }
        if (CheckCollisionPointRec(mouse, elevenFieldRect))
        {
            focusField = 1;
            replaceOnNextEdit = !draftElevenLabs.empty();
            status = draftElevenLabs.empty()
                ? "ElevenLabs field focused — Cmd/Ctrl+V pastes."
                : "ElevenLabs focused — type/paste replaces; Clear empties.";
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

void ApiKeysDialog::draw(int screenW, int screenH)
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
        "Configure API keys",
        {dialogRect.x + 20.0f, dialogRect.y + 16.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    DrawTextEx(
        font,
        "Saved under ~/.config/highline-ridge/. Tab switches fields.",
        {dialogRect.x + 20.0f, dialogRect.y + 44.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    auto drawKeyRow = [&](const char* label,
                          const std::string& draft,
                          int fieldId,
                          Rectangle field,
                          Rectangle clearBtn,
                          ApiKeyValidity liveValidity)
    {
        DrawTextEx(
            font,
            label,
            {field.x, field.y - 16.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
        const bool focused = focusField == fieldId;
        DrawRectangleRec(field, Color{18, 16, 24, 255});
        DrawRectangleLinesEx(
            field, focused ? 2.0f : 1.0f, focused ? kPanelBorder : kPanelInnerEdge);

        const std::string show = maskKeyDisplay(draft);
        const char* hint = fieldId == 0
            ? "Click, then Cmd/Ctrl+V — console.x.ai"
            : "Click, then Cmd/Ctrl+V — elevenlabs.io/app/settings/api-keys";
        DrawTextEx(
            font,
            show.empty() ? hint : show.c_str(),
            {field.x + 10.0f, field.y + 8.0f},
            kFontSmall,
            1.0f,
            show.empty() ? kTextMuted : kTextPrimary);

        Rectangle icon = {
            field.x + field.width - 28.0f, field.y + 5.0f, 22.0f, 22.0f};
        ApiKeyValidity iconV = ApiKeyValidity::Missing;
        if (!draft.empty())
        {
            if (keys != nullptr
                && draft
                    == keys->key(
                        fieldId == 0 ? ApiKeyProvider::Xai
                                     : ApiKeyProvider::ElevenLabs))
                iconV = liveValidity;
            else
                iconV = ApiKeyValidity::Unknown;
        }
        drawApiKeyStatusIcon(bold, icon, iconV);

        drawEditorButton(font, clearBtn, "Clear", false, true);
    };

    drawKeyRow(
        "xAI (images, ambient, TTS)",
        draftXai,
        0,
        xaiFieldRect,
        xaiClearRect,
        keys != nullptr ? keys->xaiValidity : ApiKeyValidity::Missing);
    drawKeyRow(
        "ElevenLabs (period music)",
        draftElevenLabs,
        1,
        elevenFieldRect,
        elevenClearRect,
        keys != nullptr ? keys->elevenLabsValidity : ApiKeyValidity::Missing);

    const float msgY = elevenFieldRect.y + 40.0f;
    if (!error.empty())
    {
        DrawTextEx(
            font,
            error.c_str(),
            {dialogRect.x + 20.0f, msgY},
            kFontTiny,
            1.0f,
            Color{220, 120, 100, 255});
    }
    else if (!status.empty())
    {
        DrawTextEx(
            font,
            status.c_str(),
            {dialogRect.x + 20.0f, msgY},
            kFontTiny,
            1.0f,
            kTextMuted);
    }

    drawEditorButton(font, confirmBtnRect, "Confirm", true, true);
    drawEditorButton(font, cancelBtnRect, "Cancel", false, true);
}

} // namespace timberline_editor
