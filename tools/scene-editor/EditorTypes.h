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

#ifndef TIMBERLINE_EDITOR_TYPES_H
#define TIMBERLINE_EDITOR_TYPES_H
#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_editor
{

enum class DragSource
{
    None,
    SceneList,
    Canvas,
    ExitLink
};
enum class ConversationNodeKind
{
    Section,   // virtual group (e.g. Main Character, Actors)
    Actor,
    Milestone,
    Dialog,
    Narrative  // scene description / examine / speak text for the PC
};
enum class ConversationEditDoc
{
    None,
    Conversations,
    Scenes,
    Items
};
struct ConversationTreeNode
{
    ConversationNodeKind kind = ConversationNodeKind::Actor;
    std::string key;          // stable expand/select id
    std::string label;
    std::string detail;       // secondary text (scene, type, etc.)
    ConversationEditDoc editDoc = ConversationEditDoc::None;
    std::string jsonPointer;  // conversations root pointer, or path under a scene object
    std::string editSceneId;  // when editDoc == Scenes
    std::vector<ConversationTreeNode> children;
};
struct ConversationVisibleRow
{
    const ConversationTreeNode* node = nullptr;
    int depth = 0;
    bool isLastChild = false;
    std::vector<bool> ancestorContinues; // true = draw vertical line past this depth
};
struct EditorVisualLine
{
    int start = 0; // buffer index of first char on this visual line
    int end = 0;   // buffer index one past last drawn char (may point at '\n')
    std::string text;
};
struct ThumbnailEntry
{
    Texture2D texture{};
    bool loaded = false;
    bool missing = false;
};
const float kTreeRowHeight = 24.0f;
const float kTreeIndent = 18.0f;
const float kTreeToggleSize = 14.0f;
const float kTreeTogglePad = 4.0f;

} // namespace timberline_editor

#endif
