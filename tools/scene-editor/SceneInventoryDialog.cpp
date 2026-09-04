/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneInventoryDialog.h"
#include "EditorInput.h"
#include "EditorButton.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"

#include <algorithm>
#include <cmath>
#include <cctype>

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

} // namespace

std::string SceneInventoryDialog::resolveItemName(const std::string& itemId) const
{
    if (docs == nullptr)
        return itemId;
    const nlohmann::json* item = docs->itemJson(itemId);
    if (item != nullptr && item->is_object())
        return item->value("name", itemId);
    return itemId;
}

std::string SceneInventoryDialog::resolveItemDescription(const std::string& itemId) const
{
    if (docs == nullptr)
        return "";
    const nlohmann::json* item = docs->itemJson(itemId);
    if (item == nullptr || !item->is_object())
        return "";
    if (item->contains("description") && (*item)["description"].is_string())
        return (*item)["description"].get<std::string>();
    if (item->contains("examineText") && (*item)["examineText"].is_string())
        return (*item)["examineText"].get<std::string>();
    return "";
}

std::string SceneInventoryDialog::resolveItemIcon(const std::string& itemId) const
{
    if (docs == nullptr)
        return "";
    const nlohmann::json* item = docs->itemJson(itemId);
    if (item == nullptr || !item->is_object())
        return "";
    if (item->contains("icon") && (*item)["icon"].is_string())
        return (*item)["icon"].get<std::string>();
    if (item->contains("iconPath") && (*item)["iconPath"].is_string())
        return (*item)["iconPath"].get<std::string>();
    // Conventional fallback used by existing takeables.
    return "resources/icons/" + itemId + "_icon.png";
}

void SceneInventoryDialog::loadFromScene()
{
    entries.clear();
    if (docs == nullptr || sceneId.empty())
        return;
    const nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
    if (scene == nullptr || !scene->is_object())
        return;

    if (scene->contains("takeables") && (*scene)["takeables"].is_array()
        && !(*scene)["takeables"].empty())
    {
        for (const nlohmann::json& row : (*scene)["takeables"])
        {
            if (!row.is_object())
                continue;
            SceneInventoryEntry entry;
            entry.id = row.value("id", row.value("defId", ""));
            if (entry.id.empty())
                continue;
            entry.name = row.value("name", resolveItemName(entry.id));
            entry.iconPath = row.value("icon", row.value("iconPath", resolveItemIcon(entry.id)));
            entry.examineText =
                row.value("examineText", resolveItemDescription(entry.id));
            entry.requiresExamine = row.value("requiresExamine", true);
            entry.requiresStoryFlag = row.value("requiresStoryFlag", "");
            entry.quantity = 1;
            entries.push_back(entry);
        }
        return;
    }

    if (scene->contains("inventory") && (*scene)["inventory"].is_array())
    {
        for (const nlohmann::json& row : (*scene)["inventory"])
        {
            if (!row.is_object())
                continue;
            SceneInventoryEntry entry;
            entry.id = row.value("defId", row.value("id", ""));
            if (entry.id.empty())
                continue;
            entry.name = resolveItemName(entry.id);
            entry.iconPath = resolveItemIcon(entry.id);
            entry.examineText = resolveItemDescription(entry.id);
            entry.requiresExamine = true;
            entry.quantity = row.value("quantity", 1);
            entries.push_back(entry);
        }
    }
}

bool SceneInventoryDialog::saveToScene()
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

    nlohmann::json takeables = nlohmann::json::array();
    nlohmann::json inventory = nlohmann::json::array();
    for (const SceneInventoryEntry& entry : entries)
    {
        if (entry.id.empty())
            continue;
        nlohmann::json takeable = nlohmann::json::object();
        takeable["id"] = entry.id;
        if (!entry.name.empty())
            takeable["name"] = entry.name;
        if (!entry.iconPath.empty())
            takeable["icon"] = entry.iconPath;
        if (!entry.examineText.empty())
            takeable["examineText"] = entry.examineText;
        takeable["requiresExamine"] = entry.requiresExamine;
        if (!entry.requiresStoryFlag.empty())
            takeable["requiresStoryFlag"] = entry.requiresStoryFlag;
        takeables.push_back(takeable);

        inventory.push_back({
            {"defId", entry.id},
            {"instanceId", entry.id},
            {"quantity", std::max(1, entry.quantity)}});
    }

    if (takeables.empty())
    {
        scene->erase("takeables");
        scene->erase("inventory");
    }
    else
    {
        (*scene)["takeables"] = takeables;
        (*scene)["inventory"] = inventory;
    }

    docs->markDirty();
    if (!docs->scenes.save())
    {
        error = "Failed to write scenes.json";
        return false;
    }
    docs->dirty = false;
    status = "Saved scene inventory (" + std::to_string(entries.size()) + " item(s)).";
    error.clear();
    if (onSaved)
        onSaved();
    return true;
}

void SceneInventoryDialog::removeAt(size_t index)
{
    if (index >= entries.size())
        return;
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
}

void SceneInventoryDialog::addItemId(const std::string& itemId)
{
    if (itemId.empty())
        return;
    for (const SceneInventoryEntry& existing : entries)
    {
        if (existing.id == itemId)
        {
            error = "Already in this scene: " + itemId;
            return;
        }
    }
    SceneInventoryEntry entry;
    entry.id = itemId;
    entry.name = resolveItemName(itemId);
    entry.iconPath = resolveItemIcon(itemId);
    entry.examineText = resolveItemDescription(itemId);
    entry.requiresExamine = true;
    entry.quantity = 1;
    entries.push_back(entry);
    error.clear();
    status = "Added " + entry.name;
    addPickerOpen = false;
    addFilter.clear();
}

void SceneInventoryDialog::openForScene(const std::string& id)
{
    if (docs == nullptr || id.empty() || !docs->scenes.hasScene(id))
        return;
    if (!docs->itemsLoaded)
        docs->loadItemsDocument();

    sceneId = id;
    loadFromScene();
    status.clear();
    error.clear();
    addPickerOpen = false;
    addFilter.clear();
    addPickerScroll = 0.0f;
    listScroll = 0.0f;
    open = true;
    ignoreInputFrames = 1;
    waitMouseRelease = true;
}

void SceneInventoryDialog::closeDialog()
{
    open = false;
    addPickerOpen = false;
    error.clear();
}

void SceneInventoryDialog::typeIntoFilter()
{
    if (!addPickerOpen)
        return;

    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
            addFilter += clip;
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (cp >= 32 && cp != '\n')
            insertUtf8(addFilter, cp);
        cp = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        backspace(addFilter);
}

void SceneInventoryDialog::handleInput(int screenW, int screenH)
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

    typeIntoFilter();

    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (addPickerOpen)
            addPickerOpen = false;
        else
            closeDialog();
    }
}

void SceneInventoryDialog::draw(int screenW, int screenH)
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

    const float dialogW = std::min(720.0f, screenW - 40.0f);
    const float dialogH = std::min(560.0f, screenH - 40.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        "Scene Inventory",
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
    DrawTextEx(
        font,
        "Items found here (Take UI). Examine-gated by default.",
        {dialog.x + 20.0f, dialog.y + 62.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float pad = 16.0f;
    const float footerH = 56.0f;
    const Rectangle content = {
        dialog.x + pad,
        dialog.y + 84.0f,
        dialog.width - pad * 2.0f,
        dialogH - 84.0f - footerH};
    DrawRectangleRec(content, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(content, 1.0f, kPanelInnerEdge);

    const float rowH = 52.0f;
    const float listTop = content.y + 10.0f;
    const float listH = content.height - 56.0f;
    const Rectangle listBounds = {content.x + 10.0f, listTop, content.width - 20.0f, listH};

    const float contentH = static_cast<float>(entries.size()) * rowH + 8.0f;
    const float maxScroll = std::max(0.0f, contentH - listH);
    if (listScroll > maxScroll)
        listScroll = maxScroll;
    if (CheckCollisionPointRec(mouse, listBounds) && !addPickerOpen)
        listScroll -= GetMouseWheelMove() * 24.0f;
    if (listScroll < 0.0f)
        listScroll = 0.0f;

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
            "(no items in this scene)",
            {listBounds.x + 8.0f, y + 8.0f},
            kFontSmall,
            1.0f,
            kTextMuted);
    }
    else
    {
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const SceneInventoryEntry& entry = entries[i];
            const Rectangle row = {
                listBounds.x, y, listBounds.width, rowH - 4.0f};
            DrawRectangleRec(row, Color{26, 24, 34, 255});
            DrawRectangleLinesEx(row, 1.0f, kPanelInnerEdge);

            const std::string title =
                entry.name.empty() ? entry.id : (entry.name + "  (" + entry.id + ")");
            DrawTextEx(
                font,
                title.c_str(),
                {row.x + 10.0f, row.y + 8.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);

            const std::string meta =
                std::string(entry.requiresExamine ? "Requires examine" : "Take anytime")
                + (entry.requiresStoryFlag.empty()
                       ? ""
                       : (" · flag: " + entry.requiresStoryFlag));
            DrawTextEx(
                font,
                meta.c_str(),
                {row.x + 10.0f, row.y + 28.0f},
                kFontTiny,
                1.0f,
                kTextMuted);

            const Rectangle examBtn = {
                row.x + row.width - 168.0f, row.y + 12.0f, 78.0f, 26.0f};
            const Rectangle removeBtn = {
                row.x + row.width - 82.0f, row.y + 12.0f, 70.0f, 26.0f};
            drawEditorButton(
                font,
                examBtn,
                entry.requiresExamine ? "Exam: ON" : "Exam: off",
                entry.requiresExamine,
                !addPickerOpen);
            drawEditorButton(font, removeBtn, "Remove", false, !addPickerOpen);

            if (canClick && !addPickerOpen)
            {
                if (CheckCollisionPointRec(mouse, examBtn))
                    entries[i].requiresExamine = !entries[i].requiresExamine;
                else if (CheckCollisionPointRec(mouse, removeBtn))
                    removeAt(i);
            }

            y += rowH;
        }
    }
    EndScissorMode();

    const Rectangle addBtn = {
        content.x + 10.0f, content.y + content.height - 40.0f, 120.0f, 30.0f};
    // ASCII "..." — UI fonts often lack U+2026 and draw it as '?'.
    drawEditorButton(font, addBtn, "Add item...", true, true);
    if (canClick && CheckCollisionPointRec(mouse, addBtn))
    {
        addPickerOpen = !addPickerOpen;
        addPickerScroll = 0.0f;
    }

    // Footer
    const float btnW = 120.0f;
    const float btnH = 34.0f;
    const float btnY = dialog.y + dialogH - btnH - 12.0f;
    const Rectangle saveBtn = {dialog.x + pad, btnY, btnW, btnH};
    const Rectangle closeBtn = {
        dialog.x + dialogW - btnW - pad, btnY, btnW, btnH};
    drawEditorButton(font, saveBtn, "Save", true, true);
    drawEditorButton(font, closeBtn, "Close", false, true);
    if (canClick && !addPickerOpen)
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

    if (addPickerOpen)
    {
        const Rectangle picker = {
            dialog.x + dialogW * 0.5f - 180.0f,
            dialog.y + 110.0f,
            360.0f,
            360.0f};
        DrawRectangleRec(picker, Color{24, 22, 32, 255});
        DrawRectangleLinesEx(picker, 2.0f, kPanelBorder);
        DrawTextEx(
            font,
            "Add from items.json",
            {picker.x + 12.0f, picker.y + 10.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);

        const Rectangle filter = {picker.x + 12.0f, picker.y + 34.0f, picker.width - 24.0f, 28.0f};
        DrawRectangleRec(filter, Color{18, 16, 24, 255});
        DrawRectangleLinesEx(filter, 1.0f, kPanelBorder);
        DrawTextEx(
            font,
            addFilter.empty() ? "Filter…" : addFilter.c_str(),
            {filter.x + 8.0f, filter.y + 6.0f},
            kFontSmall,
            1.0f,
            addFilter.empty() ? kTextMuted : kTextPrimary);

        std::vector<std::string> ids;
        if (docs != nullptr)
            ids = docs->itemIds();
        std::vector<std::string> filtered;
        filtered.reserve(ids.size());
        for (const std::string& id : ids)
        {
            if (containsIgnoreCase(id, addFilter)
                || containsIgnoreCase(resolveItemName(id), addFilter))
                filtered.push_back(id);
        }

        const Rectangle opts = {
            picker.x + 12.0f, picker.y + 70.0f, picker.width - 24.0f, picker.height - 118.0f};
        const float optRow = 24.0f;
        const float optContentH = static_cast<float>(filtered.size()) * optRow;
        const float optMaxScroll = std::max(0.0f, optContentH - opts.height);
        if (CheckCollisionPointRec(mouse, opts))
            addPickerScroll -= GetMouseWheelMove() * 22.0f;
        if (addPickerScroll < 0.0f)
            addPickerScroll = 0.0f;
        if (addPickerScroll > optMaxScroll)
            addPickerScroll = optMaxScroll;

        BeginScissorMode(
            static_cast<int>(opts.x),
            static_cast<int>(opts.y),
            static_cast<int>(opts.width),
            static_cast<int>(opts.height));
        float oy = opts.y + 2.0f - addPickerScroll;
        for (const std::string& id : filtered)
        {
            const Rectangle row = {opts.x, oy, opts.width, optRow - 2.0f};
            const bool hover = CheckCollisionPointRec(mouse, row);
            if (hover)
                DrawRectangleRec(row, kSelection);
            const std::string label = resolveItemName(id) + "  (" + id + ")";
            DrawTextEx(
                font,
                label.c_str(),
                {row.x + 6.0f, row.y + 4.0f},
                kFontTiny,
                1.0f,
                kTextPrimary);
            if (canClick && hover)
                addItemId(id);
            oy += optRow;
        }
        EndScissorMode();

        const Rectangle cancelPick = {
            picker.x + picker.width - 100.0f, picker.y + picker.height - 38.0f, 88.0f, 28.0f};
        drawEditorButton(font, cancelPick, "Cancel", false, true);
        if (canClick && CheckCollisionPointRec(mouse, cancelPick))
            addPickerOpen = false;
    }
}

} // namespace timberline_editor
