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
            entry.requiresInventoryItem = row.value("requiresInventoryItem", "");
            entry.requiresHeldItem = !entry.requiresInventoryItem.empty()
                || row.value("requiresHeldItem", false);
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
        if (entry.requiresHeldItem && !entry.requiresInventoryItem.empty())
            takeable["requiresInventoryItem"] = entry.requiresInventoryItem;
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
    entry.requiresHeldItem = false;
    entry.requiresInventoryItem.clear();
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
    mustHaveFocusRow = -1;
    mustHaveSuggestOpen = false;
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

void SceneInventoryDialog::typeIntoMustHaveField()
{
    if (addPickerOpen || mustHaveFocusRow < 0
        || mustHaveFocusRow >= static_cast<int>(entries.size()))
        return;
    std::string& target =
        entries[static_cast<size_t>(mustHaveFocusRow)].requiresInventoryItem;

    if (mustHaveSuggestOpen && IsKeyPressed(KEY_TAB))
    {
        const std::vector<std::string> suggestions = mustHaveSuggestions(8);
        if (!suggestions.empty())
            applyMustHaveSuggestion(suggestions.front());
        return;
    }

    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            target += clip;
            mustHaveSuggestOpen = true;
        }
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (cp >= 32 && cp != '\n' && cp != '\r')
        {
            insertUtf8(target, cp);
            mustHaveSuggestOpen = true;
        }
        cp = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
    {
        backspace(target);
        mustHaveSuggestOpen = true;
    }
}

std::vector<std::string> SceneInventoryDialog::mustHaveSuggestions(int maxCount) const
{
    std::vector<std::string> out;
    if (docs == nullptr || maxCount <= 0 || mustHaveFocusRow < 0
        || mustHaveFocusRow >= static_cast<int>(entries.size()))
        return out;
    if (!docs->itemsLoaded)
        const_cast<DocumentWorkspace*>(docs)->loadItemsDocument();
    const std::string prefix =
        entries[static_cast<size_t>(mustHaveFocusRow)].requiresInventoryItem;
    const std::string prefixLower = toLowerCopy(prefix);
    for (const std::string& id : docs->itemIds())
    {
        const std::string idLower = toLowerCopy(id);
        const std::string nameLower = toLowerCopy(resolveItemName(id));
        if (!prefixLower.empty()
            && idLower.find(prefixLower) == std::string::npos
            && nameLower.find(prefixLower) == std::string::npos)
            continue;
        out.push_back(id);
        if (static_cast<int>(out.size()) >= maxCount * 3)
            break;
    }
    std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
        const std::string al = toLowerCopy(a);
        const std::string bl = toLowerCopy(b);
        const bool ap = !prefixLower.empty() && al.rfind(prefixLower, 0) == 0;
        const bool bp = !prefixLower.empty() && bl.rfind(prefixLower, 0) == 0;
        if (ap != bp)
            return ap;
        return al < bl;
    });
    if (static_cast<int>(out.size()) > maxCount)
        out.resize(static_cast<size_t>(maxCount));
    return out;
}

void SceneInventoryDialog::applyMustHaveSuggestion(const std::string& itemId)
{
    if (mustHaveFocusRow < 0
        || mustHaveFocusRow >= static_cast<int>(entries.size())
        || itemId.empty())
        return;
    entries[static_cast<size_t>(mustHaveFocusRow)].requiresInventoryItem = itemId;
    entries[static_cast<size_t>(mustHaveFocusRow)].requiresHeldItem = true;
    mustHaveSuggestOpen = false;
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

    if (addPickerOpen)
        typeIntoFilter();
    else
        typeIntoMustHaveField();

    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (addPickerOpen)
            addPickerOpen = false;
        else if (mustHaveSuggestOpen)
            mustHaveSuggestOpen = false;
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

    // Title, description, examine slider, must-have slider+field.
    const float rowH = 108.0f;
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

            // Bold red X — top-right remove (#50).
            const float xSize = 22.0f;
            const Rectangle removeHit = {
                row.x + row.width - xSize - 6.0f, row.y + 6.0f, xSize, xSize};
            const bool removeHover = CheckCollisionPointRec(mouse, removeHit);
            DrawTextEx(
                bold,
                "X",
                {removeHit.x + 4.0f, removeHit.y - 1.0f},
                kFontTitle,
                1.0f,
                removeHover ? Color{255, 90, 80, 255} : Color{220, 60, 50, 255});

            const float titleMaxW = std::max(40.0f, removeHit.x - (row.x + 10.0f) - 8.0f);
            std::string title =
                entry.name.empty() ? entry.id : (entry.name + "  (" + entry.id + ")");
            while (!title.empty()
                   && MeasureTextEx(font, (title + "...").c_str(), kFontSmall, 1.0f).x
                       > titleMaxW)
                title.pop_back();
            if (MeasureTextEx(font, title.c_str(), kFontSmall, 1.0f).x > titleMaxW)
                title += "...";
            DrawTextEx(
                font,
                title.c_str(),
                {row.x + 10.0f, row.y + 6.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);

            // Description under the name (examine text or flag meta).
            std::string desc = entry.examineText;
            if (desc.empty() && !entry.requiresStoryFlag.empty())
                desc = "flag: " + entry.requiresStoryFlag;
            if (desc.empty())
                desc = entry.id;
            while (!desc.empty()
                   && MeasureTextEx(font, (desc + "...").c_str(), kFontTiny, 1.0f).x
                       > row.width - 20.0f)
                desc.pop_back();
            if (MeasureTextEx(font, desc.c_str(), kFontTiny, 1.0f).x > row.width - 20.0f)
                desc += "...";
            DrawTextEx(
                font,
                desc.c_str(),
                {row.x + 10.0f, row.y + 26.0f},
                kFontTiny,
                1.0f,
                kTextMuted);

            // Exit-Requirements-style row sliders under the description.
            const float sw = 44.0f;
            const float sh = 22.0f;
            const float ksz = 16.0f;
            auto drawSlider = [&](float rowY, bool on) {
                const Rectangle track = {row.x + 10.0f, rowY, sw, sh};
                DrawRectangleRounded(track, 0.5f, 6, Color{28, 26, 36, 255});
                DrawRectangleRoundedLines(track, 0.5f, 6, kPanelInnerEdge);
                const float kx =
                    on ? (track.x + track.width - ksz - 3.0f) : (track.x + 3.0f);
                DrawRectangleRounded(
                    {kx, track.y + (sh - ksz) * 0.5f, ksz, ksz},
                    0.5f,
                    6,
                    on ? kPanelAccent : Color{70, 66, 80, 255});
                return track;
            };

            const Rectangle examTrack = drawSlider(row.y + 46.0f, entry.requiresExamine);
            DrawTextEx(
                font,
                "Must examine scene first",
                {examTrack.x + sw + 10.0f, examTrack.y + 3.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);
            const Rectangle examHit = {
                examTrack.x, examTrack.y, sw + 10.0f + 200.0f, sh};

            // Must-have item gate: slider | autocomplete field | "Must have..."
            const Rectangle haveTrack =
                drawSlider(row.y + 74.0f, entry.requiresHeldItem);
            const float fieldX = haveTrack.x + sw + 10.0f;
            const float labelW =
                MeasureTextEx(font, "Must have...", kFontSmall, 1.0f).x;
            const float fieldW = std::max(
                80.0f, row.x + row.width - 12.0f - fieldX - labelW - 12.0f);
            const Rectangle haveField = {fieldX, haveTrack.y - 2.0f, fieldW, 26.0f};
            const bool haveFocused = mustHaveFocusRow == static_cast<int>(i);
            DrawRectangleRec(haveField, Color{24, 22, 32, 255});
            DrawRectangleLinesEx(
                haveField,
                haveFocused ? 2.0f : 1.0f,
                haveFocused ? kPanelBorder : kPanelInnerEdge);
            const std::string haveText = entry.requiresInventoryItem;
            BeginScissorMode(
                (int)haveField.x + 2,
                (int)haveField.y + 2,
                (int)haveField.width - 4,
                (int)haveField.height - 4);
            DrawTextEx(
                font,
                haveText.empty() ? "(item id)" : haveText.c_str(),
                {haveField.x + 6.0f, haveField.y + 5.0f},
                kFontSmall,
                1.0f,
                haveText.empty() ? kTextMuted : kTextPrimary);
            EndScissorMode();
            // Restore list scissor after nested End.
            BeginScissorMode(
                (int)listBounds.x,
                (int)listBounds.y,
                (int)listBounds.width,
                (int)listBounds.height);
            DrawTextEx(
                font,
                "Must have...",
                {haveField.x + haveField.width + 8.0f, haveTrack.y + 3.0f},
                kFontSmall,
                1.0f,
                kTextPrimary);
            const Rectangle haveSliderHit = {haveTrack.x, haveTrack.y, sw, sh};

            if (haveFocused)
            {
                mustHaveFieldRect = haveField;
            }

            if (canClick && !addPickerOpen)
            {
                if (CheckCollisionPointRec(mouse, removeHit))
                {
                    removeAt(i);
                    mustHaveFocusRow = -1;
                    mustHaveSuggestOpen = false;
                }
                else if (CheckCollisionPointRec(mouse, examHit))
                    entries[i].requiresExamine = !entries[i].requiresExamine;
                else if (CheckCollisionPointRec(mouse, haveSliderHit))
                {
                    entries[i].requiresHeldItem = !entries[i].requiresHeldItem;
                    if (entries[i].requiresHeldItem)
                    {
                        mustHaveFocusRow = static_cast<int>(i);
                        mustHaveSuggestOpen = true;
                        if (docs != nullptr && !docs->itemsLoaded)
                            docs->loadItemsDocument();
                    }
                    else
                    {
                        entries[i].requiresInventoryItem.clear();
                        if (mustHaveFocusRow == static_cast<int>(i))
                        {
                            mustHaveFocusRow = -1;
                            mustHaveSuggestOpen = false;
                        }
                    }
                }
                else if (CheckCollisionPointRec(mouse, haveField))
                {
                    mustHaveFocusRow = static_cast<int>(i);
                    mustHaveSuggestOpen = true;
                    entries[i].requiresHeldItem = true;
                    if (docs != nullptr && !docs->itemsLoaded)
                        docs->loadItemsDocument();
                }
            }

            y += rowH;
        }
    }
    EndScissorMode();

    // Must-have autocomplete dropdown (above list scissor).
    if (mustHaveSuggestOpen && mustHaveFocusRow >= 0
        && mustHaveFocusRow < static_cast<int>(entries.size())
        && entries[static_cast<size_t>(mustHaveFocusRow)].requiresHeldItem)
    {
        const std::vector<std::string> suggestions = mustHaveSuggestions(8);
        if (!suggestions.empty())
        {
            const float sugRowH = 22.0f;
            mustHaveSuggestRect = {
                mustHaveFieldRect.x,
                mustHaveFieldRect.y + mustHaveFieldRect.height + 2.0f,
                mustHaveFieldRect.width,
                sugRowH * static_cast<float>(suggestions.size()) + 6.0f};
            if (mustHaveSuggestRect.y + mustHaveSuggestRect.height
                > dialog.y + dialogH - 56.0f)
                mustHaveSuggestRect.y =
                    mustHaveFieldRect.y - mustHaveSuggestRect.height - 2.0f;
            DrawRectangleRec(mustHaveSuggestRect, Color{36, 32, 44, 255});
            DrawRectangleLinesEx(mustHaveSuggestRect, 1.0f, kPanelBorder);
            float sy = mustHaveSuggestRect.y + 3.0f;
            for (const std::string& id : suggestions)
            {
                Rectangle srow = {
                    mustHaveSuggestRect.x + 2.0f,
                    sy,
                    mustHaveSuggestRect.width - 4.0f,
                    sugRowH - 2.0f};
                if (CheckCollisionPointRec(mouse, srow))
                {
                    DrawRectangleRec(srow, Color{60, 54, 72, 220});
                    if (canClick)
                        applyMustHaveSuggestion(id);
                }
                std::string label = id;
                const std::string name = resolveItemName(id);
                if (!name.empty() && name != id)
                    label = name + "  (" + id + ")";
                while (!label.empty()
                       && MeasureTextEx(font, (label + "...").c_str(), kFontSmall, 1.0f).x
                           > srow.width - 12.0f)
                    label.pop_back();
                DrawTextEx(
                    font,
                    label.c_str(),
                    {srow.x + 6.0f, srow.y + 2.0f},
                    kFontSmall,
                    1.0f,
                    kTextPrimary);
                sy += sugRowH;
            }
        }
        if (canClick && !CheckCollisionPointRec(mouse, mustHaveFieldRect)
            && !CheckCollisionPointRec(mouse, mustHaveSuggestRect))
            mustHaveSuggestOpen = false;
    }

    // Bold white + bottom-left instead of "Add item..." (#50).
    const Rectangle addBtn = {
        content.x + 10.0f, content.y + content.height - 40.0f, 36.0f, 32.0f};
    const bool addHover = CheckCollisionPointRec(mouse, addBtn);
    // No box border — just the bold + glyph (#50 follow-up).
    DrawTextEx(
        bold,
        "+",
        {addBtn.x + 9.0f, addBtn.y + 1.0f},
        kFontHeading + 4.0f,
        1.0f,
        addHover ? Color{255, 255, 255, 255} : Color{230, 230, 235, 255});
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
        {
            mustHaveFocusRow = -1;
            mustHaveSuggestOpen = false;
            saveToScene();
        }
        else if (CheckCollisionPointRec(mouse, closeBtn))
            closeDialog();
        else if (!CheckCollisionPointRec(mouse, dialog) && !mustHaveSuggestOpen)
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
            addFilter.empty() ? "Filter..." : addFilter.c_str(),
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
