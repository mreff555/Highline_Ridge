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

#include "SceneEditorApp.h"
#include "EditorPaths.h"

#include "EditorTheme.h"

#include <raylib.h>

#include <string>
#include <vector>

namespace timberline_editor
{

SceneEditorApp::SceneEditorApp()
{
    wireModules();
}

void SceneEditorApp::wireModules()
{
    variableEditor.docs = &document;
    variableEditor.selectionSceneId = &selectedSceneId;
    variableEditor.variablesScroll = &variablesScroll;
    variableEditor.stackDialogOpen = &sceneGraph.stackDialogOpen;
    variableEditor.onTreeRebuild = [this]()
    {
        conversation.rebuildConversationTree();
        itemEditor.rebuildTree();
    };

    conversation.docs = &document;
    conversation.text = &variableEditor;
    conversation.walkthrough = &dialogWalkthrough;
    conversation.selectionSceneId = &selectedSceneId;
    conversation.leftScroll = &mapCanvas.listScroll;
    conversation.stackDialogOpen = &sceneGraph.stackDialogOpen;
    conversation.draggingDivider = [this]() { return layout.isDraggingDivider(); };
    conversation.onSelectScene = [this](const std::string& sceneId)
    {
        dialogWalkthrough.selectConversationScene(sceneId);
    };

    dialogWalkthrough.docs = &document;
    dialogWalkthrough.selectionSceneId = &selectedSceneId;
    dialogWalkthrough.conversationSelectedKey = &conversation.selectedKey;
    dialogWalkthrough.onDirty = [this]() { document.markDirty(); };
    dialogWalkthrough.onTreeRebuild = [this]() { conversation.rebuildConversationTree(); };
    dialogWalkthrough.onSceneChanged = [this]()
    {
        // Keep the multi-scene tree; expand the active scene root only.
        conversation.rebuildConversationTree();
        if (!selectedSceneId.empty())
            conversation.expanded.insert("scene:" + selectedSceneId);
    };

    itemEditor.docs = &document;
    itemEditor.text = &variableEditor;
    itemEditor.stackDialogOpen = &sceneGraph.stackDialogOpen;
    itemEditor.draggingDivider = [this]() { return layout.isDraggingDivider(); };

    sceneGraph.docs = &document;
    sceneGraph.canvas = &mapCanvas;
    sceneGraph.selectionSceneId = &selectedSceneId;
    sceneGraph.canvasLevel = &mapCanvas.level;
    sceneGraph.canvasScroll = &mapCanvas.scroll;

    mapCanvas.docs = &document;
    mapCanvas.graph = &sceneGraph;
    mapCanvas.thumbnails = &thumbnails;
    mapCanvas.layout = &layout;
    mapCanvas.variableEditor = &variableEditor;
    mapCanvas.conversation = &conversation;
    mapCanvas.dialogWalkthrough = &dialogWalkthrough;
    mapCanvas.itemEditor = &itemEditor;
    mapCanvas.selectionSceneId = &selectedSceneId;
    mapCanvas.variablesScroll = &variablesScroll;
    mapCanvas.requestReload = [this]() { loadActiveDocument(); };
    mapCanvas.selectSceneForEditor = [this](const std::string& id) { selectSceneForEditor(id); };
    mapCanvas.saveDocument = [this]() { return saveDocument(); };
}

void SceneEditorApp::syncModuleFonts()
{
    variableEditor.uiFont = uiFont;
    variableEditor.uiFontBold = uiFontBold;
    conversation.uiFont = uiFont;
    conversation.uiFontBold = uiFontBold;
    dialogWalkthrough.uiFont = uiFont;
    dialogWalkthrough.uiFontBold = uiFontBold;
    itemEditor.uiFont = uiFont;
    itemEditor.uiFontBold = uiFontBold;
    mapCanvas.uiFont = uiFont;
    mapCanvas.uiFontBold = uiFontBold;
}

Font SceneEditorApp::textFont() const
{
    if (uiFont.texture.id != 0)
        return uiFont;
    return GetFontDefault();
}

Font SceneEditorApp::boldFont() const
{
    if (uiFontBold.texture.id != 0)
        return uiFontBold;
    return textFont();
}

Font SceneEditorApp::tryLoadFont(const std::string candidates[], size_t count) const
{
    for (size_t i = 0; i < count; ++i)
    {
        const std::string& path = candidates[i];
        if (path.empty() || !FileExists(path.c_str()))
            continue;

        Font font = LoadFontEx(path.c_str(), 64, nullptr, 0);
        if (font.texture.id == 0)
            continue;

        SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
        TraceLog(LOG_INFO, "TIMBERLINE: loaded UI font %s", path.c_str());
        return font;
    }
    return Font{};
}

void SceneEditorApp::loadUiFont()
{
    const std::string regularCandidates[] = {
        "resources/fonts/CourierPrime-Regular.ttf",
        "../resources/fonts/CourierPrime-Regular.ttf",
        "../../resources/fonts/CourierPrime-Regular.ttf",
        "../../../resources/fonts/CourierPrime-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    const std::string boldCandidates[] = {
        "resources/fonts/CourierPrime-Bold.ttf",
        "../resources/fonts/CourierPrime-Bold.ttf",
        "../../resources/fonts/CourierPrime-Bold.ttf",
        "../../../resources/fonts/CourierPrime-Bold.ttf",
        "/System/Library/Fonts/Supplemental/Courier New Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    };

    uiFont = tryLoadFont(regularCandidates, sizeof(regularCandidates) / sizeof(regularCandidates[0]));
    uiFontBold = tryLoadFont(boldCandidates, sizeof(boldCandidates) / sizeof(boldCandidates[0]));
    if (uiFontBold.texture.id == 0)
        uiFontBold = uiFont;
    syncModuleFonts();
}

void SceneEditorApp::unloadUiFont()
{
    if (uiFont.texture.id != 0)
        UnloadFont(uiFont);
    if (uiFontBold.texture.id != 0 && uiFontBold.texture.id != uiFont.texture.id)
        UnloadFont(uiFontBold);
    uiFont = Font{};
    uiFontBold = Font{};
    syncModuleFonts();
}

void SceneEditorApp::markDirty()
{
    document.markDirty();
}

bool SceneEditorApp::saveDocument()
{
    if (variableEditor.open)
        variableEditor.saveVariableEditor();
    return document.saveAll();
}

void SceneEditorApp::unloadThumbnails()
{
    thumbnails.clear();
}

void SceneEditorApp::selectSceneForEditor(const std::string& id)
{
    if (id.empty() || selectedSceneId == id)
        return;
    selectedSceneId = id;
    variableEditor.selectedVariableKey.clear();
    variablesScroll = 0.0f;
    if (document.isConversationsTab())
    {
        mapCanvas.listScroll = 0.0f;
        conversation.rebuildConversationTree();
        dialogWalkthrough.ensureConversationSceneSelected();
        dialogWalkthrough.rebuildSteps();
        if (!selectedSceneId.empty())
            conversation.expanded.insert("scene:" + selectedSceneId);
    }
}

bool SceneEditorApp::loadActiveDocument()
{
    document.loadError.clear();
    variableEditor.closeVariableEditor();

    if (!resourceDirectoryExists(document.resourceDir))
    {
        document.loadError = "Resources folder not found:\n" + document.resourceDir +
            "\n\nLaunch with:\n./scene-editor /path/to/resources";
        document.clearScenes();
        document.clearConversations();
        conversation.roots.clear();
        selectedSceneId.clear();
        return false;
    }

    if (document.jsonTabs.empty())
    {
        document.loadError = "No .json files found in:\n" + document.resourceDir;
        document.clearScenes();
        document.clearConversations();
        document.clearItems();
        conversation.roots.clear();
        itemEditor.roots.clear();
        selectedSceneId.clear();
        return false;
    }

    const std::string filename = document.activeTabFilename();
    if (filename != "scenes.json"
        && filename != "conversations.json"
        && filename != "items.json")
    {
        document.loadError = filename
            + " is not an editable tab.\n"
              "Select scenes.json, conversations.json, or items.json.\n"
              "Craft recipes are edited on product items (components).";
        return false;
    }

    if (filename == "items.json")
    {
        document.clearConversations();
        conversation.roots.clear();
        if (!document.loadItemsDocument())
        {
            itemEditor.roots.clear();
            return false;
        }
        // Scenes still load so other panes stay coherent if user switches back.
        document.loadScenesFile();
        document.dirty = false;
        itemEditor.listScroll = 0.0f;
        itemEditor.selectedKey.clear();
        itemEditor.selectedItemId.clear();
        itemEditor.rebuildTree();
        for (const ConversationTreeNode& root : itemEditor.roots)
            itemEditor.expanded.insert(root.key);
        return true;
    }

    document.clearItems();
    itemEditor.roots.clear();

    if (!document.loadScenesFile())
    {
        selectedSceneId.clear();
        conversation.roots.clear();
        return false;
    }

    document.dirty = false;
    if (selectedSceneId.empty() || !document.scenes.hasScene(selectedSceneId))
    {
        const std::vector<std::string> ids = document.scenes.sceneIds();
        selectedSceneId = ids.empty() ? "" : ids.front();
    }

    sceneGraph.ensureDefaultLayouts();

    if (filename == "conversations.json")
    {
        if (!document.loadConversationsDocument())
        {
            conversation.roots.clear();
            return false;
        }
        document.dirty = false;
        mapCanvas.listScroll = 0.0f;
        conversation.rebuildConversationTree();
        dialogWalkthrough.ensureConversationSceneSelected();
        dialogWalkthrough.rebuildSteps();
        if (!selectedSceneId.empty())
            conversation.expanded.insert("scene:" + selectedSceneId);
        return true;
    }

    return true;
}

bool SceneEditorApp::deleteSelectedScene()
{
    if (variableEditor.open || sceneGraph.stackDialogOpen || itemEditor.blocksInput())
        return false;
    if (selectedSceneId.empty() || !document.scenes.hasScene(selectedSceneId))
        return false;

    const std::string removedId = selectedSceneId;
    if (!document.scenes.removeScene(removedId))
        return false;

    // Drop cached art for the removed id (and any stale entries).
    thumbnails.clear();
    mapCanvas.cancelLinkDrag();
    mapCanvas.dragSource = DragSource::None;
    mapCanvas.dragSceneId.clear();

    selectedSceneId.clear();
    variableEditor.selectedVariableKey.clear();
    variablesScroll = 0.0f;

    const std::vector<std::string> remaining = document.scenes.sceneIds();
    if (!remaining.empty())
        selectedSceneId = remaining.front();

    if (document.isConversationsTab())
    {
        conversation.selectedKey.clear();
        conversation.rebuildConversationTree();
        for (const ConversationTreeNode& root : conversation.roots)
            conversation.expanded.insert(root.key);
    }

    document.markDirty();
    return true;
}

void SceneEditorApp::handleShortcuts()
{
    if (variableEditor.open || sceneGraph.stackDialogOpen || itemEditor.blocksInput())
        return;

    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
    {
        if (IsKeyPressed(KEY_S))
            saveDocument();
    }

    // Delete / Forward-Delete removes the selected scene from the map and JSON.
    if (IsKeyPressed(KEY_DELETE))
        deleteSelectedScene();
}

void SceneEditorApp::update()
{
    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();
    layout.syncToWindow(screenWidth, screenHeight);
    syncModuleFonts();

    handleShortcuts();

    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        layout.cancelDividerDrag();

    if (!variableEditor.open && !sceneGraph.stackDialogOpen && !itemEditor.blocksInput())
    {
        const Rectangle left = layout.leftPaneBounds(screenWidth);
        const Rectangle listBounds = {
            left.x,
            left.y + kTabHeight + 4.0f,
            left.width,
            left.height - kTabHeight - 8.0f};
        if (document.isConversationsTab())
            conversation.handleConversationTreeInput(listBounds);
        else if (document.isItemsTab())
            itemEditor.handleInput(listBounds);
    }

    if (variableEditor.open)
    {
        // Layout metrics must be ready before hit-testing Save/Cancel/field.
        variableEditor.syncDialogLayout(screenWidth, screenHeight);
        variableEditor.handleVariableEditorTextInput();
        if (CheckCollisionPointRec(GetMousePosition(), variableEditor.fieldRect))
            SetMouseCursor(MOUSE_CURSOR_IBEAM);
        else
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        return;
    }

    if (itemEditor.blocksInput())
    {
        itemEditor.handleNewItemDialogInput(screenWidth, screenHeight);
        return;
    }

    if (sceneGraph.stackDialogOpen)
        return;

    if (document.isConversationsTab())
    {
        const Rectangle main = layout.mainPaneBounds(screenWidth);
        const Rectangle canvasBounds = {
            main.x + 4.0f, main.y + 4.0f, main.width - 8.0f, main.height - 8.0f};
        dialogWalkthrough.handleInput(canvasBounds);
        if (CheckCollisionPointRec(GetMousePosition(), dialogWalkthrough.textField))
            SetMouseCursor(MOUSE_CURSOR_IBEAM);
        else
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    }

    layout.handleDividerInput(screenWidth, screenHeight);

    if (layout.isDraggingDivider() || variableEditor.open)
        return;

    // Canvas/list interaction is handled inside draw paths historically;
    // drawCanvas still processes drag while drawing (legacy). Keep that behavior.
}

void SceneEditorApp::draw()
{
    mapCanvas.draw();
}

} // namespace timberline_editor
