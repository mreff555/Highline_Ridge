/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneAuthoringDialog.h"
#include "EditorButton.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

void backspace(std::string& buffer)
{
    if (buffer.empty())
        return;
    int i = static_cast<int>(buffer.size()) - 1;
    while (i > 0
           && (static_cast<unsigned char>(buffer[static_cast<size_t>(i)]) & 0xC0) == 0x80)
        --i;
    buffer.erase(static_cast<size_t>(i));
}

void drawFieldBox(Rectangle r, bool focused)
{
    DrawRectangleRec(r, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(r, 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
}

void drawLabel(Font font, const char* text, float x, float y)
{
    DrawTextEx(font, text, {x, y}, kFontTiny, 1.0f, kTextMuted);
}

} // namespace

void SceneAuthoringDialog::openDialog()
{
    open = true;
    ignoreInputFrames = 1;
    waitMouseRelease = true;
    scrollY = 0.0f;
    focusField = 0;
    error.clear();
    status.clear();
    payload = SceneAuthoringPayload{};
    sessionApiKey.clear();
}

void SceneAuthoringDialog::closeDialog()
{
    if (generateThread.joinable())
        generateThread.join();
    generateBusy = false;
    open = false;
    error.clear();
}

void SceneAuthoringDialog::pollGenerateResult()
{
    if (!generateResultPending)
        return;
    std::lock_guard<std::mutex> lock(generateMutex);
    if (!generateResultPending)
        return;
    status = generateResultStatus;
    generateResultPending = false;
    generateBusy = false;
    generateTarget = 0;
    if (docs != nullptr && !payload.id.empty())
        applySceneAiOutputsToPayload(payload, *docs, payload.id);
    if (onCreated && !payload.id.empty() && docs != nullptr
        && docs->scenes.hasScene(payload.id))
        onCreated(payload.id);
}

void SceneAuthoringDialog::typeIntoFocusedField()
{
    std::string* target = nullptr;
    switch (focusField)
    {
    case 0:
        target = &payload.id;
        break;
    case 1:
        target = &payload.description;
        break;
    case 2:
        target = &payload.examineDetails;
        break;
    case 3:
        target = &sessionApiKey;
        break;
    case 4:
        target = &payload.imagePath;
        break;
    case 5:
        target = &payload.ambientPath;
        break;
    case 6:
        target = &payload.musicPath;
        break;
    default:
        break;
    }
    if (target == nullptr)
        return;

    // Cmd/Ctrl+V paste (API key replaces; other fields append).
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
            if (start > 0)
                pasted = pasted.substr(start);
            if (focusField == 3)
            {
                pasted.erase(
                    std::remove_if(
                        pasted.begin(),
                        pasted.end(),
                        [](unsigned char ch) { return ch == '\n' || ch == '\r'; }),
                    pasted.end());
                *target = pasted;
            }
            else
                *target += pasted;
        }
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (focusField == 0)
        {
            // Scene id: restrict while typing.
            if (std::isalnum(static_cast<unsigned char>(cp)) || cp == '_' || cp == '-'
                || cp == ' ')
                insertUtf8(*target, cp);
        }
        else if (cp == '\n')
        {
            // only multiline fields accept enter
            if (focusField == 1 || focusField == 2)
                insertUtf8(*target, '\n');
        }
        else
            insertUtf8(*target, cp);
        cp = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        backspace(*target);
    if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        && (focusField == 1 || focusField == 2))
        insertUtf8(*target, '\n');
}

void SceneAuthoringDialog::commitCreate(bool runAi, int aiTarget)
{
    if (docs == nullptr)
        return;
    error.clear();
    payload.id = sanitizeSceneId(payload.id);
    SceneUpsertResult result = upsertScene(*docs, payload, aiTarget, runAi);
    if (!result.ok)
    {
        error = result.message;
        return;
    }
    status = result.message;
    if (runAi && !result.jobsFilePath.empty())
    {
        startGenerate(aiTarget);
        return;
    }
    if (onCreated)
        onCreated(payload.id);
    closeDialog();
}

void SceneAuthoringDialog::startGenerate(int aiTarget)
{
    if (docs == nullptr || generateBusy.load())
        return;

    payload.id = sanitizeSceneId(payload.id);
    if (!isValidSceneId(payload.id))
    {
        error = "Enter a valid scene id before generating.";
        return;
    }
    if (payload.description.empty())
    {
        error = "Description is required for AI context.";
        return;
    }

    // Ensure scene exists first (so the map can show it while AI runs).
    if (!docs->scenes.hasScene(payload.id))
    {
        SceneUpsertResult result = upsertScene(*docs, payload, aiTarget, true);
        if (!result.ok)
        {
            error = result.message;
            return;
        }
        status = result.message;
        if (onCreated)
            onCreated(payload.id);
    }
    else
    {
        // Refresh default paths + rewrite jobs for this target.
        if (payload.imagePath.empty())
            payload.imagePath = "resources/images/" + payload.id + ".png";
        if (payload.ambientPath.empty())
            payload.ambientPath = "resources/audio/ambient/" + payload.id + ".mp3";
        if (payload.musicPath.empty())
            payload.musicPath = "resources/audio/music/" + payload.id + "_theme.mp3";
        const std::string jobsPath = writeSceneAiJobsFile(
            docs->assetRoot, docs->resourceDir, payload, aiTarget);
        if (jobsPath.empty())
        {
            error = "Failed to write AI jobs file.";
            return;
        }
        status = "Jobs ready: " + jobsPath;
    }

    generateBusy = true;
    generateTarget = aiTarget;
    generateResultPending = false;
    const std::string keySnap = sessionApiKey;
    const std::string idSnap = payload.id;
    const std::string assetRoot = docs->assetRoot;
    const std::string resourceDir = docs->resourceDir;
    if (generateThread.joinable())
        generateThread.join();
    generateThread = std::thread([this, keySnap, idSnap, assetRoot, resourceDir]() {
        const std::string msg =
            runSceneAuthoringAiJobs(assetRoot, resourceDir, idSnap, keySnap);
        std::lock_guard<std::mutex> lock(generateMutex);
        generateResultStatus = msg;
        generateResultPending = true;
    });
}

void SceneAuthoringDialog::handleInput(int screenW, int screenH)
{
    if (!open)
        return;
    pollGenerateResult();

    if (waitMouseRelease)
    {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            waitMouseRelease = false;
        return;
    }
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }

    typeIntoFocusedField();

    if (IsKeyPressed(KEY_ESCAPE) && !generateBusy.load())
        closeDialog();
}

void SceneAuthoringDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;
    pollGenerateResult();

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool canClick =
        !waitMouseRelease && ignoreInputFrames <= 0 && !generateBusy.load()
        && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const bool busy = generateBusy.load();

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);

    const float dialogW = std::min(920.0f, screenW - 40.0f);
    const float dialogH = std::min(720.0f, screenH - 40.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        "New Scene",
        {dialog.x + 20.0f, dialog.y + 16.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    DrawTextEx(
        font,
        "Create a playable scene entry. Description drives AI context.",
        {dialog.x + 20.0f, dialog.y + 46.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float contentTop = dialog.y + 72.0f;
    const float footerH = 58.0f;
    const Rectangle content = {
        dialog.x + 16.0f,
        contentTop,
        dialog.width - 32.0f,
        dialogH - (contentTop - dialog.y) - footerH};
    DrawRectangleRec(content, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(content, 1.0f, kPanelInnerEdge);

    // Simple scrollable form (no scissor for hit simplicity on first version).
    float y = content.y + 12.0f - scrollY;
    const float labelX = content.x + 12.0f;
    const float fieldX = content.x + 12.0f;
    const float fieldW = content.width - 24.0f;

    auto hitField = [&](Rectangle r, int index) {
        if (canClick && CheckCollisionPointRec(mouse, r))
            focusField = index;
    };

    // ID
    drawLabel(font, "Scene id *", labelX, y);
    y += 16.0f;
    Rectangle idField = {fieldX, y, fieldW * 0.55f, 32.0f};
    drawFieldBox(idField, focusField == 0);
    DrawTextEx(
        font,
        payload.id.empty() ? "e.g. abandoned_mine_shaft" : payload.id.c_str(),
        {idField.x + 8.0f, idField.y + 8.0f},
        kFontSmall,
        1.0f,
        payload.id.empty() ? kTextMuted : kTextPrimary);
    hitField(idField, 0);
    y += 44.0f;

    // Description
    drawLabel(font, "Description * (AI context)", labelX, y);
    y += 16.0f;
    Rectangle descField = {fieldX, y, fieldW, 88.0f};
    drawFieldBox(descField, focusField == 1);
    drawWrappedText(
        font,
        payload.description.empty()
            ? "What the player sees when entering..."
            : payload.description,
        {descField.x + 8.0f, descField.y + 8.0f},
        descField.width - 16.0f,
        kFontSmall,
        3.0f,
        payload.description.empty() ? kTextMuted : kTextPrimary);
    hitField(descField, 1);
    y += 100.0f;

    // Examine
    drawLabel(font, "Examine details", labelX, y);
    y += 16.0f;
    Rectangle examField = {fieldX, y, fieldW, 64.0f};
    drawFieldBox(examField, focusField == 2);
    drawWrappedText(
        font,
        payload.examineDetails.empty() ? "(optional)" : payload.examineDetails,
        {examField.x + 8.0f, examField.y + 8.0f},
        examField.width - 16.0f,
        kFontSmall,
        3.0f,
        payload.examineDetails.empty() ? kTextMuted : kTextPrimary);
    hitField(examField, 2);
    y += 76.0f;

    // API key
    drawLabel(font, "Session xAI API key (images; not saved to disk)", labelX, y);
    y += 16.0f;
    Rectangle keyField = {fieldX, y, fieldW, 32.0f};
    drawFieldBox(keyField, focusField == 3);
    const std::string keyShow = sessionApiKey.empty()
        ? "(optional if XAI_API_KEY is set)"
        : std::string(std::min<size_t>(sessionApiKey.size(), 28), '*');
    DrawTextEx(
        font,
        keyShow.c_str(),
        {keyField.x + 8.0f, keyField.y + 8.0f},
        kFontSmall,
        1.0f,
        sessionApiKey.empty() ? kTextMuted : kTextPrimary);
    hitField(keyField, 3);
    y += 44.0f;

    // Paths + generate buttons
    struct PathRow
    {
        const char* label;
        std::string* value;
        int focus;
        int genTarget;
        const char* genLabel;
    };
    PathRow rows[] = {
        {"Image path", &payload.imagePath, 4, 1, "Generate image"},
        {"Ambient path", &payload.ambientPath, 5, 2, "Generate ambient"},
        {"Music path", &payload.musicPath, 6, 3, "Generate music"},
    };

    for (const PathRow& row : rows)
    {
        drawLabel(font, row.label, labelX, y);
        y += 16.0f;
        const float genW = 150.0f;
        Rectangle pathField = {fieldX, y, fieldW - genW - 10.0f, 32.0f};
        Rectangle genBtn = {fieldX + pathField.width + 10.0f, y, genW, 32.0f};
        drawFieldBox(pathField, focusField == row.focus);
        DrawTextEx(
            font,
            row.value->empty() ? "(auto from id)" : row.value->c_str(),
            {pathField.x + 8.0f, pathField.y + 8.0f},
            kFontTiny,
            1.0f,
            row.value->empty() ? kTextMuted : kTextPrimary);
        hitField(pathField, row.focus);
        drawEditorButton(font, genBtn, row.genLabel, true, !busy);
        if (canClick && CheckCollisionPointRec(mouse, genBtn))
            startGenerate(row.genTarget);
        if (busy && generateTarget.load() == row.genTarget)
        {
            DrawTextEx(
                font,
                "Working",
                {genBtn.x + genBtn.width + 8.0f, genBtn.y + 8.0f},
                kFontTiny,
                1.0f,
                Color{220, 80, 70, 255});
        }
        y += 44.0f;
    }

    // Speak toggle
    Rectangle speakBtn = {fieldX, y, 160.0f, 28.0f};
    drawEditorButton(
        font,
        speakBtn,
        payload.speakEnabled ? "Speak: ON" : "Speak: off",
        payload.speakEnabled,
        !busy);
    if (canClick && CheckCollisionPointRec(mouse, speakBtn))
        payload.speakEnabled = !payload.speakEnabled;
    y += 40.0f;

    // Footer buttons
    const float btnW = 130.0f;
    const float btnH = 36.0f;
    const float btnY = dialog.y + dialogH - btnH - 14.0f;
    Rectangle createBtn = {
        dialog.x + dialogW - btnW * 2.0f - 36.0f, btnY, btnW, btnH};
    Rectangle cancelBtn = {dialog.x + dialogW - btnW - 18.0f, btnY, btnW, btnH};
    Rectangle genAllBtn = {dialog.x + 20.0f, btnY, 180.0f, btnH};

    drawEditorButton(font, genAllBtn, "Generate all assets", true, !busy);
    drawEditorButton(font, createBtn, "Create", true, !busy);
    drawEditorButton(font, cancelBtn, "Cancel", false, !busy);

    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, cancelBtn))
            closeDialog();
        else if (CheckCollisionPointRec(mouse, createBtn))
            commitCreate(false, 0);
        else if (CheckCollisionPointRec(mouse, genAllBtn))
            startGenerate(0);
        else if (!CheckCollisionPointRec(mouse, dialog) && !busy)
            closeDialog();
    }

    if (!status.empty())
    {
        DrawTextEx(
            font,
            status.c_str(),
            {dialog.x + 20.0f, dialog.y + dialogH - footerH + 4.0f},
            kFontTiny,
            1.0f,
            Color{120, 180, 120, 255});
    }
    if (!error.empty())
    {
        DrawTextEx(
            font,
            error.c_str(),
            {dialog.x + 20.0f, dialog.y + dialogH - footerH + 4.0f},
            kFontTiny,
            1.0f,
            Color{220, 100, 90, 255});
    }
    if (busy)
    {
        const float pulse =
            0.45f + 0.55f * (0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 5.0f));
        DrawTextEx(
            font,
            "Working…",
            {dialog.x + dialogW * 0.5f - 40.0f, dialog.y + 16.0f},
            kFontSmall,
            1.0f,
            Color{
                220,
                static_cast<unsigned char>(40 + 40 * (1.0f - pulse)),
                static_cast<unsigned char>(40 + 40 * (1.0f - pulse)),
                255});
    }
}

} // namespace timberline_editor
