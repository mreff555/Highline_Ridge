/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneInteractionsDialog.h"
#include "EditorInput.h"
#include "EditorButton.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cmath>
#include <cctype>
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

std::string toLowerCopy(std::string s)
{
    for (char& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool containsIgnoreCase(const std::string& hay, const std::string& needle)
{
    if (needle.empty())
        return true;
    return toLowerCopy(hay).find(toLowerCopy(needle)) != std::string::npos;
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

} // namespace

std::vector<std::string> SceneInteractionsDialog::splitCsv(const std::string& csv)
{
    std::vector<std::string> out;
    const nlohmann::json arr = csvToJsonArray(csv);
    for (const nlohmann::json& entry : arr)
        out.push_back(entry.get<std::string>());
    return out;
}

std::string SceneInteractionsDialog::joinCsv(const std::vector<std::string>& values)
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

std::string SceneInteractionsDialog::resolveItemName(const std::string& itemId) const
{
    if (docs == nullptr || itemId.empty())
        return itemId;
    const nlohmann::json* item = docs->itemJson(itemId);
    if (item != nullptr && item->is_object())
        return item->value("name", itemId);
    return itemId;
}

std::string SceneInteractionsDialog::uniqueInteractionId(const std::string& base) const
{
    std::string id = base.empty() ? "place_item" : base;
    bool clash = true;
    int n = 1;
    while (clash)
    {
        clash = false;
        for (const SceneInteractionEdit& e : entries)
        {
            if (e.id == id)
            {
                clash = true;
                break;
            }
        }
        if (!clash)
            break;
        ++n;
        id = base + "_" + std::to_string(n);
    }
    return id;
}

SceneInteractionEdit* SceneInteractionsDialog::selected()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()))
        return nullptr;
    return &entries[static_cast<size_t>(selectedIndex)];
}

const SceneInteractionEdit* SceneInteractionsDialog::selected() const
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()))
        return nullptr;
    return &entries[static_cast<size_t>(selectedIndex)];
}

std::string* SceneInteractionsDialog::focusString()
{
    SceneInteractionEdit* edit = selected();
    if (edit == nullptr)
        return nullptr;
    switch (focusField)
    {
    case FocusId:
        return &edit->id;
    case FocusLabel:
        return &edit->label;
    case FocusUseDetails:
        return &edit->useDetails;
    case FocusGrantItem:
        return &edit->grantItemId;
    case FocusUseFlag:
        return &edit->useFlag;
    case FocusHideFlag:
        return &edit->hideWhenStoryFlag;
    case FocusRequiresItems:
        return &edit->requiresAnyInventoryItemsCsv;
    default:
        return nullptr;
    }
}

void SceneInteractionsDialog::loadFromScene()
{
    entries.clear();
    selectedIndex = -1;
    focusField = FocusNone;
    if (docs == nullptr || sceneId.empty())
        return;
    const nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
        return;
    if (!scene->contains("interactions") || !(*scene)["interactions"].is_array())
        return;

    for (const nlohmann::json& row : (*scene)["interactions"])
    {
        if (!row.is_object())
            continue;
        SceneInteractionEdit edit;
        edit.original = row;
        edit.id = row.value("id", "");
        edit.label = row.value("label", edit.id.empty() ? "(interaction)" : edit.id);
        edit.useDetails = row.value("useDetails", "");
        edit.requiresExamine = row.value("requiresExamine", true);
        edit.repeat = row.value("repeat", false);
        edit.useFlag = row.value("useFlag", "");
        edit.hideWhenStoryFlag = row.value("hideWhenStoryFlag", "");
        edit.exitSceneId = row.value("exitSceneId", "");
        edit.requiresAnyInventoryItemsCsv =
            readStringArrayCsv(row, "requiresAnyInventoryItems");
        if (row.contains("grantItem") && row["grantItem"].is_object())
            edit.grantItemId = row["grantItem"].value("id", "");
        entries.push_back(std::move(edit));
    }
    if (!entries.empty())
        selectedIndex = 0;
}

bool SceneInteractionsDialog::saveToScene()
{
    if (docs == nullptr || sceneId.empty())
    {
        error = "No scene selected.";
        return false;
    }
    nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
    {
        error = "Scene not found.";
        return false;
    }

    std::vector<std::string> seenIds;
    nlohmann::json arr = nlohmann::json::array();
    for (const SceneInteractionEdit& edit : entries)
    {
        if (edit.id.empty())
        {
            error = "Every interaction needs an id.";
            return false;
        }
        for (const std::string& seen : seenIds)
        {
            if (seen == edit.id)
            {
                error = "Duplicate interaction id: " + edit.id;
                return false;
            }
        }
        seenIds.push_back(edit.id);

        nlohmann::json row =
            edit.original.is_object() ? edit.original : nlohmann::json::object();
        row["id"] = edit.id;
        row["label"] = edit.label.empty() ? edit.id : edit.label;
        if (!edit.useDetails.empty())
            row["useDetails"] = edit.useDetails;
        else
            row.erase("useDetails");
        row["requiresExamine"] = edit.requiresExamine;
        row["repeat"] = edit.repeat;
        if (!edit.useFlag.empty())
            row["useFlag"] = edit.useFlag;
        else
            row.erase("useFlag");
        if (!edit.hideWhenStoryFlag.empty())
            row["hideWhenStoryFlag"] = edit.hideWhenStoryFlag;
        else
            row.erase("hideWhenStoryFlag");

        if (!edit.grantItemId.empty())
        {
            if (!docs->itemsLoaded)
                docs->loadItemsDocument();
            if (docs->itemJson(edit.grantItemId) == nullptr)
            {
                error = "Unknown item id (create it in Items first): " + edit.grantItemId;
                return false;
            }
            nlohmann::json grant = nlohmann::json::object();
            if (row.contains("grantItem") && row["grantItem"].is_object())
                grant = row["grantItem"];
            grant["id"] = edit.grantItemId;
            row["grantItem"] = grant;
        }
        else
            row.erase("grantItem");

        const nlohmann::json reqItems = csvToJsonArray(edit.requiresAnyInventoryItemsCsv);
        if (!reqItems.empty())
            row["requiresAnyInventoryItems"] = reqItems;
        else
            row.erase("requiresAnyInventoryItems");

        // Preserve exitSceneId from original / edit hint.
        if (!edit.exitSceneId.empty())
            row["exitSceneId"] = edit.exitSceneId;

        arr.push_back(row);
    }

    if (arr.empty())
        scene->erase("interactions");
    else
        (*scene)["interactions"] = arr;

    docs->markDirty();
    if (!docs->scenes.save())
    {
        error = "Failed to write scenes.json";
        return false;
    }
    docs->dirty = false;
    status = "Saved " + std::to_string(entries.size()) + " interaction(s).";
    error.clear();
    loadFromScene();
    if (onSaved)
        onSaved();
    return true;
}

void SceneInteractionsDialog::addPlaceItem(const std::string& itemId)
{
    if (itemId.empty())
        return;
    if (!docs->itemsLoaded)
        docs->loadItemsDocument();
    if (docs->itemJson(itemId) == nullptr)
    {
        error = "Unknown item id: " + itemId;
        return;
    }

    const std::string name = resolveItemName(itemId);
    SceneInteractionEdit edit;
    edit.id = uniqueInteractionId("place_" + itemId);
    edit.label = "Search for the " + name + ".";
    edit.useDetails =
        "You search carefully and find the " + name + ".";
    edit.grantItemId = itemId;
    edit.requiresExamine = true;
    edit.repeat = false;
    edit.useFlag = sceneId + ":" + itemId + "_taken";
    edit.hideWhenStoryFlag = edit.useFlag;
    edit.original = nlohmann::json::object();
    entries.push_back(std::move(edit));
    selectedIndex = static_cast<int>(entries.size()) - 1;
    focusField = FocusLabel;
    addPickerOpen = false;
    addFilter.clear();
    status = "Added place-item interaction for " + name
        + " (player uses Use after Examine).";
    error.clear();
}

void SceneInteractionsDialog::removeSelected()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()))
        return;
    const bool hadExit = !entries[static_cast<size_t>(selectedIndex)].exitSceneId.empty();
    entries.erase(entries.begin() + selectedIndex);
    if (entries.empty())
        selectedIndex = -1;
    else if (selectedIndex >= static_cast<int>(entries.size()))
        selectedIndex = static_cast<int>(entries.size()) - 1;
    focusField = FocusNone;
    status = hadExit ? "Removed interaction (had map Use exit)." : "Removed interaction.";
    error.clear();
}

void SceneInteractionsDialog::openForScene(const std::string& id)
{
    if (docs == nullptr || id.empty() || !docs->scenes.hasScene(id))
        return;
    if (!docs->itemsLoaded)
        docs->loadItemsDocument();
    sceneId = id;
    loadFromScene();
    status.clear();
    error.clear();
    listScroll = 0.0f;
    detailScroll = 0.0f;
    addPickerOpen = false;
    addFilter.clear();
    focusField = FocusNone;
    open = true;
    ignoreInputFrames = 1;
    waitMouseRelease = true;
}

void SceneInteractionsDialog::closeDialog()
{
    open = false;
    waitMouseRelease = false;
    addPickerOpen = false;
    focusField = FocusNone;
}

void SceneInteractionsDialog::typeIntoFilter()
{
    int codepoint = 0;
    while ((codepoint = GetCharPressed()) > 0)
        insertUtf8(addFilter, codepoint);
    if (IsKeyPressed(KEY_BACKSPACE))
        backspace(addFilter);
}

void SceneInteractionsDialog::typeIntoFocusedField()
{
    std::string* target = focusString();
    if (target == nullptr)
        return;
    const bool multiline = (focusField == FocusUseDetails);
    int codepoint = 0;
    while ((codepoint = GetCharPressed()) > 0)
    {
        if (!multiline && (codepoint == '\n' || codepoint == '\r'))
            continue;
        insertUtf8(*target, codepoint);
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        backspace(*target);
    // Enter is a key event on macOS/GLFW — not always in GetCharPressed (#52).
    if (multiline
        && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)
            || IsKeyPressedRepeat(KEY_ENTER) || IsKeyPressedRepeat(KEY_KP_ENTER)))
        target->push_back('\n');
}

void SceneInteractionsDialog::handleInput(int screenW, int screenH)
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
        if (addPickerOpen)
            addPickerOpen = false;
        else
            closeDialog();
        return;
    }
    if (addPickerOpen)
        typeIntoFilter();
    else
        typeIntoFocusedField();
}

void SceneInteractionsDialog::draw(int screenW, int screenH)
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
    const float dialogW = std::min(860.0f, screenW - 32.0f);
    const float dialogH = std::min(640.0f, screenH - 24.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f, (screenH - dialogH) * 0.5f, dialogW, dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    const float pad = 14.0f;
    float y = dialog.y + pad;
    DrawTextEx(
        bold,
        ("Scene interactions: " + sceneId).c_str(),
        {dialog.x + pad, y},
        kFontHeading,
        1.0f,
        kTextPrimary);
    y += 24.0f;
    DrawTextEx(
        font,
        "Place items via Use (after Examine if gated) — not the Take button. "
        "Map Use stubs (exitSceneId) appear here too.",
        {dialog.x + pad, y},
        kFontTiny,
        1.0f,
        kTextMuted);
    y += 20.0f;

    const float listW = 260.0f;
    const float footerH = 48.0f;
    const Rectangle listBounds = {
        dialog.x + pad, y, listW, dialog.y + dialogH - footerH - y};
    const Rectangle detailBounds = {
        listBounds.x + listW + 12.0f,
        y,
        dialog.width - pad * 2.0f - listW - 12.0f,
        listBounds.height};

    // List
    DrawRectangleRec(listBounds, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(listBounds, 1.0f, kPanelInnerEdge);
    const float rowH = 26.0f;
    const float contentH = static_cast<float>(entries.size()) * rowH + 8.0f;
    const float maxListScroll = std::max(0.0f, contentH - listBounds.height);
    if (CheckCollisionPointRec(mouse, listBounds))
        listScroll = std::clamp(listScroll - GetMouseWheelMove() * 24.0f, 0.0f, maxListScroll);

    BeginScissorMode(
        (int)listBounds.x, (int)listBounds.y, (int)listBounds.width, (int)listBounds.height);
    float ly = listBounds.y + 4.0f - listScroll;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const SceneInteractionEdit& e = entries[i];
        Rectangle row = {listBounds.x + 2.0f, ly, listBounds.width - 4.0f, rowH - 2.0f};
        const bool sel = selectedIndex == static_cast<int>(i);
        if (sel)
            DrawRectangleRec(row, Color{50, 44, 62, 255});
        else if (CheckCollisionPointRec(mouse, row))
            DrawRectangleRec(row, Color{36, 32, 44, 200});
        std::string mark;
        if (!e.grantItemId.empty())
            mark = "[item] ";
        else if (!e.exitSceneId.empty())
            mark = "[exit] ";
        const std::string label = mark + (e.label.empty() ? e.id : e.label);
        DrawTextEx(
            font, label.c_str(), {row.x + 6.0f, row.y + 4.0f}, kFontSmall, 1.0f, kTextPrimary);
        if (canClick && !addPickerOpen && CheckCollisionPointRec(mouse, row))
        {
            selectedIndex = static_cast<int>(i);
            focusField = FocusNone;
        }
        ly += rowH;
    }
    EndScissorMode();
    if (entries.empty())
        DrawTextEx(
            font,
            "(none)",
            {listBounds.x + 10.0f, listBounds.y + 10.0f},
            kFontSmall,
            1.0f,
            kTextMuted);

    // Detail
    DrawRectangleRec(detailBounds, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(detailBounds, 1.0f, kPanelInnerEdge);
    SceneInteractionEdit* edit = selected();
    float dy = detailBounds.y + 8.0f - detailScroll;
    auto field = [&](const char* caption, int focusId, std::string& value, float h,
                     const char* placeholder) {
        DrawTextEx(font, caption, {detailBounds.x + 10.0f, dy}, kFontTiny, 1.0f, kTextMuted);
        dy += 14.0f;
        Rectangle r = {detailBounds.x + 10.0f, dy, detailBounds.width - 20.0f, h};
        DrawRectangleRec(r, Color{24, 22, 32, 255});
        DrawRectangleLinesEx(
            r, focusField == focusId ? 2.0f : 1.0f,
            focusField == focusId ? kPanelBorder : kPanelInnerEdge);
        BeginScissorMode((int)r.x + 2, (int)r.y + 2, (int)r.width - 4, (int)r.height - 4);
        DrawTextEx(
            font,
            value.empty() ? placeholder : value.c_str(),
            {r.x + 8.0f, r.y + 6.0f},
            kFontSmall,
            1.0f,
            value.empty() ? kTextMuted : kTextPrimary);
        EndScissorMode();
        if (canClick && !addPickerOpen && CheckCollisionPointRec(mouse, detailBounds)
            && CheckCollisionPointRec(mouse, r))
            focusField = focusId;
        dy += h + 8.0f;
    };

    if (edit != nullptr)
    {
        const float detailContentH = 520.0f;
        const float maxDetailScroll =
            std::max(0.0f, detailContentH - detailBounds.height);
        if (CheckCollisionPointRec(mouse, detailBounds) && !addPickerOpen)
            detailScroll =
                std::clamp(detailScroll - GetMouseWheelMove() * 24.0f, 0.0f, maxDetailScroll);

        BeginScissorMode(
            (int)detailBounds.x,
            (int)detailBounds.y,
            (int)detailBounds.width,
            (int)detailBounds.height);

        field("Id", FocusId, edit->id, 28.0f, "(required)");
        field("Label (Use picker)", FocusLabel, edit->label, 28.0f, "(required)");
        field("Use details (notebook)", FocusUseDetails, edit->useDetails, 70.0f, "(empty)");
        field("grantItem id", FocusGrantItem, edit->grantItemId, 28.0f, "(none — no grant)");
        if (!edit->grantItemId.empty())
        {
            DrawTextEx(
                font,
                ("Catalog name: " + resolveItemName(edit->grantItemId)).c_str(),
                {detailBounds.x + 10.0f, dy},
                kFontTiny,
                1.0f,
                kTextMuted);
            dy += 16.0f;
        }

        auto toggle = [&](const char* label, bool& value) {
            Rectangle btn = {detailBounds.x + 10.0f, dy, 200.0f, 26.0f};
            drawEditorButton(
                font,
                btn,
                (std::string(label) + (value ? ": ON" : ": off")).c_str(),
                value,
                true);
            if (canClick && !addPickerOpen && CheckCollisionPointRec(mouse, detailBounds)
                && CheckCollisionPointRec(mouse, btn))
                value = !value;
            dy += 32.0f;
        };
        toggle("Requires examine", edit->requiresExamine);
        toggle("Repeatable", edit->repeat);

        field("useFlag (set on Use)", FocusUseFlag, edit->useFlag, 28.0f, "(none)");
        field(
            "hideWhenStoryFlag",
            FocusHideFlag,
            edit->hideWhenStoryFlag,
            28.0f,
            "(none)");
        field(
            "requiresAnyInventoryItems (comma)",
            FocusRequiresItems,
            edit->requiresAnyInventoryItemsCsv,
            28.0f,
            "(none)");

        if (!edit->exitSceneId.empty())
        {
            DrawTextEx(
                font,
                ("Map Use exit → " + edit->exitSceneId).c_str(),
                {detailBounds.x + 10.0f, dy},
                kFontTiny,
                1.0f,
                Color{200, 180, 120, 255});
            dy += 18.0f;
        }

        EndScissorMode();
    }
    else
    {
        DrawTextEx(
            font,
            "Select an interaction, or Add place item…",
            {detailBounds.x + 12.0f, detailBounds.y + 12.0f},
            kFontSmall,
            1.0f,
            kTextMuted);
    }

    // Footer
    const float btnY = dialog.y + dialogH - 40.0f;
    Rectangle addBtn = {dialog.x + pad, btnY, 140.0f, 28.0f};
    Rectangle remBtn = {addBtn.x + 148.0f, btnY, 100.0f, 28.0f};
    Rectangle saveBtn = {remBtn.x + 108.0f, btnY, 100.0f, 28.0f};
    Rectangle closeBtn = {saveBtn.x + 108.0f, btnY, 100.0f, 28.0f};
    drawEditorButton(font, addBtn, "Add place item…", true, true);
    drawEditorButton(font, remBtn, "Remove", false, edit != nullptr);
    drawEditorButton(font, saveBtn, "Save", true, true);
    drawEditorButton(font, closeBtn, "Close", false, true);
    if (canClick && !addPickerOpen && CheckCollisionPointRec(mouse, addBtn))
        addPickerOpen = true;
    if (canClick && !addPickerOpen && edit != nullptr && CheckCollisionPointRec(mouse, remBtn))
        removeSelected();
    if (canClick && !addPickerOpen && CheckCollisionPointRec(mouse, saveBtn))
        saveToScene();
    if (canClick && !addPickerOpen && CheckCollisionPointRec(mouse, closeBtn))
        closeDialog();

    const float msgX = closeBtn.x + 110.0f;
    const float msgMaxW = dialog.x + dialog.width - pad - msgX;
    if (!error.empty())
    {
        std::string shown = error;
        while (!shown.empty()
               && MeasureTextEx(font, shown.c_str(), kFontTiny, 1.0f).x > msgMaxW)
            shown.pop_back();
        DrawTextEx(
            font, shown.c_str(), {msgX, btnY + 6.0f}, kFontTiny, 1.0f, Color{220, 90, 80, 255});
    }
    else if (!status.empty())
    {
        std::string shown = status;
        while (!shown.empty()
               && MeasureTextEx(font, shown.c_str(), kFontTiny, 1.0f).x > msgMaxW)
            shown.pop_back();
        DrawTextEx(
            font,
            shown.c_str(),
            {msgX, btnY + 6.0f},
            kFontTiny,
            1.0f,
            Color{120, 180, 120, 255});
    }

    if (addPickerOpen)
    {
        const Rectangle picker = {
            dialog.x + dialogW * 0.5f - 180.0f, dialog.y + 100.0f, 360.0f, 380.0f};
        DrawRectangleRec(picker, Color{24, 22, 32, 255});
        DrawRectangleLinesEx(picker, 2.0f, kPanelBorder);
        DrawTextEx(
            font,
            "Add place item from items.json",
            {picker.x + 12.0f, picker.y + 10.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);
        const Rectangle filter = {picker.x + 12.0f, picker.y + 34.0f, picker.width - 24.0f, 28.0f};
        DrawRectangleRec(filter, Color{18, 16, 24, 255});
        DrawRectangleLinesEx(filter, 1.0f, kPanelBorder);
        DrawTextEx(
            font,
            addFilter.empty() ? "Filter..." : addFilter.c_str(),
            {filter.x + 8.0f, filter.y + 6.0f},
            kFontSmall,
            1.0f,
            addFilter.empty() ? kTextMuted : kTextPrimary);

        std::vector<std::string> ids = docs != nullptr ? docs->itemIds() : std::vector<std::string>{};
        std::vector<std::string> filtered;
        for (const std::string& id : ids)
        {
            if (containsIgnoreCase(id, addFilter)
                || containsIgnoreCase(resolveItemName(id), addFilter))
                filtered.push_back(id);
        }
        const Rectangle opts = {
            picker.x + 12.0f, picker.y + 70.0f, picker.width - 24.0f, picker.height - 120.0f};
        const float optRow = 24.0f;
        const float optMax =
            std::max(0.0f, static_cast<float>(filtered.size()) * optRow - opts.height);
        if (CheckCollisionPointRec(mouse, opts))
            addPickerScroll =
                std::clamp(addPickerScroll - GetMouseWheelMove() * 22.0f, 0.0f, optMax);
        BeginScissorMode(
            (int)opts.x, (int)opts.y, (int)opts.width, (int)opts.height);
        float oy = opts.y + 2.0f - addPickerScroll;
        for (const std::string& id : filtered)
        {
            Rectangle row = {opts.x, oy, opts.width, optRow - 2.0f};
            if (CheckCollisionPointRec(mouse, row))
            {
                DrawRectangleRec(row, kSelection);
                if (canClick)
                    addPlaceItem(id);
            }
            DrawTextEx(
                font,
                (resolveItemName(id) + "  (" + id + ")").c_str(),
                {row.x + 6.0f, row.y + 3.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);
            oy += optRow;
        }
        EndScissorMode();

        Rectangle cancelPick = {
            picker.x + picker.width - 100.0f, picker.y + picker.height - 36.0f, 88.0f, 26.0f};
        drawEditorButton(font, cancelPick, "Cancel", false, true);
        if (canClick && CheckCollisionPointRec(mouse, cancelPick))
            addPickerOpen = false;
    }

    if (canClick && !addPickerOpen && !CheckCollisionPointRec(mouse, dialog))
        closeDialog();
}

} // namespace timberline_editor
