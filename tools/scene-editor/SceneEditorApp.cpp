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
#include "EditorInput.h"
#include "EditorPaths.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "SpeakConversationCreate.h"

#if defined(__APPLE__)
#include "macos/EditorNativeMenu.h"
#endif

#include <raylib.h>

#include <algorithm>
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
    variableEditor.parchment = &parchmentEditor;
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
    conversation.onTreeSelection =
        [this](const std::string& treeKey, const std::string& sceneId) {
            dialogFlow.migrateFromTreeSelection(treeKey, sceneId);
        };

    dialogWalkthrough.docs = &document;
    dialogWalkthrough.parchment = &parchmentEditor;
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

    dialogFlow.docs = &document;
    dialogFlow.selectionSceneId = &selectedSceneId;
    dialogFlow.parchment = &parchmentEditor;
    dialogPalette.flow = &dialogFlow;
    dialogDetails.flow = &dialogFlow;
    dialogDetails.onEditFight = [this](DialogFlowNode* n) {
        if (n != nullptr)
            fightAuthoring.openForFight(selectedSceneId, n);
    };
    dialogFlow.onEditFight = [this](DialogFlowNode* n) {
        if (n != nullptr)
            fightAuthoring.openForFight(selectedSceneId, n);
    };
    dialogDetails.onEditActorDialog = [this](DialogFlowNode* n) {
        if (n != nullptr)
            actorDialogAuthoring.openForActorDialog(selectedSceneId, n);
    };
    dialogFlow.onEditActorDialog = [this](DialogFlowNode* n) {
        if (n != nullptr)
            actorDialogAuthoring.openForActorDialog(selectedSceneId, n);
    };
    dialogDetails.onEditEvent = [this](DialogFlowNode* n) {
        if (n != nullptr)
            eventAuthoring.openForEvent(selectedSceneId, n);
    };
    dialogFlow.onEditEvent = [this](DialogFlowNode* n) {
        if (n != nullptr)
            eventAuthoring.openForEvent(selectedSceneId, n);
    };
    dialogMedia.docs = &document;
    dialogMedia.flow = &dialogFlow;
    dialogMedia.thumbnails = &thumbnails;
    dialogMedia.selectionSceneId = &selectedSceneId;

    itemEditor.docs = &document;
    itemEditor.text = &variableEditor;
    itemEditor.parchment = &parchmentEditor;
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
    mapCanvas.dialogFlow = &dialogFlow;
    mapCanvas.dialogPalette = &dialogPalette;
    mapCanvas.dialogDetails = &dialogDetails;
    mapCanvas.dialogMedia = &dialogMedia;
    mapCanvas.itemEditor = &itemEditor;
    mapCanvas.selectionSceneId = &selectedSceneId;
    mapCanvas.variablesScroll = &variablesScroll;
    // Explicit Reload still hits disk; tab clicks use in-memory switch (K12).
    mapCanvas.requestReload = [this]() { loadActiveDocument(); };
    mapCanvas.requestTabSwitch = [this](int tabIndex) { switchJsonTabInMemory(tabIndex); };
    mapCanvas.onCreateSpeakConversation =
        [this](const std::string& sceneId) { openCreateSpeakConversation(sceneId); };
    mapCanvas.onOpenSpeakConversation =
        [this](const std::string& sceneId) { openExistingSpeakConversation(sceneId); };
    mapCanvas.onDeleteSpeakConversation =
        [this](const std::string& sceneId) {
            speakDeletePendingSceneId = speakParentSceneId(sceneId);
            speakDeleteConfirmOpen = true;
            speakDeleteWaitMouseRelease = true;
        };
    mapCanvas.drawSpeakConversationCta =
        [this](Rectangle listBounds) { drawSpeakConversationCta(listBounds); };
    mapCanvas.selectSceneForEditor = [this](const std::string& id) { selectSceneForEditor(id); };
    mapCanvas.saveDocument = [this]() { return saveDocument(); };
    speakCreateDialog.docs = &document;
    speakCreateDialog.onCreated =
        [this](const SpeakConversationCreatePayload& payload) {
            finishSpeakConversationCreate(payload);
        };
    mapCanvas.sceneAuthoring.docs = &document;
    mapCanvas.sceneAuthoring.onCreated = [this](const std::string& id)
    {
        // Only drop this scene's thumb — clear() raced with map draw while
        // Generate image rotated/replaced the plate (dangling ThumbnailEntry).
        if (!id.empty())
            thumbnails.invalidate(id);
        // selectSceneForEditor is a no-op when id is already selected, so always
        // invalidate bottom-pane media. Otherwise a prior failed load (files not
        // on disk yet) keeps Play disabled after Generate/Confirm writes them.
        mapCanvas.previewMusicPath.clear();
        mapCanvas.previewAmbientPath.clear();
        mapCanvas.previewLargePath.clear();
        mapCanvas.previewBoundSceneId.clear();
        selectSceneForEditor(id);
    };

    mapCanvas.sceneAssist.docs = &document;
    mapCanvas.sceneAssist.thumbnails = &thumbnails;
    mapCanvas.sceneAssist.onAccepted = [this]()
    {
        // Force bottom-pane media to reload live (or cleared preview) paths.
        mapCanvas.previewMusicPath.clear();
        mapCanvas.previewAmbientPath.clear();
        mapCanvas.previewLargePath.clear();
        mapCanvas.previewBoundSceneId.clear();
    };
    variableEditor.onAiAssist = [this]()
    {
        if (!selectedSceneId.empty())
            mapCanvas.sceneAssist.openForScene(selectedSceneId);
    };
    mapCanvas.sceneInventory.docs = &document;
    mapCanvas.sceneInventory.onSaved = [this]()
    {
        // Variables pane refreshes from scenes document on next draw.
        (void)this;
    };
    variableEditor.onSceneInventory = [this]()
    {
        if (!selectedSceneId.empty())
            mapCanvas.sceneInventory.openForScene(selectedSceneId);
    };
    mapCanvas.sceneInteractions.docs = &document;
    mapCanvas.sceneInteractions.onSaved = [this]()
    {
        mapCanvas.cachedLinkRoutes.clear();
        (void)this;
    };
    variableEditor.onSceneInteractions = [this]()
    {
        if (!selectedSceneId.empty())
            mapCanvas.sceneInteractions.openForScene(selectedSceneId);
    };
    mapCanvas.sceneEffects.docs = &document;
    mapCanvas.sceneEffects.onSaved = [this]() { (void)this; };
    variableEditor.onSceneEffects = [this]()
    {
        if (!selectedSceneId.empty())
            mapCanvas.sceneEffects.openForScene(selectedSceneId);
    };
    mapCanvas.sceneStoryEvents.docs = &document;
    mapCanvas.sceneStoryEvents.onSaved = [this]() { (void)this; };
    variableEditor.onSceneStoryEvents = [this]()
    {
        if (!selectedSceneId.empty())
            mapCanvas.sceneStoryEvents.openForScene(selectedSceneId);
    };
    mapCanvas.sceneTransition.docs = &document;
    mapCanvas.sceneTransition.graph = &sceneGraph;
    mapCanvas.sceneUseTransition.docs = &document;
    mapCanvas.sceneUseTransition.graph = &sceneGraph;
    mapCanvas.sceneFloorConnect.docs = &document;
    mapCanvas.sceneFloorConnect.graph = &sceneGraph;

    mapCanvas.preferences = &preferences;
    mapCanvas.openPreferences = [this]()
    {
        preferences.openDialog(document.resourceDir, document.assetRoot);
    };

    apiKeysDialog.keys = &sessionApiKeys;
    mapCanvas.apiKeysDialog = &apiKeysDialog;
    mapCanvas.sceneAuthoring.sessionKeys = &sessionApiKeys;
    mapCanvas.sceneAssist.sessionKeys = &sessionApiKeys;
    mapCanvas.sceneExitRequirements.sessionKeys = &sessionApiKeys;
    itemEditor.sessionKeys = &sessionApiKeys;
    fightAuthoring.docs = &document;
    fightAuthoring.parchment = &parchmentEditor;
    fightAuthoring.sessionKeys = &sessionApiKeys;
    fightAuthoring.onSaved = [this]() { document.markDirty(); };
    mapCanvas.fightAuthoring = &fightAuthoring;
    actorDialogAuthoring.docs = &document;
    actorDialogAuthoring.flow = &dialogFlow;
    actorDialogAuthoring.parchment = &parchmentEditor;
    actorDialogAuthoring.sessionKeys = &sessionApiKeys;
    actorDialogAuthoring.onSaved = [this]() { document.markDirty(); };
    mapCanvas.actorDialogAuthoring = &actorDialogAuthoring;
    eventAuthoring.docs = &document;
    eventAuthoring.onSaved = [this]() { document.markDirty(); };
    mapCanvas.eventAuthoring = &eventAuthoring;

    // Temporary: seed session from env / ~/.config/highline-ridge/*_api_key.
    sessionApiKeys.bootstrapFromEnvAndFiles();
}

void SceneEditorApp::syncModuleFonts()
{
    variableEditor.uiFont = uiFont;
    variableEditor.uiFontBold = uiFontBold;
    conversation.uiFont = uiFont;
    conversation.uiFontBold = uiFontBold;
    dialogWalkthrough.uiFont = uiFont;
    dialogWalkthrough.uiFontBold = uiFontBold;
    dialogFlow.uiFont = uiFont;
    dialogFlow.uiFontBold = uiFontBold;
    dialogPalette.uiFont = uiFont;
    dialogPalette.uiFontBold = uiFontBold;
    dialogDetails.uiFont = uiFont;
    dialogDetails.uiFontBold = uiFontBold;
    dialogMedia.uiFont = uiFont;
    dialogMedia.uiFontBold = uiFontBold;
    itemEditor.uiFont = uiFont;
    itemEditor.uiFontBold = uiFontBold;
    mapCanvas.uiFont = uiFont;
    mapCanvas.uiFontBold = uiFontBold;
    mapCanvas.parchment = &parchmentEditor;
    parchmentEditor.sessionKeys = &sessionApiKeys;
    mapCanvas.sceneAuthoring.parchment = &parchmentEditor;
    mapCanvas.sceneAuthoring.uiFont = uiFont;
    mapCanvas.sceneAuthoring.uiFontBold = uiFontBold;
    apiKeysDialog.uiFont = uiFont;
    apiKeysDialog.uiFontBold = uiFontBold;
    mapCanvas.sceneAssist.uiFont = uiFont;
    mapCanvas.sceneAssist.uiFontBold = uiFontBold;
    mapCanvas.sceneInventory.uiFont = uiFont;
    mapCanvas.sceneInventory.uiFontBold = uiFontBold;
    mapCanvas.sceneInteractions.uiFont = uiFont;
    mapCanvas.sceneInteractions.uiFontBold = uiFontBold;
    mapCanvas.sceneStoryEvents.uiFont = uiFont;
    mapCanvas.sceneStoryEvents.uiFontBold = uiFontBold;
    mapCanvas.sceneEffects.uiFont = uiFont;
    mapCanvas.sceneEffects.uiFontBold = uiFontBold;
    mapCanvas.sceneTransition.uiFont = uiFont;
    mapCanvas.sceneTransition.uiFontBold = uiFontBold;
    mapCanvas.sceneExitRequirements.docs = &document;
    mapCanvas.sceneExitRequirements.graph = &sceneGraph;
    mapCanvas.sceneExitRequirements.parchment = &parchmentEditor;
    mapCanvas.sceneExitRequirements.uiFont = uiFont;
    mapCanvas.sceneExitRequirements.uiFontBold = uiFontBold;
    fightAuthoring.docs = &document;
    fightAuthoring.parchment = &parchmentEditor;
    fightAuthoring.sessionKeys = &sessionApiKeys;
    fightAuthoring.uiFont = uiFont;
    fightAuthoring.uiFontBold = uiFontBold;
    actorDialogAuthoring.docs = &document;
    actorDialogAuthoring.flow = &dialogFlow;
    actorDialogAuthoring.parchment = &parchmentEditor;
    actorDialogAuthoring.sessionKeys = &sessionApiKeys;
    actorDialogAuthoring.uiFont = uiFont;
    actorDialogAuthoring.uiFontBold = uiFontBold;
    eventAuthoring.docs = &document;
    eventAuthoring.uiFont = uiFont;
    eventAuthoring.uiFontBold = uiFontBold;
    mapCanvas.sceneUseTransition.uiFont = uiFont;
    mapCanvas.sceneUseTransition.uiFontBold = uiFontBold;
    mapCanvas.sceneFloorConnect.uiFont = uiFont;
    mapCanvas.sceneFloorConnect.uiFontBold = uiFontBold;
    preferences.uiFont = uiFont;
    preferences.uiFontBold = uiFontBold;
    speakCreateDialog.uiFont = uiFont;
    speakCreateDialog.uiFontBold = uiFontBold;
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
    parchmentEditor.unloadAssets();
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
    dialogMedia.unloadAudio();
    dialogFlow.unloadIcons();
    mapCanvas.unloadScenePreviewMedia();
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
        {
            conversation.expanded.insert("scene:" + selectedSceneId);
            dialogFlow.migrateFromTreeSelection(
                "scene:" + selectedSceneId, selectedSceneId);
        }
    }
}

bool SceneEditorApp::switchJsonTabInMemory(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= static_cast<int>(document.jsonTabs.size()))
        return false;

    const bool sameTab = (tabIndex == document.activeTabIndex);
    document.activeTabIndex = tabIndex;
    thumbnails.clear();

    const std::string filename = document.activeTabFilename();
    if (filename == "conversations.json")
    {
        // K15: first-load from disk only while still unloaded — never invent empty root.
        if (!document.conversationsLoaded)
        {
            if (!document.loadConversationsDocument())
                return false;
            // Preserve dirty from scenes/items mutations; do not clear dirty.
        }
        if (!sameTab)
            mapCanvas.listScroll = 0.0f;
        conversation.rebuildConversationTree();
        dialogWalkthrough.ensureConversationSceneSelected();
        dialogWalkthrough.rebuildSteps();
        if (!selectedSceneId.empty())
        {
            conversation.expanded.insert("scene:" + selectedSceneId);
            dialogFlow.migrateFromTreeSelection(
                "scene:" + selectedSceneId, selectedSceneId);
        }
        return true;
    }

    if (filename == "items.json")
    {
        if (!document.itemsLoaded)
        {
            if (!document.loadItemsDocument())
            {
                itemEditor.roots.clear();
                return false;
            }
        }
        // Keep scenes loaded for coherent switch-back; do not reload from disk.
        if (!document.scenes.isLoaded())
            document.loadScenesFile();
        itemEditor.listScroll = 0.0f;
        itemEditor.rebuildTree();
        for (const ConversationTreeNode& root : itemEditor.roots)
            itemEditor.expanded.insert(root.key);
        return true;
    }

    if (filename == "scenes.json")
    {
        if (!document.scenes.isLoaded())
        {
            if (!document.loadScenesFile())
                return false;
        }
        sceneGraph.ensureDefaultLayouts();
        return true;
    }

    // Unknown tab: keep buffers; status via loadError for explicit reload path.
    return true;
}

void SceneEditorApp::openCreateSpeakConversation(const std::string& sceneId)
{
    const std::string parentId = speakParentSceneId(sceneId);
    if (parentId.empty() || !document.scenes.hasScene(parentId))
    {
        speakCreateStatus = "Select a scene that exists in scenes.json.";
        return;
    }
    // K15 before modal opens so refuse/AlreadyHasPhases uses real root.
    if (!document.conversationsLoaded)
    {
        if (!document.loadConversationsDocument())
        {
            speakCreateStatus = document.loadError.empty()
                ? "Failed to load conversations.json."
                : document.loadError;
            return;
        }
    }
    if (sceneHasSpeakPhases(document, parentId))
    {
        speakCreateStatus =
            "Scene already has speakPhases — use Open Speak conversation.";
        openExistingSpeakConversation(parentId);
        return;
    }
    selectedSceneId = parentId;
    speakCreateDialog.openDialog(parentId);
}

void SceneEditorApp::openExistingSpeakConversation(const std::string& sceneId)
{
    const std::string parentId = speakParentSceneId(sceneId);
    if (parentId.empty())
        return;

    if (!document.conversationsLoaded)
    {
        if (!document.loadConversationsDocument())
        {
            speakCreateStatus = document.loadError.empty()
                ? "Failed to load conversations.json."
                : document.loadError;
            return;
        }
    }

    int conversationsIndex = -1;
    for (size_t i = 0; i < document.jsonTabs.size(); ++i)
    {
        if (document.jsonTabs[i] == "conversations.json")
        {
            conversationsIndex = static_cast<int>(i);
            break;
        }
    }
    if (conversationsIndex < 0)
    {
        speakCreateStatus = "conversations.json tab not found.";
        return;
    }

    selectedSceneId = parentId;
    switchJsonTabInMemory(conversationsIndex);
    conversation.rebuildConversationTree();
    conversation.selectedKey = "scene:" + parentId;
    conversation.expanded.insert("scene:" + parentId);
    dialogFlow.clearGraph();
    dialogFlow.migrateFromTreeSelection("scene:" + parentId, parentId);
    speakCreateStatus = "Opened Speak conversation for " + parentId;
}

void SceneEditorApp::finishSpeakConversationCreate(
    const SpeakConversationCreatePayload& payload)
{
    const std::string sceneId = payload.sceneId;
    selectedSceneId = sceneId;
    speakCreateStatus =
        "Created Speak conversation for " + sceneId + " (unsaved)";

    int conversationsIndex = -1;
    for (size_t i = 0; i < document.jsonTabs.size(); ++i)
    {
        if (document.jsonTabs[i] == "conversations.json")
        {
            conversationsIndex = static_cast<int>(i);
            break;
        }
    }
    if (conversationsIndex >= 0)
        switchJsonTabInMemory(conversationsIndex);

    conversation.rebuildConversationTree();
    conversation.selectedKey = "scene:" + sceneId;
    conversation.expanded.insert("scene:" + sceneId);
    dialogFlow.clearGraph();
    dialogFlow.migrateFromTreeSelection("scene:" + sceneId, sceneId);

    if (payload.openActorDialogAfter)
    {
        DialogFlowNode* introNode = nullptr;
        for (DialogFlowNode& node : dialogFlow.nodes)
        {
            if (node.kind == DialogNodeKind::ActorDialog
                && node.jsonPointer.find("/intro") != std::string::npos)
            {
                introNode = &node;
                break;
            }
        }
        if (introNode != nullptr)
            actorDialogAuthoring.openForActorDialog(sceneId, introNode);
    }
}

void SceneEditorApp::deleteSpeakConversationForScene(const std::string& sceneId)
{
    const std::string parentId = speakParentSceneId(sceneId);
    if (parentId.empty())
        return;

    if (!document.conversationsLoaded)
    {
        if (!document.loadConversationsDocument())
        {
            speakCreateStatus = document.loadError.empty()
                ? "Failed to load conversations.json."
                : document.loadError;
            return;
        }
    }

    std::string err;
    const SpeakConversationDeleteStatus st =
        deleteSpeakConversation(document, parentId, true, err);
    if (st != SpeakConversationDeleteStatus::Ok)
    {
        speakCreateStatus =
            err.empty() ? speakConversationDeleteStatusMessage(st) : err;
        return;
    }

    speakCreateStatus =
        "Deleted Speak conversation for " + parentId + " (unsaved)";
    conversation.rebuildConversationTree();
    if (conversation.selectedKey.rfind("scene:" + parentId, 0) == 0
        || conversation.selectedKey.find(":" + parentId + ":") != std::string::npos
        || conversation.selectedKey.find("/" + parentId + "/") != std::string::npos)
    {
        conversation.selectedKey.clear();
    }
    dialogFlow.clearGraph();
    if (document.isConversationsTab() && !selectedSceneId.empty())
    {
        dialogFlow.migrateFromTreeSelection(
            "scene:" + selectedSceneId, selectedSceneId);
    }
}

void SceneEditorApp::drawSpeakConversationCta(Rectangle listBounds)
{
    if (!document.isConversationsTab() || !document.conversationsLoaded)
        return;
    if (speakCreateDialog.blocksInput() || speakDeleteConfirmOpen)
        return;
    if (preferences.blocksInput() || apiKeysDialog.blocksInput()
        || variableEditor.open || parchmentEditor.blocksInput()
        || fightAuthoring.blocksInput() || actorDialogAuthoring.blocksInput()
        || eventAuthoring.blocksInput())
        return;

    const Font font = textFont();
    const Font bold = boldFont();
    const std::string sceneId = selectedSceneId;
    const bool hasScene = !sceneId.empty() && document.scenes.hasScene(sceneId);
    const bool hasPhases = hasScene && sceneHasSpeakPhases(document, sceneId);
    const bool missingOrEmpty =
        hasScene && !hasPhases; // covers missing key + empty speakPhases

    // Banner near bottom of left list (above status chrome).
    const float bannerH = 72.0f;
    const Rectangle banner = {
        listBounds.x + 6.0f,
        listBounds.y + listBounds.height - bannerH - 4.0f,
        listBounds.width - 12.0f,
        bannerH};

    std::string line;
    if (!hasScene)
        line = "Select a scene (Scenes tab) to create Speak.";
    else if (hasPhases)
        line = sceneId + " — Speak conversation exists.";
    else if (conversation.roots.empty())
        line = "No scenes with speakPhases. Selected: " + sceneId;
    else
        line = sceneId + " has no Speak conversation.";

    DrawRectangleRec(banner, Color{28, 26, 36, 230});
    DrawRectangleLinesEx(banner, 1.0f, kPanelBorder);
    drawWrappedText(
        font,
        line,
        {banner.x + 8.0f, banner.y + 6.0f},
        banner.width - 16.0f,
        kFontTiny,
        2.0f,
        kTextMuted);

    const float btnH = 26.0f;
    const float btnY = banner.y + banner.height - btnH - 8.0f;
    Rectangle createBtn = {banner.x + 8.0f, btnY, 150.0f, btnH};
    Rectangle deleteBtn = {
        banner.x + 8.0f + 158.0f, btnY, 150.0f, btnH};

    const bool canCreate = missingOrEmpty;
    const bool canDelete = hasPhases || (hasScene && sceneHasConversationEntry(document, sceneId));

    if (canCreate)
    {
        const bool hover = CheckCollisionPointRec(GetMousePosition(), createBtn);
        DrawRectangleRec(createBtn, hover ? kPanelAccent : Color{48, 44, 58, 255});
        DrawRectangleLinesEx(createBtn, 1.0f, kPanelBorder);
        DrawTextEx(
            bold,
            "Create Speak...",
            {createBtn.x + 12.0f, createBtn.y + 5.0f},
            kFontTiny,
            1.0f,
            kTextPrimary);
    }
    if (canDelete)
    {
        if (!canCreate)
            deleteBtn.x = banner.x + 8.0f;
        const bool hover = CheckCollisionPointRec(GetMousePosition(), deleteBtn);
        DrawRectangleRec(deleteBtn, hover ? Color{90, 40, 48, 255} : Color{48, 44, 58, 255});
        DrawRectangleLinesEx(deleteBtn, 1.0f, kPanelBorder);
        DrawTextEx(
            bold,
            "Delete Speak...",
            {deleteBtn.x + 12.0f, deleteBtn.y + 5.0f},
            kFontTiny,
            1.0f,
            kTextPrimary);
    }

    if (!speakCreateStatus.empty())
    {
        DrawTextEx(
            font,
            speakCreateStatus.c_str(),
            {banner.x + 8.0f, banner.y + 28.0f},
            kFontTiny,
            1.0f,
            kTextMuted);
    }

    if (editorMousePressed(MOUSE_BUTTON_LEFT))
    {
        const Vector2 mouse = GetMousePosition();
        if (canCreate && CheckCollisionPointRec(mouse, createBtn))
            openCreateSpeakConversation(sceneId);
        else if (canDelete && CheckCollisionPointRec(mouse, deleteBtn))
        {
            speakDeletePendingSceneId = sceneId;
            speakDeleteConfirmOpen = true;
            speakDeleteWaitMouseRelease = true;
        }
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
        {
            conversation.expanded.insert("scene:" + selectedSceneId);
            dialogFlow.migrateFromTreeSelection(
                "scene:" + selectedSceneId, selectedSceneId);
        }
        return true;
    }

    return true;
}

bool SceneEditorApp::deleteSelectedScene()
{
    // Opens the shared confirm (+ optional purge) flow on the map canvas.
    // Do not delete scenes while editing Conversations/Items text (Delete key).
    if (document.isConversationsTab() || document.isItemsTab())
        return false;
    if (preferences.blocksInput() || apiKeysDialog.blocksInput() || variableEditor.open
        || sceneGraph.stackDialogOpen || itemEditor.blocksInput()
        || mapCanvas.blocksInput()
        || mapCanvas.contextMenuSource != SceneMapCanvas::ContextMenuSource::None)
        return false;
    if (selectedSceneId.empty() || !document.scenes.hasScene(selectedSceneId))
        return false;
    mapCanvas.requestDeleteSelectedScene();
    return true;
}

void SceneEditorApp::handleShortcuts()
{
    if (parchmentEditor.blocksInput() || preferences.blocksInput()
        || apiKeysDialog.blocksInput() || variableEditor.open
        || sceneGraph.stackDialogOpen || itemEditor.blocksInput()
        || mapCanvas.sceneAuthoring.blocksInput()
        || mapCanvas.sceneAssist.blocksInput()
        || mapCanvas.sceneInventory.blocksInput()
        || mapCanvas.sceneInteractions.blocksInput()
        || mapCanvas.sceneStoryEvents.blocksInput()
        || mapCanvas.sceneEffects.blocksInput()
        || mapCanvas.sceneTransition.blocksInput()
        || mapCanvas.sceneExitRequirements.blocksInput()
        || fightAuthoring.blocksInput()
        || actorDialogAuthoring.blocksInput()
        || eventAuthoring.blocksInput()
        || speakCreateDialog.blocksInput()
        || speakDeleteConfirmOpen
        || mapCanvas.sceneUseTransition.blocksInput()
        || mapCanvas.sceneFloorConnect.blocksInput()
        || mapCanvas.confirmMode != SceneMapCanvas::ConfirmMode::None
        || mapCanvas.contextMenuSource != SceneMapCanvas::ContextMenuSource::None)
        return;

    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool super = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (ctrl || super)
    {
        // Ctrl+S / Cmd+S — Save (macOS also has File → Save in the menu bar).
        if (IsKeyPressed(KEY_S))
            saveDocument();
        // Ctrl+, / Cmd+, — Preferences (macOS also has a native menu item).
        if (IsKeyPressed(KEY_COMMA))
            preferences.openDialog(document.resourceDir, document.assetRoot);
    }

    // Delete must not steal from text editing (Conversations walkthrough, etc.).
    // Scene delete remains available from the list/map context menus.
    if (IsKeyPressed(KEY_DELETE))
    {
        if (document.isConversationsTab() || document.isItemsTab()
            || dialogWalkthrough.textFieldFocused || variableEditor.open)
            return;
        deleteSelectedScene();
    }
}

void SceneEditorApp::update()
{
    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();
    layout.syncToWindow(screenWidth, screenHeight);
    syncModuleFonts();

    sessionApiKeys.poll();

#if defined(__APPLE__)
    // Native File → Save (⌘S) from the Cocoa menu bar.
    if (gEditorSaveMenuRequested.exchange(false))
        saveDocument();

    // Native Preferences… menu (⌘,) sets this flag from Cocoa.
    if (gEditorPreferencesMenuRequested.exchange(false))
    {
        if (!preferences.blocksInput() && !apiKeysDialog.blocksInput()
            && !variableEditor.open && !itemEditor.blocksInput()
            && !mapCanvas.sceneAuthoring.blocksInput()
            && !mapCanvas.sceneAssist.blocksInput()
            && !mapCanvas.sceneInventory.blocksInput()
            && !mapCanvas.sceneInteractions.blocksInput()
            && !mapCanvas.sceneStoryEvents.blocksInput()
            && !mapCanvas.sceneEffects.blocksInput()
            && !mapCanvas.sceneTransition.blocksInput()
            && !mapCanvas.sceneExitRequirements.blocksInput()
            && !fightAuthoring.blocksInput()
            && !actorDialogAuthoring.blocksInput()
            && !eventAuthoring.blocksInput()
            && !mapCanvas.sceneUseTransition.blocksInput()
            && !mapCanvas.sceneFloorConnect.blocksInput()
            && mapCanvas.confirmMode == SceneMapCanvas::ConfirmMode::None)
        {
            preferences.openDialog(document.resourceDir, document.assetRoot);
        }
    }

    // Options → Configure API keys… (#56).
    if (gEditorApiKeysMenuRequested.exchange(false))
    {
        if (!preferences.blocksInput() && !apiKeysDialog.blocksInput()
            && !variableEditor.open && !itemEditor.blocksInput()
            && mapCanvas.confirmMode == SceneMapCanvas::ConfirmMode::None)
        {
            apiKeysDialog.openDialog();
        }
    }
#endif

    handleShortcuts();

    mapCanvas.externalModalBlocksInput =
        speakCreateDialog.blocksInput() || speakDeleteConfirmOpen;

    if (apiKeysDialog.blocksInput())
    {
        apiKeysDialog.handleInput(screenWidth, screenHeight);
        return;
    }

    if (speakDeleteConfirmOpen)
    {
        if (speakDeleteWaitMouseRelease)
        {
            if (!editorMouseDown(MOUSE_BUTTON_LEFT))
                speakDeleteWaitMouseRelease = false;
            return;
        }
        if (IsKeyPressed(KEY_ESCAPE))
        {
            speakDeleteConfirmOpen = false;
            speakDeletePendingSceneId.clear();
            return;
        }
        // Click handling happens in draw() confirm overlay (same pattern as map confirms).
        return;
    }

    if (speakCreateDialog.blocksInput())
    {
        speakCreateDialog.handleInput(screenWidth, screenHeight);
        return;
    }

    if (parchmentEditor.blocksInput())
    {
        parchmentEditor.handleInput(screenWidth, screenHeight);
        return;
    }

    if (fightAuthoring.blocksInput())
    {
        fightAuthoring.handleInput(screenWidth, screenHeight);
        return;
    }

    if (actorDialogAuthoring.blocksInput())
    {
        actorDialogAuthoring.handleInput(screenWidth, screenHeight);
        return;
    }

    if (eventAuthoring.blocksInput())
    {
        eventAuthoring.handleInput(screenWidth, screenHeight);
        return;
    }

    if (!editorMouseDown(MOUSE_BUTTON_LEFT))
        layout.cancelDividerDrag();

    if (!preferences.blocksInput() && !variableEditor.open && !sceneGraph.stackDialogOpen
        && !itemEditor.blocksInput()
        && !mapCanvas.sceneAuthoring.blocksInput()
        && !mapCanvas.sceneAssist.blocksInput()
        && !mapCanvas.sceneInventory.blocksInput()
        && !mapCanvas.sceneInteractions.blocksInput()
        && !mapCanvas.sceneStoryEvents.blocksInput()
        && !mapCanvas.sceneEffects.blocksInput()
        && !mapCanvas.sceneTransition.blocksInput()
        && !mapCanvas.sceneExitRequirements.blocksInput()
        && !fightAuthoring.blocksInput()
        && !actorDialogAuthoring.blocksInput()
        && !eventAuthoring.blocksInput()
        && !speakCreateDialog.blocksInput()
        && !speakDeleteConfirmOpen
        && !mapCanvas.sceneUseTransition.blocksInput()
        && !mapCanvas.sceneFloorConnect.blocksInput()
        && mapCanvas.confirmMode == SceneMapCanvas::ConfirmMode::None
        && mapCanvas.contextMenuSource == SceneMapCanvas::ContextMenuSource::None)
    {
        const Rectangle left = layout.leftPaneBounds(screenWidth);
        const Rectangle listBounds = {
            left.x,
            left.y + kTabHeight + 4.0f,
            left.width,
            left.height - kTabHeight - 8.0f};
        if (document.isConversationsTab())
        {
            // Reserve bottom strip for Create/Delete Speak CTA.
            Rectangle treeBounds = listBounds;
            treeBounds.height = std::max(40.0f, listBounds.height - 76.0f);
            conversation.handleConversationTreeInput(treeBounds);
        }
        else if (document.isItemsTab())
            itemEditor.handleInput(listBounds);
    }

    // Preferences is topmost when open.
    if (preferences.blocksInput())
    {
        preferences.handleInput(screenWidth, screenHeight);
        return;
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

    // Topmost modal wins input (draw order: authoring → assist → inventory →
    // effects → transition → use transition → prefs).
    if (mapCanvas.sceneFloorConnect.blocksInput())
    {
        mapCanvas.sceneFloorConnect.handleInput(screenWidth, screenHeight);
        return;
    }

    if (mapCanvas.sceneUseTransition.blocksInput())
    {
        mapCanvas.sceneUseTransition.handleInput(screenWidth, screenHeight);
        return;
    }

    if (mapCanvas.sceneTransition.blocksInput())
    {
        mapCanvas.sceneTransition.handleInput(screenWidth, screenHeight);
        return;
    }

    if (mapCanvas.sceneExitRequirements.blocksInput())
    {
        mapCanvas.sceneExitRequirements.handleInput(screenWidth, screenHeight);
        return;
    }

    if (mapCanvas.sceneEffects.blocksInput())
    {
        mapCanvas.sceneEffects.handleInput(screenWidth, screenHeight);
        return;
    }

    if (mapCanvas.sceneStoryEvents.blocksInput())
    {
        mapCanvas.sceneStoryEvents.handleInput(screenWidth, screenHeight);
        return;
    }

    if (mapCanvas.sceneInteractions.blocksInput())
    {
        mapCanvas.sceneInteractions.handleInput(screenWidth, screenHeight);
        return;
    }

    if (mapCanvas.sceneInventory.blocksInput())
    {
        mapCanvas.sceneInventory.handleInput(screenWidth, screenHeight);
        return;
    }

    if (mapCanvas.sceneAssist.blocksInput())
    {
        mapCanvas.sceneAssist.handleInput(screenWidth, screenHeight);
        return;
    }

    if (mapCanvas.sceneAuthoring.blocksInput())
    {
        mapCanvas.sceneAuthoring.handleInput(screenWidth, screenHeight);
        return;
    }

    if (mapCanvas.confirmMode != SceneMapCanvas::ConfirmMode::None
        || mapCanvas.contextMenuSource != SceneMapCanvas::ContextMenuSource::None)
        return;

    if (sceneGraph.stackDialogOpen)
        return;

    if (document.isConversationsTab())
    {
        const Rectangle main = layout.mainPaneBounds(screenWidth);
        const Rectangle canvasBounds = {
            main.x + 4.0f, main.y + 4.0f, main.width - 8.0f, main.height - 8.0f};
        const Rectangle bottom = layout.bottomPaneBounds(screenWidth, screenHeight);
        const DialogBottomSplit split = computeDialogBottomSplit(bottom);
        const bool paneInteract = !layout.isDraggingDivider();
        dialogPalette.handleInput(split.types, paneInteract);
        dialogDetails.handleInput(split.details, paneInteract);
        dialogFlow.handleInput(canvasBounds, paneInteract);
        dialogMedia.handleInput(split.media, paneInteract);
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
    mapCanvas.externalModalBlocksInput =
        speakCreateDialog.blocksInput() || speakDeleteConfirmOpen;

    // Modals that must paint inside mapCanvas BeginDrawing/EndDrawing are
    // drawn via mapCanvas callbacks / late hooks below through mapCanvas.draw.
    mapCanvas.drawSpeakCreateOverlay = [this](int screenW, int screenH) {
        if (speakCreateDialog.blocksInput())
            speakCreateDialog.draw(screenW, screenH);
        if (!speakDeleteConfirmOpen)
            return;

        const Font font = textFont();
        const Font bold = boldFont();
        DrawRectangle(0, 0, screenW, screenH, kModalOverlay);
        const float dlgW = 420.0f;
        const float dlgH = 160.0f;
        const Rectangle dlg = {
            (static_cast<float>(screenW) - dlgW) * 0.5f,
            (static_cast<float>(screenH) - dlgH) * 0.5f,
            dlgW,
            dlgH};
        DrawRectangleRounded(dlg, 0.03f, 8, kModalFill);
        DrawRectangleLinesEx(dlg, 2.0f, kPanelBorder);
        DrawTextEx(
            bold,
            "Delete Speak conversation?",
            {dlg.x + 20.0f, dlg.y + 18.0f},
            kFontHeading,
            1.0f,
            kTextPrimary);
        const std::string msg =
            "Remove conversations.json entry for \"" + speakDeletePendingSceneId
            + "\"? (unsaved until Save / quit)";
        drawWrappedText(
            font, msg, {dlg.x + 20.0f, dlg.y + 52.0f}, dlgW - 40.0f, kFontBody, 4.0f,
            kTextMuted);

        const Rectangle yesBtn = {dlg.x + dlgW - 240.0f, dlg.y + dlgH - 48.0f, 100.0f, 32.0f};
        const Rectangle noBtn = {dlg.x + dlgW - 120.0f, dlg.y + dlgH - 48.0f, 100.0f, 32.0f};
        DrawRectangleRec(yesBtn, Color{140, 50, 60, 255});
        DrawRectangleLinesEx(yesBtn, 1.0f, kPanelBorder);
        DrawTextEx(bold, "Delete", {yesBtn.x + 24.0f, yesBtn.y + 7.0f}, kFontBody, 1.0f, kTextPrimary);
        DrawRectangleRec(noBtn, Color{36, 32, 44, 255});
        DrawRectangleLinesEx(noBtn, 1.0f, kPanelBorder);
        DrawTextEx(font, "Cancel", {noBtn.x + 24.0f, noBtn.y + 7.0f}, kFontBody, 1.0f, kTextPrimary);

        if (speakDeleteWaitMouseRelease)
            return;
        if (editorMousePressed(MOUSE_BUTTON_LEFT))
        {
            const Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, yesBtn))
            {
                const std::string id = speakDeletePendingSceneId;
                speakDeleteConfirmOpen = false;
                speakDeletePendingSceneId.clear();
                deleteSpeakConversationForScene(id);
            }
            else if (CheckCollisionPointRec(mouse, noBtn)
                     || !CheckCollisionPointRec(mouse, dlg))
            {
                speakDeleteConfirmOpen = false;
                speakDeletePendingSceneId.clear();
            }
        }
    };
    mapCanvas.draw();
}

} // namespace timberline_editor
