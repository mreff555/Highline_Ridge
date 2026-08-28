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

#ifndef TIMBERLINE_CONVERSATION_TREE_H
#define TIMBERLINE_CONVERSATION_TREE_H

#include "DocumentWorkspace.h"
#include "EditorTypes.h"
#include "VariableEditor.h"

#include <functional>
#include <set>
#include <string>
#include <vector>
#include <raylib.h>

namespace timberline_editor
{

struct DialogWalkthrough;

struct ConversationTree
{
    std::vector<ConversationTreeNode> roots;
    std::set<std::string> expanded;
    std::string selectedKey;
    mutable std::vector<ConversationVisibleRow> visibleRowsCache;
    mutable bool visibleRowsDirty = true;
    Rectangle listBounds{0, 0, 0, 0};
    bool listBoundsValid = false;

    DocumentWorkspace* docs = nullptr;
    VariableEditor* text = nullptr;
    DialogWalkthrough* walkthrough = nullptr;
    std::string* selectionSceneId = nullptr;
    float* leftScroll = nullptr;
    bool* stackDialogOpen = nullptr;
    std::function<bool()> draggingDivider;
    /** Called when the user picks a scene root in the conversations tree. */
    std::function<void(const std::string& sceneId)> onSelectScene;
    Font uiFont{};
    Font uiFontBold{};


ConversationTreeNode makeNarrativeFieldNode(
    const std::string& sceneId,
    const std::string& label,
    const std::string& pointerUnderScene,
    const nlohmann::json& value) const;

void appendNarrativeFieldsFromObject(
    ConversationTreeNode& parent,
    const std::string& sceneId,
    const nlohmann::json& object,
    const std::string& pointerPrefix) const;

void invalidateConversationVisibleRows();

void rebuildConversationTree();

/** Build Main Character + Actors subtrees for one scene id. */
void appendSceneConversationContent(
    ConversationTreeNode& sceneRoot,
    const std::string& sceneId);

void stampConversationEditDoc(ConversationTreeNode& node);

bool isConversationExpanded(const std::string& key) const;

void toggleConversationExpanded(const std::string& key);

bool allSceneRootsExpanded() const;
void expandAllSceneRoots();
void collapseAllSceneRoots();
void toggleExpandAllSceneRoots();

/** Parse scene id from tree key "scene:<id>" (empty if not a scene root). */
static std::string sceneIdFromTreeKey(const std::string& key);

void collectVisibleConversationRows(
    const ConversationTreeNode& node,
    int depth,
    bool isLastChild,
    std::vector<bool> ancestorContinues,
    std::vector<ConversationVisibleRow>& out) const;

const std::vector<ConversationVisibleRow>& visibleConversationRows() const;

void handleConversationTreeInput(Rectangle listBounds);

void drawConversationTree(Rectangle listBounds);

void openConversationNodeEditor(const ConversationTreeNode& node);

static std::string truncateForTree(const std::string& text, size_t maxLen);
};

} // namespace timberline_editor
#endif
