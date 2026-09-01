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

#ifndef TIMBERLINE_SCENE_EDITOR_APP_H
#define TIMBERLINE_SCENE_EDITOR_APP_H

#include "ConversationTree.h"
#include "DialogWalkthrough.h"
#include "DocumentWorkspace.h"
#include "EditorLayout.h"
#include "EditorPreferencesDialog.h"
#include "ItemEditor.h"
#include "SceneGraphModel.h"
#include "SceneMapCanvas.h"
#include "ThumbnailCache.h"
#include "VariableEditor.h"

#include <raylib.h>

#include <string>

namespace timberline_editor
{

// Shell: wires modules, owns session selection, runs the frame loop.
struct SceneEditorApp
{
    DocumentWorkspace document;
    EditorLayout layout;
    ThumbnailCache thumbnails;
    VariableEditor variableEditor;
    ConversationTree conversation;
    DialogWalkthrough dialogWalkthrough;
    ItemEditor itemEditor;
    SceneGraphModel sceneGraph;
    SceneMapCanvas mapCanvas;
    EditorPreferencesDialog preferences;

    std::string selectedSceneId;
    float variablesScroll = 0.0f;

    Font uiFont{};
    Font uiFontBold{};

    SceneEditorApp();

    Font textFont() const;
    Font boldFont() const;
    Font tryLoadFont(const std::string candidates[], size_t count) const;
    void loadUiFont();
    void unloadUiFont();

    void wireModules();
    void syncModuleFonts();

    bool loadActiveDocument();
    void selectSceneForEditor(const std::string& id);
    bool deleteSelectedScene();
    bool saveDocument();
    void markDirty();
    void unloadThumbnails();

    // Path aliases used by main / tooling.
    std::string& resourceDir() { return document.resourceDir; }
    std::string& assetRoot() { return document.assetRoot; }
    bool& dirty() { return document.dirty; }
    bool dirty() const { return document.dirty; }

    void handleShortcuts();
    void update();
    void draw();
};

} // namespace timberline_editor

#endif /* TIMBERLINE_SCENE_EDITOR_APP_H */
