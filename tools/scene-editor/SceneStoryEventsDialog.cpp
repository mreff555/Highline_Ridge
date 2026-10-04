/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneStoryEventsDialog.h"
#include "EditorInput.h"
#include "EditorButton.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cmath>
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

std::string readStringArrayCsv(const nlohmann::json& node, const char* key)
{
    if (!node.contains(key) || !node[key].is_array())
        return {};
    std::ostringstream oss;
    bool first = true;
    for (const nlohmann::json& entry : node[key])
    {
        if (!entry.is_string())
            continue;
        if (!first)
            oss << ", ";
        first = false;
        oss << entry.get<std::string>();
    }
    return oss.str();
}

nlohmann::json csvToJsonArray(const std::string& csv)
{
    nlohmann::json arr = nlohmann::json::array();
    std::string token;
    for (size_t i = 0; i <= csv.size(); ++i)
    {
        const char ch = (i < csv.size() ? csv[i] : ',');
        if (ch == ',' || i == csv.size())
        {
            // trim
            size_t a = 0;
            while (a < token.size() && (token[a] == ' ' || token[a] == '\t'))
                ++a;
            size_t b = token.size();
            while (b > a && (token[b - 1] == ' ' || token[b - 1] == '\t'))
                --b;
            if (b > a)
                arr.push_back(token.substr(a, b - a));
            token.clear();
            continue;
        }
        token.push_back(ch);
    }
    return arr;
}

} // namespace

std::vector<std::string> SceneStoryEventsDialog::splitCsv(const std::string& csv)
{
    std::vector<std::string> out;
    const nlohmann::json arr = csvToJsonArray(csv);
    for (const nlohmann::json& entry : arr)
        out.push_back(entry.get<std::string>());
    return out;
}

std::string SceneStoryEventsDialog::joinCsv(const std::vector<std::string>& values)
{
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
            oss << ", ";
        oss << values[i];
    }
    return oss.str();
}

StoryEventEdit* SceneStoryEventsDialog::selected()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()))
        return nullptr;
    return &entries[static_cast<size_t>(selectedIndex)];
}

const StoryEventEdit* SceneStoryEventsDialog::selected() const
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()))
        return nullptr;
    return &entries[static_cast<size_t>(selectedIndex)];
}

std::string* SceneStoryEventsDialog::focusString()
{
    StoryEventEdit* edit = selected();
    if (edit == nullptr)
        return nullptr;
    switch (focusField)
    {
    case FocusId:
        return &edit->id;
    case FocusDirection:
        return &edit->direction;
    case FocusRequiresFlags:
        return &edit->requiresFlagsCsv;
    case FocusUnlessFlags:
        return &edit->unlessFlagsCsv;
    case FocusSetsFlags:
        return &edit->setsFlagsCsv;
    case FocusClearsFlags:
        return &edit->clearsFlagsCsv;
    case FocusConsumedStatus:
        return &edit->requiresConsumedStatusCsv;
    case FocusNarrativeHeader:
        return &edit->narrativeHeader;
    case FocusNarrative:
        return &edit->narrative;
    default:
        return nullptr;
    }
}

void SceneStoryEventsDialog::loadFromScene()
{
    entries.clear();
    selectedIndex = -1;
    focusField = FocusNone;
    if (docs == nullptr || sceneId.empty())
        return;
    const nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
        return;
    if (!scene->contains("storyEvents") || !(*scene)["storyEvents"].is_array())
        return;

    for (const nlohmann::json& row : (*scene)["storyEvents"])
    {
        if (!row.is_object())
            continue;
        StoryEventEdit edit;
        edit.id = row.value("id", "");
        edit.when = row.value("when", "exit");
        edit.direction = row.value("direction", "");
        edit.requiresExamined = row.value("requiresExamined", false);
        edit.requiresFlagsCsv = readStringArrayCsv(row, "requiresFlags");
        edit.unlessFlagsCsv = readStringArrayCsv(row, "unlessFlags");
        edit.setsFlagsCsv = readStringArrayCsv(row, "setsFlags");
        edit.clearsFlagsCsv = readStringArrayCsv(row, "clearsFlags");
        edit.requiresConsumedStatusCsv = readStringArrayCsv(row, "requiresConsumedStatus");
        edit.narrativeHeader = row.value("narrativeHeader", "Examining:");
        edit.narrative = row.value("narrative", "");
        edit.blockMovement = row.value("blockMovement", false);
        edit.refreshTakeables = row.value("refreshTakeables", false);
        edit.once = row.value("once", true);
        if (edit.id.empty())
            continue;
        entries.push_back(std::move(edit));
    }
    if (!entries.empty())
        selectedIndex = 0;
}

bool SceneStoryEventsDialog::saveToScene()
{
    if (docs == nullptr || sceneId.empty())
    {
        error = "No scene selected.";
        return false;
    }
    nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
    {
        error = "Scene not found in document.";
        return false;
    }

    nlohmann::json events = nlohmann::json::array();
    for (const StoryEventEdit& edit : entries)
    {
        if (edit.id.empty())
        {
            error = "Every event needs an id.";
            return false;
        }
        if (edit.when != "enter" && edit.when != "exit" && edit.when != "examine")
        {
            error = "Event '" + edit.id + "' has invalid when (use enter/exit/examine).";
            return false;
        }
        if (edit.when == "exit" && edit.direction.empty())
        {
            error = "Exit event '" + edit.id + "' needs a direction.";
            return false;
        }

        nlohmann::json row = nlohmann::json::object();
        row["id"] = edit.id;
        row["when"] = edit.when;
        if (edit.when == "exit")
            row["direction"] = edit.direction;
        if (edit.requiresExamined)
            row["requiresExamined"] = true;
        const nlohmann::json requiresFlags = csvToJsonArray(edit.requiresFlagsCsv);
        if (!requiresFlags.empty())
            row["requiresFlags"] = requiresFlags;
        const nlohmann::json unlessFlags = csvToJsonArray(edit.unlessFlagsCsv);
        if (!unlessFlags.empty())
            row["unlessFlags"] = unlessFlags;
        const nlohmann::json setsFlags = csvToJsonArray(edit.setsFlagsCsv);
        if (!setsFlags.empty())
            row["setsFlags"] = setsFlags;
        const nlohmann::json clearsFlags = csvToJsonArray(edit.clearsFlagsCsv);
        if (!clearsFlags.empty())
            row["clearsFlags"] = clearsFlags;
        const nlohmann::json consumed = csvToJsonArray(edit.requiresConsumedStatusCsv);
        if (!consumed.empty())
            row["requiresConsumedStatus"] = consumed;
        if (!edit.narrativeHeader.empty())
            row["narrativeHeader"] = edit.narrativeHeader;
        if (!edit.narrative.empty())
            row["narrative"] = edit.narrative;
        if (edit.blockMovement)
            row["blockMovement"] = true;
        if (edit.refreshTakeables)
            row["refreshTakeables"] = true;
        row["once"] = edit.once;
        events.push_back(row);
    }

    if (events.empty())
        scene->erase("storyEvents");
    else
        (*scene)["storyEvents"] = events;

    docs->markDirty();
    if (!docs->scenes.save())
    {
        error = "Failed to write scenes.json";
        return false;
    }
    docs->dirty = false;
    status = "Saved " + std::to_string(entries.size()) + " story event(s).";
    error.clear();
    if (onSaved)
        onSaved();
    return true;
}

void SceneStoryEventsDialog::addEvent()
{
    StoryEventEdit edit;
    edit.id = "event_" + std::to_string(entries.size() + 1);
    edit.when = "exit";
    edit.direction = "right";
    edit.narrativeHeader = "Examining:";
    edit.blockMovement = true;
    edit.once = true;
    entries.push_back(std::move(edit));
    selectedIndex = static_cast<int>(entries.size()) - 1;
    focusField = FocusId;
    status = "Added event.";
    error.clear();
}

void SceneStoryEventsDialog::removeSelected()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()))
        return;
    entries.erase(entries.begin() + selectedIndex);
    if (entries.empty())
        selectedIndex = -1;
    else if (selectedIndex >= static_cast<int>(entries.size()))
        selectedIndex = static_cast<int>(entries.size()) - 1;
    focusField = FocusNone;
    status = "Removed event.";
}

void SceneStoryEventsDialog::openForScene(const std::string& id)
{
    if (docs == nullptr || id.empty() || !docs->scenes.hasScene(id))
        return;
    sceneId = id;
    loadFromScene();
    status.clear();
    error.clear();
    listScroll = 0.0f;
    detailScroll = 0.0f;
    focusField = FocusNone;
    open = true;
    ignoreInputFrames = 1;
    waitMouseRelease = true;
}

void SceneStoryEventsDialog::closeDialog()
{
    open = false;
    focusField = FocusNone;
    error.clear();
}

void SceneStoryEventsDialog::typeIntoFocusedField()
{
    std::string* buffer = focusString();
    if (buffer == nullptr)
        return;

    const bool multiline = (focusField == FocusNarrative);
    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
            buffer->append(clip);
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (cp == '\n' || cp == '\r')
        {
            if (multiline)
                buffer->push_back('\n');
        }
        else if (cp >= 32)
        {
            insertUtf8(*buffer, cp);
        }
        cp = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        backspace(*buffer);
    // Enter is a key event on macOS/GLFW — not always in GetCharPressed (#52).
    if (multiline
        && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)
            || IsKeyPressedRepeat(KEY_ENTER) || IsKeyPressedRepeat(KEY_KP_ENTER)))
        buffer->push_back('\n');
}

void SceneStoryEventsDialog::handleInput(int screenW, int screenH)
{
    (void)screenW;
    (void)screenH;
    if (!open)
        return;

    if (waitMouseRelease)
    {
        if (!editorMouseDown(MOUSE_BUTTON_LEFT))
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

void SceneStoryEventsDialog::draw(int screenW, int screenH)
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

    const float dialogW = std::min(980.0f, screenW - 32.0f);
    const float dialogH = std::min(640.0f, screenH - 32.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(bold, "Story Events", {dialog.x + 20.0f, dialog.y + 14.0f}, kFontHeading, 1.0f, kTextPrimary);
    DrawTextEx(
        font,
        ("Scene: " + sceneId).c_str(),
        {dialog.x + 20.0f, dialog.y + 42.0f},
        kFontTiny,
        1.0f,
        kTextMuted);
    DrawTextEx(
        font,
        "Enter / exit / examine beats (flags, narrative, block movement).",
        {dialog.x + 20.0f, dialog.y + 58.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float pad = 16.0f;
    const float footerH = 56.0f;
    const Rectangle content = {
        dialog.x + pad,
        dialog.y + 80.0f,
        dialog.width - pad * 2.0f,
        dialogH - 80.0f - footerH};

    const float listW = 260.0f;
    const Rectangle listPanel = {content.x, content.y, listW, content.height};
    const Rectangle detailPanel = {
        content.x + listW + 10.0f,
        content.y,
        content.width - listW - 10.0f,
        content.height};
    DrawRectangleRec(listPanel, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(listPanel, 1.0f, kPanelInnerEdge);
    DrawRectangleRec(detailPanel, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(detailPanel, 1.0f, kPanelInnerEdge);

    // --- List ---
    const float listBtnH = 28.0f;
    const Rectangle addBtn = {
        listPanel.x + 8.0f, listPanel.y + listPanel.height - listBtnH - 8.0f, 110.0f, listBtnH};
    const Rectangle removeBtn = {
        addBtn.x + 118.0f, addBtn.y, 110.0f, listBtnH};
    drawEditorButton(font, addBtn, "Add", true, true);
    drawEditorButton(font, removeBtn, "Remove", false, selected() != nullptr);
    if (canClick && CheckCollisionPointRec(mouse, addBtn))
        addEvent();
    if (canClick && selected() != nullptr && CheckCollisionPointRec(mouse, removeBtn))
        removeSelected();

    const Rectangle listBounds = {
        listPanel.x + 8.0f,
        listPanel.y + 8.0f,
        listPanel.width - 16.0f,
        listPanel.height - listBtnH - 24.0f};
    const float rowH = 36.0f;
    const float contentH = static_cast<float>(entries.size()) * rowH + 8.0f;
    const float maxScroll = std::max(0.0f, contentH - listBounds.height);
    if (CheckCollisionPointRec(mouse, listBounds))
        listScroll -= GetMouseWheelMove() * 24.0f;
    listScroll = std::clamp(listScroll, 0.0f, maxScroll);

    BeginScissorMode(
        static_cast<int>(listBounds.x),
        static_cast<int>(listBounds.y),
        static_cast<int>(listBounds.width),
        static_cast<int>(listBounds.height));
    float y = listBounds.y + 4.0f - listScroll;
    if (entries.empty())
    {
        DrawTextEx(
            font,
            "(no story events)",
            {listBounds.x + 6.0f, y + 6.0f},
            kFontSmall,
            1.0f,
            kTextMuted);
    }
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const Rectangle row = {listBounds.x, y, listBounds.width, rowH - 4.0f};
        const bool sel = (static_cast<int>(i) == selectedIndex);
        DrawRectangleRec(row, sel ? kSelection : Color{26, 24, 34, 255});
        DrawRectangleLinesEx(row, 1.0f, kPanelInnerEdge);
        const std::string title = entries[i].id.empty() ? "(unnamed)" : entries[i].id;
        const std::string meta = entries[i].when
            + (entries[i].when == "exit" && !entries[i].direction.empty()
                   ? (" " + entries[i].direction)
                   : "");
        DrawTextEx(font, title.c_str(), {row.x + 8.0f, row.y + 4.0f}, kFontSmall, 1.0f, kTextPrimary);
        DrawTextEx(font, meta.c_str(), {row.x + 8.0f, row.y + 18.0f}, kFontTiny, 1.0f, kTextMuted);
        if (canClick && CheckCollisionPointRec(mouse, row))
        {
            selectedIndex = static_cast<int>(i);
            focusField = FocusNone;
        }
        y += rowH;
    }
    EndScissorMode();

    // --- Detail ---
    auto drawField = [&](const char* label,
                         Rectangle field,
                         const std::string& value,
                         int fieldId,
                         bool multiline = false) {
        DrawTextEx(
            font,
            label,
            {field.x, field.y - 16.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
        const bool focused = (focusField == fieldId);
        DrawRectangleRec(field, Color{24, 22, 32, 255});
        DrawRectangleLinesEx(field, 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
        BeginScissorMode(
            static_cast<int>(field.x + 2),
            static_cast<int>(field.y + 2),
            static_cast<int>(field.width - 4),
            static_cast<int>(field.height - 4));
        DrawTextEx(
            font,
            value.empty() ? " " : value.c_str(),
            {field.x + 6.0f, field.y + 6.0f},
            kFontSmall,
            1.0f,
            value.empty() ? kTextMuted : kTextPrimary);
        EndScissorMode();
        if (canClick && CheckCollisionPointRec(mouse, field))
            focusField = fieldId;
        (void)multiline;
    };

    StoryEventEdit* edit = selected();
    if (edit == nullptr)
    {
        DrawTextEx(
            font,
            "Select or add an event.",
            {detailPanel.x + 14.0f, detailPanel.y + 14.0f},
            kFontSmall,
            1.0f,
            kTextMuted);
    }
    else
    {
        if (CheckCollisionPointRec(mouse, detailPanel))
            detailScroll -= GetMouseWheelMove() * 28.0f;
        detailScroll = std::max(0.0f, detailScroll);

        float dy = detailPanel.y + 14.0f - detailScroll;
        const float x = detailPanel.x + 12.0f;
        const float w = detailPanel.width - 24.0f;

        // when cycle
        DrawTextEx(font, "when", {x, dy}, kFontTiny, 1.0f, kTextMuted);
        dy += 16.0f;
        const char* whens[] = {"enter", "exit", "examine"};
        for (int i = 0; i < 3; ++i)
        {
            const Rectangle btn = {x + static_cast<float>(i) * 90.0f, dy, 84.0f, 26.0f};
            const bool on = (edit->when == whens[i]);
            drawEditorButton(font, btn, whens[i], on, true);
            if (canClick && CheckCollisionPointRec(mouse, btn))
                edit->when = whens[i];
        }
        dy += 42.0f;

        drawField("id", {x, dy, w * 0.55f, 28.0f}, edit->id, FocusId);
        drawField(
            "direction (exit)",
            {x + w * 0.58f, dy, w * 0.42f, 28.0f},
            edit->direction,
            FocusDirection);
        dy += 52.0f;

        drawField("requiresFlags (csv)", {x, dy, w, 28.0f}, edit->requiresFlagsCsv, FocusRequiresFlags);
        dy += 52.0f;
        drawField("unlessFlags (csv)", {x, dy, w, 28.0f}, edit->unlessFlagsCsv, FocusUnlessFlags);
        dy += 52.0f;
        drawField("setsFlags (csv)", {x, dy, w, 28.0f}, edit->setsFlagsCsv, FocusSetsFlags);
        dy += 52.0f;
        drawField("clearsFlags (csv)", {x, dy, w, 28.0f}, edit->clearsFlagsCsv, FocusClearsFlags);
        dy += 52.0f;
        drawField(
            "requiresConsumedStatus (csv)",
            {x, dy, w, 28.0f},
            edit->requiresConsumedStatusCsv,
            FocusConsumedStatus);
        dy += 52.0f;
        drawField(
            "narrativeHeader",
            {x, dy, w, 28.0f},
            edit->narrativeHeader,
            FocusNarrativeHeader);
        dy += 52.0f;
        drawField("narrative", {x, dy, w, 110.0f}, edit->narrative, FocusNarrative, true);
        dy += 130.0f;

        auto toggle = [&](const char* label, bool& value, float ox) {
            const Rectangle btn = {x + ox, dy, 140.0f, 26.0f};
            drawEditorButton(font, btn, label, value, true);
            if (canClick && CheckCollisionPointRec(mouse, btn))
                value = !value;
        };
        toggle(edit->requiresExamined ? "Exam: ON" : "Exam: off", edit->requiresExamined, 0.0f);
        toggle(edit->blockMovement ? "Block move: ON" : "Block move: off", edit->blockMovement, 148.0f);
        toggle(
            edit->refreshTakeables ? "Refresh take: ON" : "Refresh take: off",
            edit->refreshTakeables,
            296.0f);
        toggle(edit->once ? "Once: ON" : "Once: off", edit->once, 444.0f);
    }

    // Footer
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
    {
        DrawTextEx(
            font,
            status.c_str(),
            {saveBtn.x + btnW + 12.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            Color{120, 180, 120, 255});
    }
    if (!error.empty())
    {
        DrawTextEx(
            font,
            error.c_str(),
            {saveBtn.x + btnW + 12.0f, btnY + 8.0f},
            kFontTiny,
            1.0f,
            Color{220, 100, 90, 255});
    }
}

} // namespace timberline_editor
