/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneEffectsDialog.h"
#include "EditorButton.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

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

Color deltaColor(float v)
{
    if (v > 0.0f)
        return Color{90, 190, 110, 255};
    if (v < 0.0f)
        return Color{220, 90, 80, 255};
    return kTextMuted;
}

std::string formatDelta(float v)
{
    char buf[32];
    if (v > 0.0f)
        std::snprintf(buf, sizeof(buf), "+%.0f", v);
    else if (v < 0.0f)
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    else
        std::snprintf(buf, sizeof(buf), "0");
    return buf;
}

void readUseDeltas(const nlohmann::json& obj, StatDeltaSet& out)
{
    out.health = obj.value("useHealthDelta", 0.0f);
    out.energy = obj.value("useEnergyDelta", 0.0f);
    out.resolve = obj.value("useResolveDelta", 0.0f);
    out.lucidity = obj.value("useLucidityDelta", 0.0f);
    out.charisma = obj.value("useCharismaDelta", 0.0f);
}

void writeUseDeltaField(nlohmann::json& obj, const char* key, float value)
{
    if (value == 0.0f)
        obj.erase(key);
    else
        obj[key] = value;
}

void writeUseDeltas(nlohmann::json& obj, const StatDeltaSet& deltas)
{
    writeUseDeltaField(obj, "useHealthDelta", deltas.health);
    writeUseDeltaField(obj, "useEnergyDelta", deltas.energy);
    writeUseDeltaField(obj, "useResolveDelta", deltas.resolve);
    writeUseDeltaField(obj, "useLucidityDelta", deltas.lucidity);
    writeUseDeltaField(obj, "useCharismaDelta", deltas.charisma);
}

struct StatField
{
    const char* label;
    float* value;
    int focusId;
};

float drawStatRow(
    Font font,
    float x,
    float y,
    float width,
    StatDeltaSet& deltas,
    int focusBase,
    int& focusField,
    bool canClick,
    Vector2 mouse,
    bool enabled)
{
    StatField fields[] = {
        {"Health", &deltas.health, focusBase + 0},
        {"Energy", &deltas.energy, focusBase + 1},
        {"Resolve", &deltas.resolve, focusBase + 2},
        {"Lucidity", &deltas.lucidity, focusBase + 3},
        {"Charisma", &deltas.charisma, focusBase + 4},
    };
    const float colW = width / 5.0f;
    for (int i = 0; i < 5; ++i)
    {
        const float cx = x + colW * static_cast<float>(i);
        DrawTextEx(font, fields[i].label, {cx, y}, kFontTiny, 1.0f, kTextMuted);
        const Rectangle box = {cx, y + 16.0f, colW - 8.0f, 28.0f};
        DrawRectangleRec(box, Color{22, 20, 28, 255});
        DrawRectangleLinesEx(
            box,
            1.0f,
            (focusField == fields[i].focusId) ? kPanelBorder : kPanelInnerEdge);

        const std::string shown = formatDelta(*fields[i].value);
        DrawTextEx(
            font,
            shown.c_str(),
            {box.x + 8.0f, box.y + 6.0f},
            kFontSmall,
            1.0f,
            deltaColor(*fields[i].value));

        const Rectangle minus = {box.x + box.width - 52.0f, box.y + 4.0f, 22.0f, 20.0f};
        const Rectangle plus = {box.x + box.width - 26.0f, box.y + 4.0f, 22.0f, 20.0f};
        drawEditorButton(font, minus, "-", false, enabled);
        drawEditorButton(font, plus, "+", false, enabled);
        if (canClick && enabled)
        {
            if (CheckCollisionPointRec(mouse, minus))
                *fields[i].value -= 1.0f;
            else if (CheckCollisionPointRec(mouse, plus))
                *fields[i].value += 1.0f;
            else if (CheckCollisionPointRec(mouse, box))
                focusField = fields[i].focusId;
        }
    }
    return 48.0f;
}

} // namespace

std::string summarizeSceneEffects(const nlohmann::json& scene)
{
    if (!scene.is_object())
        return "";

    std::ostringstream out;
    bool any = false;
    const float exam = scene.value("examineLucidityDelta", 0.0f);
    if (exam != 0.0f)
    {
        out << "Examine L" << formatDelta(exam);
        any = true;
    }

    StatDeltaSet use;
    readUseDeltas(scene, use);
    if (use.anyNonZero())
    {
        if (any)
            out << " · ";
        out << "Use";
        if (use.health != 0.0f)
            out << " H" << formatDelta(use.health);
        if (use.energy != 0.0f)
            out << " E" << formatDelta(use.energy);
        if (use.resolve != 0.0f)
            out << " R" << formatDelta(use.resolve);
        if (use.lucidity != 0.0f)
            out << " L" << formatDelta(use.lucidity);
        if (use.charisma != 0.0f)
            out << " C" << formatDelta(use.charisma);
        any = true;
    }

    int interCount = 0;
    int interWithDelta = 0;
    if (scene.contains("interactions") && scene["interactions"].is_array())
    {
        for (const auto& row : scene["interactions"])
        {
            if (!row.is_object())
                continue;
            ++interCount;
            StatDeltaSet d;
            readUseDeltas(row, d);
            if (d.anyNonZero())
                ++interWithDelta;
        }
    }
    if (interCount > 0)
    {
        if (any)
            out << " · ";
        out << interCount << " interaction" << (interCount == 1 ? "" : "s");
        if (interWithDelta > 0)
            out << " (" << interWithDelta << " with Δ)";
        any = true;
    }
    return any ? out.str() : "";
}

void SceneEffectsDialog::loadFromScene()
{
    examineLucidityDelta = 0.0f;
    examineLucidityOncePerDay = false;
    useDeltas = {};
    useRepeatStatus = false;
    interactions.clear();
    if (docs == nullptr || sceneId.empty())
        return;
    const nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
        return;

    examineLucidityDelta = scene->value("examineLucidityDelta", 0.0f);
    examineLucidityOncePerDay = scene->value("examineLucidityOncePerDay", false);
    readUseDeltas(*scene, useDeltas);
    useRepeatStatus = scene->value("useRepeatStatus", false);

    if (scene->contains("interactions") && (*scene)["interactions"].is_array())
    {
        for (const nlohmann::json& row : (*scene)["interactions"])
        {
            if (!row.is_object())
                continue;
            SceneInteractionEffectsEdit edit;
            edit.id = row.value("id", "");
            edit.label = row.value("label", edit.id.empty() ? "(interaction)" : edit.id);
            readUseDeltas(row, edit.deltas);
            interactions.push_back(edit);
        }
    }
}

bool SceneEffectsDialog::saveToScene()
{
    if (docs == nullptr || sceneId.empty())
    {
        error = "No scene selected.";
        return false;
    }
    nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
    {
        error = "Scene missing.";
        return false;
    }

    if (examineLucidityDelta == 0.0f)
        scene->erase("examineLucidityDelta");
    else
        (*scene)["examineLucidityDelta"] = examineLucidityDelta;

    if (examineLucidityOncePerDay)
        (*scene)["examineLucidityOncePerDay"] = true;
    else
        scene->erase("examineLucidityOncePerDay");

    writeUseDeltas(*scene, useDeltas);
    if (useRepeatStatus)
        (*scene)["useRepeatStatus"] = true;
    else
        scene->erase("useRepeatStatus");

    if (scene->contains("interactions") && (*scene)["interactions"].is_array())
    {
        nlohmann::json& arr = (*scene)["interactions"];
        const size_t n = std::min(arr.size(), interactions.size());
        for (size_t i = 0; i < n; ++i)
        {
            if (!arr[i].is_object())
                continue;
            writeUseDeltas(arr[i], interactions[i].deltas);
        }
    }

    docs->markDirty();
    if (!docs->scenes.save())
    {
        error = "Failed to write scenes.json";
        return false;
    }
    docs->dirty = false;
    status = "Saved scene effects.";
    error.clear();
    if (onSaved)
        onSaved();
    return true;
}

float* SceneEffectsDialog::focusFloat()
{
    // focusField encoding:
    // 0 = examine lucidity
    // 10..14 = scene use stats
    // 100 + i*10 + s = interaction i stat s
    if (focusField == 0)
        return &examineLucidityDelta;
    if (focusField >= 10 && focusField <= 14)
    {
        float* vals[] = {
            &useDeltas.health,
            &useDeltas.energy,
            &useDeltas.resolve,
            &useDeltas.lucidity,
            &useDeltas.charisma};
        return vals[focusField - 10];
    }
    if (focusField >= 100)
    {
        const int idx = (focusField - 100) / 10;
        const int stat = (focusField - 100) % 10;
        if (idx >= 0 && idx < static_cast<int>(interactions.size()) && stat >= 0 && stat < 5)
        {
            float* vals[] = {
                &interactions[static_cast<size_t>(idx)].deltas.health,
                &interactions[static_cast<size_t>(idx)].deltas.energy,
                &interactions[static_cast<size_t>(idx)].deltas.resolve,
                &interactions[static_cast<size_t>(idx)].deltas.lucidity,
                &interactions[static_cast<size_t>(idx)].deltas.charisma};
            return vals[stat];
        }
    }
    return nullptr;
}

void SceneEffectsDialog::typeIntoFocusedField()
{
    float* target = focusFloat();
    if (target == nullptr)
        return;

    // Digits / minus / backspace via a temporary string each key — simple replace model.
    static thread_local std::string editBuf;
    static thread_local int editFocus = -2;
    if (editFocus != focusField)
    {
        editFocus = focusField;
        editBuf = formatDelta(*target);
        if (editBuf == "0")
            editBuf.clear();
    }

    int cp = GetCharPressed();
    while (cp > 0)
    {
        if ((cp >= '0' && cp <= '9') || cp == '-' || cp == '+' || cp == '.')
            insertUtf8(editBuf, cp);
        cp = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        backspace(editBuf);

    if (editBuf.empty() || editBuf == "-" || editBuf == "+" || editBuf == "."
        || editBuf == "-." || editBuf == "+.")
        return;
    try
    {
        *target = std::stof(editBuf);
    }
    catch (...)
    {
    }
}

void SceneEffectsDialog::openForScene(const std::string& id)
{
    if (docs == nullptr || id.empty() || !docs->scenes.hasScene(id))
        return;
    sceneId = id;
    loadFromScene();
    status.clear();
    error.clear();
    focusField = -1;
    scrollY = 0.0f;
    open = true;
    ignoreInputFrames = 1;
    waitMouseRelease = true;
}

void SceneEffectsDialog::closeDialog()
{
    open = false;
    focusField = -1;
    error.clear();
}

void SceneEffectsDialog::handleInput(int screenW, int screenH)
{
    (void)screenW;
    (void)screenH;
    if (!open)
        return;
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
    if (IsKeyPressed(KEY_ESCAPE))
        closeDialog();
}

void SceneEffectsDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool canClick =
        !waitMouseRelease && ignoreInputFrames <= 0
        && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);

    const float dialogW = std::min(820.0f, screenW - 40.0f);
    const float dialogH = std::min(640.0f, screenH - 40.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        "Scene Effects",
        {dialog.x + 20.0f, dialog.y + 16.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    DrawTextEx(
        font,
        ("Scene: " + sceneId).c_str(),
        {dialog.x + 20.0f, dialog.y + 46.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float pad = 16.0f;
    const float footerH = 56.0f;
    const Rectangle content = {
        dialog.x + pad,
        dialog.y + 70.0f,
        dialog.width - pad * 2.0f,
        dialogH - 70.0f - footerH};
    DrawRectangleRec(content, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(content, 1.0f, kPanelInnerEdge);

    float contentHeight = 280.0f + static_cast<float>(interactions.size()) * 110.0f;
    const float maxScroll = std::max(0.0f, contentHeight - content.height);
    if (CheckCollisionPointRec(mouse, content))
        scrollY -= GetMouseWheelMove() * 28.0f;
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);

    BeginScissorMode(
        static_cast<int>(content.x),
        static_cast<int>(content.y),
        static_cast<int>(content.width),
        static_cast<int>(content.height));

    float y = content.y + 12.0f - scrollY;
    const float x = content.x + 12.0f;
    const float w = content.width - 24.0f;

    DrawTextEx(font, "Examine", {x, y}, kFontSmall, 1.0f, kTextPrimary);
    y += 22.0f;
    DrawTextEx(font, "Lucidity Δ", {x, y}, kFontTiny, 1.0f, kTextMuted);
    const Rectangle examBox = {x, y + 16.0f, 120.0f, 28.0f};
    DrawRectangleRec(examBox, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(
        examBox, 1.0f, focusField == 0 ? kPanelBorder : kPanelInnerEdge);
    DrawTextEx(
        font,
        formatDelta(examineLucidityDelta).c_str(),
        {examBox.x + 8.0f, examBox.y + 6.0f},
        kFontSmall,
        1.0f,
        deltaColor(examineLucidityDelta));
    const Rectangle examMinus = {examBox.x + examBox.width + 8.0f, examBox.y + 4.0f, 28.0f, 20.0f};
    const Rectangle examPlus = {examMinus.x + 34.0f, examBox.y + 4.0f, 28.0f, 20.0f};
    drawEditorButton(font, examMinus, "-", false, true);
    drawEditorButton(font, examPlus, "+", false, true);

    const Rectangle onceBtn = {examPlus.x + 40.0f, examBox.y, 150.0f, 28.0f};
    drawEditorButton(
        font,
        onceBtn,
        examineLucidityOncePerDay ? "Once/day: ON" : "Once/day: off",
        examineLucidityOncePerDay,
        true);

    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, examBox))
            focusField = 0;
        else if (CheckCollisionPointRec(mouse, examMinus))
            examineLucidityDelta -= 1.0f;
        else if (CheckCollisionPointRec(mouse, examPlus))
            examineLucidityDelta += 1.0f;
        else if (CheckCollisionPointRec(mouse, onceBtn))
            examineLucidityOncePerDay = !examineLucidityOncePerDay;
    }
    y += 56.0f;

    DrawTextEx(font, "Scene Use (direct Use button)", {x, y}, kFontSmall, 1.0f, kTextPrimary);
    y += 22.0f;
    y += drawStatRow(font, x, y, w, useDeltas, 10, focusField, canClick, mouse, true);

    const Rectangle repeatBtn = {x, y + 4.0f, 160.0f, 28.0f};
    drawEditorButton(
        font,
        repeatBtn,
        useRepeatStatus ? "Repeat status: ON" : "Repeat status: off",
        useRepeatStatus,
        true);
    if (canClick && CheckCollisionPointRec(mouse, repeatBtn))
        useRepeatStatus = !useRepeatStatus;
    y += 44.0f;

    DrawTextEx(
        font,
        "Interactions (edit deltas on existing entries)",
        {x, y},
        kFontSmall,
        1.0f,
        kTextPrimary);
    y += 24.0f;

    if (interactions.empty())
    {
        DrawTextEx(
            font,
            "(no interactions on this scene)",
            {x, y},
            kFontTiny,
            1.0f,
            kTextMuted);
        y += 28.0f;
    }
    else
    {
        for (size_t i = 0; i < interactions.size(); ++i)
        {
            SceneInteractionEffectsEdit& inter = interactions[i];
            const Rectangle card = {x, y, w, 96.0f};
            DrawRectangleRec(card, Color{26, 24, 34, 255});
            DrawRectangleLinesEx(card, 1.0f, kPanelInnerEdge);
            const std::string title =
                inter.label.empty() ? inter.id : (inter.label + "  [" + inter.id + "]");
            DrawTextEx(
                font,
                title.c_str(),
                {card.x + 10.0f, card.y + 8.0f},
                kFontTiny,
                1.0f,
                kTextPrimary);
            drawStatRow(
                font,
                card.x + 10.0f,
                card.y + 28.0f,
                card.width - 20.0f,
                inter.deltas,
                100 + static_cast<int>(i) * 10,
                focusField,
                canClick,
                mouse,
                true);
            y += 110.0f;
        }
    }

    EndScissorMode();

    const float btnW = 120.0f;
    const float btnH = 34.0f;
    const float btnY = dialog.y + dialogH - btnH - 12.0f;
    const Rectangle saveBtn = {dialog.x + pad, btnY, btnW, btnH};
    const Rectangle closeBtn = {dialog.x + dialogW - btnW - pad, btnY, btnW, btnH};
    drawEditorButton(font, saveBtn, "Save", true, true);
    drawEditorButton(font, closeBtn, "Close", false, true);
    if (canClick)
    {
        if (CheckCollisionPointRec(mouse, saveBtn))
            saveToScene();
        else if (CheckCollisionPointRec(mouse, closeBtn))
            closeDialog();
        else if (!CheckCollisionPointRec(mouse, dialog))
            closeDialog();
    }

    if (!status.empty())
        DrawTextEx(
            font,
            status.c_str(),
            {saveBtn.x + btnW + 12.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            Color{120, 180, 120, 255});
    if (!error.empty())
        DrawTextEx(
            font,
            error.c_str(),
            {saveBtn.x + btnW + 12.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            Color{220, 100, 90, 255});
}

} // namespace timberline_editor
