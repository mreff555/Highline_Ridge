/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#include "ItemEditor.h"

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

const float kNewItemHeaderBtnH = 22.0f;
const float kNewItemHeaderBtnW = 78.0f;
const float kEditItemHeaderBtnW = 78.0f;
const float kIdBadgeFont = 12.0f;
const float kIdBadgeHeight = 22.0f;
const float kAuthoringFooterH = 58.0f;
const float kAuthoringHeaderH = 56.0f;

void appendUtf8Codepoint(std::string& buffer, int codepoint)
{
    if (codepoint <= 0)
        return;
    if (codepoint < 0x80)
    {
        buffer.push_back(static_cast<char>(codepoint));
        return;
    }
    // Keep wizard simple: accept ASCII printable only for ids/weights;
    // description/name may include limited UTF-8 via multi-byte if raylib gives full codepoints.
    char bytes[5] = {};
    int size = 0;
    if (codepoint <= 0x7FF)
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
    else if (codepoint <= 0x10FFFF)
    {
        bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    }
    for (int i = 0; i < size; ++i)
        buffer.push_back(bytes[i]);
}

void drawEditorButton(Font font, Rectangle bounds, const char* label, bool accent, bool enabled)
{
    const Color fill = !enabled
        ? kButtonDisabled
        : (accent ? kPanelAccent : Color{44, 42, 52, 255});
    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, 1.0f, kPanelBorder);
    const Vector2 size = MeasureTextEx(font, label, kFontBody, 1.0f);
    DrawTextEx(
        font,
        label,
        {bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + (bounds.height - size.y) * 0.5f},
        kFontBody,
        1.0f,
        enabled ? kTextPrimary : kTextDisabled);
}

void drawCheckboxRow(
    Font font,
    Rectangle row,
    const char* label,
    bool checked,
    bool hovered)
{
    const float box = 16.0f;
    const Rectangle boxRec = {
        row.x,
        row.y + (row.height - box) * 0.5f,
        box,
        box};
    DrawRectangleRec(boxRec, hovered ? Color{50, 46, 58, 255} : Color{36, 34, 44, 255});
    DrawRectangleLinesEx(boxRec, 1.0f, kPanelBorder);
    if (checked)
    {
        DrawRectangle(
            static_cast<int>(boxRec.x + 3),
            static_cast<int>(boxRec.y + 3),
            static_cast<int>(box - 6),
            static_cast<int>(box - 6),
            kPanelBorder);
    }
    DrawTextEx(
        font,
        label,
        {boxRec.x + box + 8.0f, row.y + (row.height - kFontSmall) * 0.5f},
        kFontSmall,
        1.0f,
        kTextPrimary);
}

} // namespace

namespace
{

ConversationTreeNode makeItemFieldNode(
    const std::string& itemId,
    const std::string& label,
    const std::string& pointerUnderItem,
    const nlohmann::json& value)
{
    ConversationTreeNode node;
    node.kind = ConversationNodeKind::Narrative;
    node.key = "item:" + itemId + ":" + pointerUnderItem;
    node.label = label;
    node.editDoc = ConversationEditDoc::Items;
    node.editSceneId = itemId; // stores item id for Items edit target
    node.jsonPointer = pointerUnderItem;
    if (value.is_string())
    {
        const std::string text = value.get<std::string>();
        node.detail = text.empty() ? "(empty)" : ItemEditor::truncateForTree(text, 40);
    }
    else if (value.is_boolean())
        node.detail = value.get<bool>() ? "true" : "false";
    else if (value.is_number_integer())
        node.detail = std::to_string(value.get<long long>());
    else if (value.is_number_float())
    {
        std::ostringstream stream;
        stream << value.get<double>();
        node.detail = stream.str();
    }
    else if (value.is_null())
        node.detail = "(null)";
    else if (value.is_object())
        node.detail = "{…}";
    else if (value.is_array())
        node.detail = "[…]";
    else
        node.detail = "…";
    return node;
}

void appendFieldIfPresent(
    ConversationTreeNode& parent,
    const std::string& itemId,
    const nlohmann::json& item,
    const char* key,
    const char* label)
{
    if (!item.contains(key))
        return;
    parent.children.push_back(makeItemFieldNode(itemId, label, std::string("/") + key, item[key]));
}

void appendFieldAlways(
    ConversationTreeNode& parent,
    const std::string& itemId,
    const nlohmann::json& item,
    const char* key,
    const char* label)
{
    if (item.contains(key))
        parent.children.push_back(makeItemFieldNode(itemId, label, std::string("/") + key, item[key]));
    else
        parent.children.push_back(makeItemFieldNode(itemId, label, std::string("/") + key, nlohmann::json()));
}

} // namespace

void ItemEditor::invalidateVisibleRows()
{
    visibleRowsDirty = true;
}

bool ItemEditor::isExpanded(const std::string& key) const
{
    return expanded.count(key) > 0;
}

void ItemEditor::toggleExpanded(const std::string& key)
{
    if (expanded.count(key) > 0)
        expanded.erase(key);
    else
        expanded.insert(key);
    invalidateVisibleRows();
}

bool ItemEditor::allRootsExpanded() const
{
    if (roots.empty())
        return false;
    for (const ConversationTreeNode& root : roots)
    {
        if (root.children.empty())
            continue;
        if (!isExpanded(root.key))
            return false;
    }
    return true;
}

void ItemEditor::expandAllRoots()
{
    for (const ConversationTreeNode& root : roots)
    {
        if (!root.children.empty())
            expanded.insert(root.key);
    }
    invalidateVisibleRows();
}

void ItemEditor::collapseAllRoots()
{
    for (const ConversationTreeNode& root : roots)
        expanded.erase(root.key);
    invalidateVisibleRows();
}

void ItemEditor::toggleExpandAllRoots()
{
    if (allRootsExpanded())
        collapseAllRoots();
    else
        expandAllRoots();
}

void ItemEditor::rebuildTree()
{
    roots.clear();
    invalidateVisibleRows();
    if (docs == nullptr || !docs->itemsLoaded)
        return;

    const std::vector<std::string> ids = docs->itemIds();
    for (const std::string& itemId : ids)
    {
        const nlohmann::json* item = docs->itemJson(itemId);
        if (item == nullptr)
            continue;

        ConversationTreeNode root;
        root.kind = ConversationNodeKind::Section;
        root.key = "item-root:" + itemId;
        root.label = itemId;
        root.editDoc = ConversationEditDoc::None;
        root.editSceneId = itemId;
        const std::string name = item->value("name", "");
        root.detail = name.empty() ? "(unnamed)" : name;
        if (item->value("ttsEnabled", false))
        {
            const std::string voice = item->value("ttsDefaultVoice", "");
            root.detail += voice.empty() ? "  [TTS]" : ("  [TTS:" + voice + "]");
        }

        // Core dialog / identity fields
        appendFieldAlways(root, itemId, *item, "name", "name");
        appendFieldAlways(root, itemId, *item, "description", "description");
        appendFieldIfPresent(root, itemId, *item, "alternateDescription", "alternateDescription");
        appendFieldIfPresent(root, itemId, *item, "useNarrative", "useNarrative");

        // TTS bags (always offer so authors can add them)
        appendFieldAlways(root, itemId, *item, "examineTts", "examineTts");
        appendFieldAlways(root, itemId, *item, "useTts", "useTts");
        appendFieldAlways(root, itemId, *item, "takeTts", "takeTts");
        appendFieldAlways(root, itemId, *item, "assembleNarrative", "assembleNarrative");
        appendFieldAlways(root, itemId, *item, "assembleTts", "assembleTts");

        // Craft / quantity / consume
        appendFieldAlways(root, itemId, *item, "components", "components");
        appendFieldIfPresent(root, itemId, *item, "consumeOnCombine", "consumeOnCombine");
        appendFieldIfPresent(root, itemId, *item, "consumeOnUse", "consumeOnUse");
        appendFieldIfPresent(root, itemId, *item, "quantity", "quantity");

        // Common non-dialog fields (edit as JSON / scalar)
        appendFieldIfPresent(root, itemId, *item, "weightLb", "weightLb");
        appendFieldIfPresent(root, itemId, *item, "lightSource", "lightSource");
        appendFieldIfPresent(root, itemId, *item, "visuals", "visuals");
        appendFieldIfPresent(root, itemId, *item, "icons", "icons");
        appendFieldIfPresent(root, itemId, *item, "container", "container");
        appendFieldIfPresent(root, itemId, *item, "sfx", "sfx");
        appendFieldIfPresent(root, itemId, *item, "examineAudio", "examineAudio");
        appendFieldIfPresent(root, itemId, *item, "examineRevealFlag", "examineRevealFlag");
        appendFieldIfPresent(root, itemId, *item, "useRequiresFlag", "useRequiresFlag");
        appendFieldIfPresent(root, itemId, *item, "useRevealFlag", "useRevealFlag");
        appendFieldIfPresent(root, itemId, *item, "ttsEnabled", "ttsEnabled");
        appendFieldIfPresent(root, itemId, *item, "ttsDefaultVoice", "ttsDefaultVoice");

        roots.push_back(std::move(root));
    }
}

void ItemEditor::collectVisibleRows(
    const ConversationTreeNode& node,
    int depth,
    bool isLastChild,
    std::vector<bool> ancestorContinues,
    std::vector<ConversationVisibleRow>& out) const
{
    ConversationVisibleRow row;
    row.node = &node;
    row.depth = depth;
    row.isLastChild = isLastChild;
    row.ancestorContinues = ancestorContinues;
    out.push_back(row);

    if (node.children.empty() || !isExpanded(node.key))
        return;

    ancestorContinues.push_back(!isLastChild);
    for (size_t i = 0; i < node.children.size(); ++i)
    {
        collectVisibleRows(
            node.children[i],
            depth + 1,
            i + 1 == node.children.size(),
            ancestorContinues,
            out);
    }
}

const std::vector<ConversationVisibleRow>& ItemEditor::visibleRows() const
{
    if (!visibleRowsDirty)
        return visibleRowsCache;

    visibleRowsCache.clear();
    for (size_t i = 0; i < roots.size(); ++i)
    {
        collectVisibleRows(
            roots[i],
            0,
            i + 1 == roots.size(),
            {},
            visibleRowsCache);
    }
    visibleRowsDirty = false;
    return visibleRowsCache;
}

void ItemEditor::openNodeEditor(const ConversationTreeNode& node)
{
    if (text == nullptr || docs == nullptr)
        return;
    if (node.editDoc != ConversationEditDoc::Items || node.jsonPointer.empty())
        return;

    const std::string& itemId = node.editSceneId;
    nlohmann::json* item = docs->itemJson(itemId);
    if (item == nullptr)
        return;

    // Ensure missing leaf keys exist so json_pointer resolve works.
    if (node.jsonPointer.size() >= 2 && node.jsonPointer[0] == '/')
    {
        const std::string leaf = node.jsonPointer.substr(1);
        if (leaf.find('/') == std::string::npos && !item->contains(leaf))
        {
            // TTS bags default to objects; scalars to empty string / false.
            if (leaf.size() >= 3 && leaf.compare(leaf.size() - 3, 3, "Tts") == 0)
                (*item)[leaf] = nlohmann::json::object();
            else if (leaf == "components")
                (*item)[leaf] = nlohmann::json::array();
            else if (leaf == "lightSource" || leaf == "ttsEnabled")
                (*item)[leaf] = false;
            else if (leaf == "consumeOnCombine" || leaf == "consumeOnUse")
                (*item)[leaf] = true;
            else if (leaf == "weightLb")
                (*item)[leaf] = 0.0;
            else
                (*item)[leaf] = "";
            docs->markDirty();
        }
    }

    const nlohmann::json* value = docs->itemFieldAt(itemId, node.jsonPointer);
    if (value == nullptr)
    {
        TraceLog(LOG_WARNING, "TIMBERLINE: item edit path missing %s %s",
                 itemId.c_str(), node.jsonPointer.c_str());
        return;
    }

    text->docTarget = ConversationEditDoc::Items;
    text->jsonPointer = node.jsonPointer;
    text->editorSceneId.clear();
    text->editorItemId = itemId;
    text->editorKey = node.label;
    text->scrollY = 0.0f;
    text->error.clear();
    selectedKey = node.key;
    selectedItemId = itemId;

    if (value->is_string())
    {
        text->kind = VariableEditor::VariableKindString;
        text->buffer = value->get<std::string>();
        text->multiline = true;
    }
    else if (value->is_boolean())
    {
        text->kind = VariableEditor::VariableKindBool;
        text->buffer = value->get<bool>() ? "true" : "false";
        text->multiline = false;
    }
    else if (value->is_number_integer())
    {
        text->kind = VariableEditor::VariableKindInteger;
        text->buffer = std::to_string(value->get<long long>());
        text->multiline = false;
    }
    else if (value->is_number_float())
    {
        text->kind = VariableEditor::VariableKindFloat;
        std::ostringstream stream;
        stream << value->get<double>();
        text->buffer = stream.str();
        text->multiline = false;
    }
    else if (value->is_null())
    {
        text->kind = VariableEditor::VariableKindString;
        text->buffer.clear();
        text->multiline = true;
    }
    else
    {
        text->kind = VariableEditor::VariableKindJson;
        text->buffer = value->dump(2);
        text->multiline = true;
    }

    text->ensureGlobalDefaultVoiceLoaded();
    text->ensureTtsSyntaxThemeLoaded();
    text->setupTextTtsForOpenedValue(*value);

    if (!text->textTtsEnabled)
    {
        text->cursor = static_cast<int>(text->buffer.size());
        text->selectAnchor = -1;
        text->mouseSelecting = false;
    }
    text->open = true;
    text->ignoreInputFrames = 1;
}


void ItemEditor::handleInput(Rectangle listBounds)
{
    if (docs == nullptr || text == nullptr)
        return;
    if ((stackDialogOpen != nullptr && *stackDialogOpen) || text->open || authoringDialogOpen)
        return;
    if (draggingDivider && draggingDivider())
        return;

    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, listBounds))
        return;

    listScroll -= GetMouseWheelMove() * kTreeRowHeight * 2.0f;
    if (listScroll < 0.0f)
        listScroll = 0.0f;

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;

    // Header expand/collapse-all control (same style as row toggles).
    const float headerToggleX = listBounds.x + 8.0f;
    const float headerToggleY =
        listBounds.y + (20.0f - kTreeToggleSize) * 0.5f;
    const Rectangle headerToggle = {
        headerToggleX,
        headerToggleY,
        kTreeToggleSize,
        kTreeToggleSize};
    if (CheckCollisionPointRec(mouse, headerToggle))
    {
        toggleExpandAllRoots();
        return;
    }

    const Rectangle newBtn = newItemBtnBounds(listBounds);
    if (docs->itemsLoaded && CheckCollisionPointRec(mouse, newBtn))
    {
        openNewItemDialog();
        return;
    }

    const Rectangle editBtn = editItemBtnBounds(listBounds);
    if (docs->itemsLoaded
        && !selectedItemId.empty()
        && CheckCollisionPointRec(mouse, editBtn))
    {
        openModifyItemDialog(selectedItemId);
        return;
    }

    const std::vector<ConversationVisibleRow>& rows = visibleRows();
    const Rectangle treeBounds = {
        listBounds.x,
        listBounds.y + 20.0f,
        listBounds.width,
        listBounds.height - 20.0f};

    float y = treeBounds.y + 4.0f - listScroll;
    for (const ConversationVisibleRow& row : rows)
    {
        if (row.node == nullptr)
            continue;
        const ConversationTreeNode& node = *row.node;
        const Rectangle rowBounds = {
            treeBounds.x + 2.0f,
            y,
            treeBounds.width - 4.0f,
            kTreeRowHeight - 1.0f};

        if (CheckCollisionPointRec(mouse, treeBounds) && CheckCollisionPointRec(mouse, rowBounds))
        {
            const bool wasSelected = (node.key == selectedKey);
            selectedKey = node.key;
            selectedItemId = node.editSceneId;

            const float baseX = treeBounds.x + 8.0f;
            const float toggleX = baseX + static_cast<float>(row.depth) * kTreeIndent;
            const Rectangle toggle = {
                toggleX,
                y + (kTreeRowHeight - kTreeToggleSize) * 0.5f,
                kTreeToggleSize,
                kTreeToggleSize};

            if (!node.children.empty() && CheckCollisionPointRec(mouse, toggle))
            {
                toggleExpanded(node.key);
                return;
            }

            if (node.editDoc == ConversationEditDoc::Items)
                openNodeEditor(node);
            else if (node.kind == ConversationNodeKind::Section
                     && node.editDoc == ConversationEditDoc::None
                     && wasSelected
                     && !node.editSceneId.empty())
            {
                // Second click on already-selected root opens Modify Item.
                openModifyItemDialog(node.editSceneId);
            }
            else if (!node.children.empty())
                toggleExpanded(node.key);
            return;
        }
        y += kTreeRowHeight;
    }
}

void ItemEditor::draw(Rectangle listBounds)
{
    if (docs == nullptr)
        return;

    if (!docs->itemsLoaded)
    {
        const std::string message = docs->loadError.empty() ? "Loading items..." : docs->loadError;
        drawWrappedText(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            message,
            {listBounds.x + 12.0f, listBounds.y + 12.0f},
            listBounds.width - 24.0f,
            kListMetaFont,
            4.0f,
            kTextMuted);
        return;
    }

    if (roots.empty())
    {
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            "No items in items.json",
            {listBounds.x + 12.0f, listBounds.y + 12.0f},
            kFontBody,
            1.0f,
            kTextMuted);
        return;
    }

    const std::vector<ConversationVisibleRow>& rows = visibleRows();
    const float contentHeight = static_cast<float>(rows.size()) * kTreeRowHeight + 8.0f;
    const float maxScroll = std::max(0.0f, contentHeight - (listBounds.height - 20.0f));
    if (listScroll > maxScroll)
        listScroll = maxScroll;

    const Vector2 mouse = GetMousePosition();
    const bool canInteract =
        !(stackDialogOpen != nullptr && *stackDialogOpen) &&
        (text == nullptr || !text->open) &&
        !authoringDialogOpen &&
        !(draggingDivider && draggingDivider());

    // Header: [+/-] Items (N)  — toggle expands or collapses every item.
    const float headerToggleX = listBounds.x + 8.0f;
    const float headerToggleY =
        listBounds.y + (20.0f - kTreeToggleSize) * 0.5f;
    const Rectangle headerToggle = {
        headerToggleX,
        headerToggleY,
        kTreeToggleSize,
        kTreeToggleSize};
    const bool allOpen = allRootsExpanded();
    DrawRectangleRec(headerToggle, Color{40, 36, 48, 255});
    DrawRectangleLinesEx(headerToggle, 1.0f, kPanelBorder);
    if (canInteract && CheckCollisionPointRec(mouse, headerToggle))
        DrawRectangleRec(headerToggle, Color{60, 54, 72, 180});
    DrawTextEx(
        (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
        allOpen ? "-" : "+",
        {headerToggle.x + 3.0f, headerToggle.y},
        kFontTiny,
        1.0f,
        kTextPrimary);

    const std::string headerLabel = "Items (" + std::to_string(roots.size()) + ")";
    DrawTextEx(
        (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
        headerLabel.c_str(),
        {headerToggle.x + kTreeToggleSize + kTreeTogglePad + 2.0f, listBounds.y + 4.0f},
        kFontTiny,
        1.0f,
        kPanelBorder);

    // Edit / New Item buttons (right side of header).
    if (docs->itemsLoaded)
    {
        const Rectangle newBtn = newItemBtnBounds(listBounds);
        const bool newHover = canInteract && CheckCollisionPointRec(mouse, newBtn);
        DrawRectangleRec(newBtn, newHover ? kPanelAccent : Color{40, 36, 48, 255});
        DrawRectangleLinesEx(newBtn, 1.0f, kPanelBorder);
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            "New Item",
            {newBtn.x + 8.0f, newBtn.y + 4.0f},
            kFontTiny,
            1.0f,
            kTextPrimary);

        if (!selectedItemId.empty())
        {
            const Rectangle editBtn = editItemBtnBounds(listBounds);
            const bool editHover = canInteract && CheckCollisionPointRec(mouse, editBtn);
            DrawRectangleRec(editBtn, editHover ? kPanelAccent : Color{40, 36, 48, 255});
            DrawRectangleLinesEx(editBtn, 1.0f, kPanelBorder);
            DrawTextEx(
                (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
                "Edit Item",
                {editBtn.x + 8.0f, editBtn.y + 4.0f},
                kFontTiny,
                1.0f,
                kTextPrimary);
        }
    }

    const Rectangle treeBounds = {
        listBounds.x,
        listBounds.y + 20.0f,
        listBounds.width,
        listBounds.height - 20.0f};

    BeginScissorMode(
        static_cast<int>(treeBounds.x),
        static_cast<int>(treeBounds.y),
        static_cast<int>(treeBounds.width),
        static_cast<int>(treeBounds.height));

    float y = treeBounds.y + 4.0f - listScroll;
    for (const ConversationVisibleRow& row : rows)
    {
        if (row.node == nullptr)
            continue;

        const ConversationTreeNode& node = *row.node;
        const Rectangle rowBounds = {
            treeBounds.x + 2.0f,
            y,
            treeBounds.width - 4.0f,
            kTreeRowHeight - 1.0f};

        const bool selected = node.key == selectedKey;
        const bool hovered =
            canInteract &&
            CheckCollisionPointRec(mouse, treeBounds) &&
            CheckCollisionPointRec(mouse, rowBounds);

        if (selected)
            DrawRectangleRec(rowBounds, kSelection);
        else if (hovered)
            DrawRectangleRec(rowBounds, Color{60, 54, 72, 180});

        const float baseX = treeBounds.x + 8.0f;
        const Color lineColor = {96, 86, 72, 220};
        for (int d = 0; d < row.depth; ++d)
        {
            if (d >= static_cast<int>(row.ancestorContinues.size()))
                break;
            if (!row.ancestorContinues[static_cast<size_t>(d)])
                continue;
            const float lx = baseX + static_cast<float>(d) * kTreeIndent + kTreeToggleSize * 0.5f;
            DrawLineEx({lx, y}, {lx, y + kTreeRowHeight}, 1.0f, lineColor);
        }

        const float toggleX = baseX + static_cast<float>(row.depth) * kTreeIndent;
        if (!node.children.empty())
        {
            const Rectangle toggle = {
                toggleX,
                y + (kTreeRowHeight - kTreeToggleSize) * 0.5f,
                kTreeToggleSize,
                kTreeToggleSize};
            DrawRectangleLinesEx(toggle, 1.0f, kPanelBorder);
            DrawTextEx(
                (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
                isExpanded(node.key) ? "-" : "+",
                {toggle.x + 3.0f, toggle.y},
                kFontTiny,
                1.0f,
                kTextPrimary);
        }

        const float textX = toggleX + kTreeToggleSize + kTreeTogglePad + 2.0f;
        std::string line = node.label;
        if (!node.detail.empty())
            line += "  —  " + node.detail;
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            truncateForTree(line, 48).c_str(),
            {textX, y + 4.0f},
            kFontSmall,
            1.0f,
            selected ? kTextPrimary : kTextMuted);

        y += kTreeRowHeight;
    }

    EndScissorMode();
}

std::string ItemEditor::truncateForTree(const std::string& text, size_t maxLen)
{
    std::string compact;
    compact.reserve(text.size());
    bool lastSpace = false;
    for (char ch : text)
    {
        if (ch == '\n' || ch == '\r' || ch == '\t')
        {
            if (!lastSpace && !compact.empty())
            {
                compact.push_back(' ');
                lastSpace = true;
            }
            continue;
        }
        compact.push_back(ch);
        lastSpace = ch == ' ';
    }
    if (compact.size() <= maxLen)
        return compact;
    return compact.substr(0, maxLen - 1) + "…";
}

} // namespace timberline_editor
