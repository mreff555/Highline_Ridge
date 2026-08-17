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

#include "ConversationTree.h"

#include "ConversationHelpers.h"
#include "DialogWalkthrough.h"
#include "DocumentWorkspace.h"
#include "EditorPaths.h"
#include "EditorTheme.h"
#include "EditorTypes.h"
#include "EditorUiDraw.h"
#include "ImageCompression.h"
#include "PlatformPath.h"
#include "RaylibCompat.h"
#include "SceneDocument.h"
#include "ThumbnailCache.h"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <filesystem>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using timberline_engine::SceneActor;
using timberline_engine::SceneDocument;
using timberline_engine::SceneLayout;
using timberline_engine::buildAssetSearchPaths;
using timberline_engine::compressedAssetPath;
using timberline_engine::listDirectoryFileNames;
using timberline_engine::loadTextureFromAssetFile;
using timberline_engine::pathJoin;

namespace fs = std::filesystem;

namespace timberline_editor
{
#include "VariableEditor.h"


ConversationTreeNode ConversationTree::makeNarrativeFieldNode(
    const std::string& sceneId,
    const std::string& label,
    const std::string& pointerUnderScene,
    const nlohmann::json& value) const
{
    ConversationTreeNode node;
    node.kind = ConversationNodeKind::Narrative;
    node.key = "narrative:" + sceneId + ":" + pointerUnderScene;
    node.label = label;
    node.editDoc = ConversationEditDoc::Scenes;
    node.editSceneId = sceneId;
    node.jsonPointer = pointerUnderScene;
    if (value.is_string())
    {
        const std::string text = value.get<std::string>();
        node.detail = text.empty() ? "(empty)" : truncateForTree(text, 40);
    }
    else if (value.is_null())
        node.detail = "(null)";
    else
        node.detail = "{…}";
    return node;
}


void ConversationTree::appendNarrativeFieldsFromObject(
    ConversationTreeNode& parent,
    const std::string& sceneId,
    const nlohmann::json& object,
    const std::string& pointerPrefix) const
{
    static const char* kNarrativeKeys[] = {
        "description",
        "examineDetails",
        "speakDetails",
        "useDetails",
        "wakeNarrative"
    };
    static const char* kNarrativeLabels[] = {
        "Description",
        "Examine",
        "Speak",
        "Use",
        "Wake"
    };

    for (size_t i = 0; i < sizeof(kNarrativeKeys) / sizeof(kNarrativeKeys[0]); ++i)
    {
        const char* key = kNarrativeKeys[i];
        if (!object.contains(key))
            continue;
        const nlohmann::json& value = object[key];
        if (!value.is_string() && !value.is_null())
            continue;
        const std::string pointer = pointerPrefix.empty()
            ? std::string("/") + key
            : conversationPointerJoin(pointerPrefix, key);
        parent.children.push_back(makeNarrativeFieldNode(
            sceneId,
            kNarrativeLabels[i],
            pointer,
            value));
    }
}


void ConversationTree::invalidateConversationVisibleRows()
{
    visibleRowsDirty = true;
}


std::string ConversationTree::sceneIdFromTreeKey(const std::string& key)
{
    const std::string prefix = "scene:";
    if (key.rfind(prefix, 0) != 0)
        return "";
    return key.substr(prefix.size());
}

void ConversationTree::appendSceneConversationContent(
    ConversationTreeNode& sceneRoot,
    const std::string& sceneId)
{
    // --- Main character (scene narrative from scenes.json) ---
    ConversationTreeNode mainCharacter;
    mainCharacter.kind = ConversationNodeKind::Section;
    mainCharacter.key = "section:main_character:" + sceneId;
    mainCharacter.label = "Main Character";
    mainCharacter.detail = "scene narrative";

    if (docs->scenes.isLoaded())
    {
        const nlohmann::json* scene = docs->scenes.sceneJson(sceneId);
        if (scene != nullptr && scene->is_object())
        {
            appendNarrativeFieldsFromObject(mainCharacter, sceneId, *scene, "");

            if (scene->contains("subScenes") && (*scene)["subScenes"].is_object())
            {
                const nlohmann::json& subScenes = (*scene)["subScenes"];
                std::vector<std::string> subIds;
                for (auto it = subScenes.begin(); it != subScenes.end(); ++it)
                    subIds.push_back(it.key());
                std::sort(subIds.begin(), subIds.end());

                for (const std::string& subId : subIds)
                {
                    if (!subScenes[subId].is_object())
                        continue;
                    ConversationTreeNode subNode;
                    subNode.kind = ConversationNodeKind::Section;
                    subNode.key = "section:subscene:" + sceneId + ":" + subId;
                    subNode.label = "Sub-scene: " + subId;
                    subNode.detail = "focus / variant";
                    appendNarrativeFieldsFromObject(
                        subNode,
                        sceneId,
                        subScenes[subId],
                        conversationPointerJoin("/subScenes", subId));
                    if (!subNode.children.empty())
                        mainCharacter.children.push_back(std::move(subNode));
                }
            }
        }
    }

    if (!mainCharacter.children.empty())
        sceneRoot.children.push_back(std::move(mainCharacter));

    // --- Actor conversations (conversations.json) ---
    if (!docs->conversationsLoaded || !docs->conversationsRoot.is_object()
        || !docs->conversationsRoot.contains(sceneId)
        || !docs->conversationsRoot[sceneId].is_object())
        return;

    const nlohmann::json& sceneNode = docs->conversationsRoot[sceneId];
    const std::string scenePointer = conversationPointerJoin("", sceneId);
    if (!sceneNode.contains("speakPhases") || !sceneNode["speakPhases"].is_array())
        return;

    std::map<std::string, ConversationTreeNode> actorsById;
    const nlohmann::json& phases = sceneNode["speakPhases"];
    for (size_t phaseIndex = 0; phaseIndex < phases.size(); ++phaseIndex)
    {
        const nlohmann::json& phase = phases[phaseIndex];
        if (!phase.is_object())
            continue;

        const std::string actorId = phaseActorId(phase);
        const std::string actorName = phaseActorName(phase, actorId);
        const std::string phasePointer = conversationPointerIndex(
            conversationPointerJoin(scenePointer, "speakPhases"), phaseIndex);

        if (actorsById.find(actorId) == actorsById.end())
        {
            ConversationTreeNode actor;
            actor.kind = ConversationNodeKind::Actor;
            actor.key = "actor:" + sceneId + ":" + actorId;
            actor.label = actorName;
            actor.detail = actorId;
            actorsById[actorId] = actor;
        }
        else if (
            actorsById[actorId].label == actorsById[actorId].detail
            && actorName != actorId)
        {
            actorsById[actorId].label = actorName;
        }

        ConversationTreeNode milestone;
        milestone.kind = ConversationNodeKind::Milestone;
        milestone.key = "phase:" + phasePointer;
        milestone.editDoc = ConversationEditDoc::Conversations;
        milestone.jsonPointer = phasePointer;
        if (phase.contains("id") && phase["id"].is_string())
            milestone.label = phase["id"].get<std::string>();
        else
            milestone.label = "(unnamed phase)";
        milestone.detail = phase.value("type", "once");

        {
            const char* phaseKeys[] = {"intro", "resumeIntro", "text"};
            const char* phaseLabels[] = {"Intro", "Resume intro", "Text"};
            for (size_t ki = 0; ki < 3; ++ki)
            {
                if (!phase.contains(phaseKeys[ki]) || !phase[phaseKeys[ki]].is_string())
                    continue;
                ConversationTreeNode n;
                n.kind = ConversationNodeKind::Narrative;
                n.key = "narrative-conv:" + phasePointer + "/" + phaseKeys[ki];
                n.label = phaseLabels[ki];
                n.editDoc = ConversationEditDoc::Conversations;
                n.jsonPointer = conversationPointerJoin(phasePointer, phaseKeys[ki]);
                const std::string text = phase[phaseKeys[ki]].get<std::string>();
                n.detail = text.empty() ? "(empty)" : truncateForTree(text, 40);
                milestone.children.push_back(std::move(n));
            }
        }

        if (phase.contains("choices") && phase["choices"].is_array())
        {
            const nlohmann::json& choices = phase["choices"];
            for (size_t i = 0; i < choices.size(); ++i)
            {
                if (!choices[i].is_object())
                    continue;
                ConversationTreeNode choiceNode = buildChoiceTreeNode(
                    choices[i],
                    conversationPointerIndex(
                        conversationPointerJoin(phasePointer, "choices"), i));
                stampConversationEditDoc(choiceNode);
                milestone.children.push_back(std::move(choiceNode));
            }
        }

        if (phase.contains("lines") && phase["lines"].is_array())
        {
            const nlohmann::json& lines = phase["lines"];
            for (size_t i = 0; i < lines.size(); ++i)
            {
                if (!lines[i].is_object())
                    continue;
                ConversationTreeNode lineNode = buildChoiceTreeNode(
                    lines[i],
                    conversationPointerIndex(
                        conversationPointerJoin(phasePointer, "lines"), i));
                if (lineNode.label == "(dialog)")
                {
                    if (lines[i].contains("id") && lines[i]["id"].is_string())
                        lineNode.label = lines[i]["id"].get<std::string>();
                    else
                        lineNode.label = "line " + std::to_string(i);
                }
                stampConversationEditDoc(lineNode);
                milestone.children.push_back(std::move(lineNode));
            }
        }

        stampConversationEditDoc(milestone);
        actorsById[actorId].children.push_back(std::move(milestone));
    }

    ConversationTreeNode actorsSection;
    actorsSection.kind = ConversationNodeKind::Section;
    actorsSection.key = "section:actors:" + sceneId;
    actorsSection.label = "Dialog / Actors";
    actorsSection.detail = "speakPhases";

    for (auto& entry : actorsById)
    {
        std::sort(
            entry.second.children.begin(),
            entry.second.children.end(),
            [](const ConversationTreeNode& a, const ConversationTreeNode& b)
            {
                if (a.label != b.label)
                    return a.label < b.label;
                return a.detail < b.detail;
            });
        actorsSection.children.push_back(std::move(entry.second));
    }

    std::sort(
        actorsSection.children.begin(),
        actorsSection.children.end(),
        [](const ConversationTreeNode& a, const ConversationTreeNode& b)
        {
            if (a.label != b.label)
                return a.label < b.label;
            return a.detail < b.detail;
        });

    if (!actorsSection.children.empty())
        sceneRoot.children.push_back(std::move(actorsSection));
}

void ConversationTree::rebuildConversationTree()
{
    roots.clear();
    invalidateConversationVisibleRows();
    if (docs == nullptr)
        return;

    // Top level = every scene that has conversation speakPhases (items-tab style).
    std::vector<std::string> sceneIds;
    if (docs->conversationsLoaded && docs->conversationsRoot.is_object())
    {
        for (auto it = docs->conversationsRoot.begin(); it != docs->conversationsRoot.end();
             ++it)
        {
            if (!it.value().is_object())
                continue;
            const nlohmann::json& node = it.value();
            if (!node.contains("speakPhases") || !node["speakPhases"].is_array()
                || node["speakPhases"].empty())
                continue;
            sceneIds.push_back(it.key());
        }
    }
    std::sort(sceneIds.begin(), sceneIds.end());

    for (const std::string& sceneId : sceneIds)
    {
        ConversationTreeNode sceneRoot;
        sceneRoot.kind = ConversationNodeKind::Section;
        sceneRoot.key = "scene:" + sceneId;
        sceneRoot.label = sceneId;
        sceneRoot.detail = "conversation scene";
        sceneRoot.editSceneId = sceneId; // reuse field for scene id
        appendSceneConversationContent(sceneRoot, sceneId);
        roots.push_back(std::move(sceneRoot));
    }
}


void ConversationTree::stampConversationEditDoc(ConversationTreeNode& node)
{
    node.editDoc = ConversationEditDoc::Conversations;
    for (ConversationTreeNode& child : node.children)
        stampConversationEditDoc(child);
}


bool ConversationTree::isConversationExpanded(const std::string& key) const
{
    return expanded.count(key) > 0;
}


bool ConversationTree::allSceneRootsExpanded() const
{
    if (roots.empty())
        return false;
    for (const ConversationTreeNode& root : roots)
    {
        if (root.children.empty())
            continue;
        if (!isConversationExpanded(root.key))
            return false;
    }
    return true;
}

void ConversationTree::expandAllSceneRoots()
{
    for (const ConversationTreeNode& root : roots)
    {
        if (!root.children.empty())
            expanded.insert(root.key);
    }
    invalidateConversationVisibleRows();
}

void ConversationTree::collapseAllSceneRoots()
{
    for (const ConversationTreeNode& root : roots)
        expanded.erase(root.key);
    invalidateConversationVisibleRows();
}

void ConversationTree::toggleExpandAllSceneRoots()
{
    if (allSceneRootsExpanded())
        collapseAllSceneRoots();
    else
        expandAllSceneRoots();
}

void ConversationTree::toggleConversationExpanded(const std::string& key)
{
    if (expanded.count(key) > 0)
        expanded.erase(key);
    else
        expanded.insert(key);
    invalidateConversationVisibleRows();
}


void ConversationTree::collectVisibleConversationRows(
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

    if (!node.children.empty() && isConversationExpanded(node.key))
    {
        std::vector<bool> childAncestors = ancestorContinues;
        childAncestors.push_back(!isLastChild);
        for (size_t i = 0; i < node.children.size(); ++i)
        {
            const bool childLast = (i + 1 == node.children.size());
            collectVisibleConversationRows(
                node.children[i],
                depth + 1,
                childLast,
                childAncestors,
                out);
        }
    }
}


const std::vector<ConversationVisibleRow>& ConversationTree::visibleConversationRows() const
{
    if (!visibleRowsDirty)
        return visibleRowsCache;

    visibleRowsCache.clear();
    for (size_t i = 0; i < roots.size(); ++i)
    {
        const bool last = (i + 1 == roots.size());
        collectVisibleConversationRows(
            roots[i],
            0,
            last,
            {},
            visibleRowsCache);
    }
    visibleRowsDirty = false;
    return visibleRowsCache;
}


void ConversationTree::handleConversationTreeInput(Rectangle listBounds)
{
    listBounds = listBounds;
    listBoundsValid = true;

    if (docs == nullptr || !docs->conversationsLoaded || roots.empty())
        return;
    if ((stackDialogOpen && *stackDialogOpen) || (text && text->open)
        || (draggingDivider && draggingDivider()))
        return;

    const Vector2 mouse = GetMousePosition();

    // Header expand/collapse-all (scene roots), same as items tab.
    const float headerToggleX = listBounds.x + 8.0f;
    const float headerToggleY = listBounds.y + (20.0f - kTreeToggleSize) * 0.5f;
    const Rectangle headerToggle = {
        headerToggleX, headerToggleY, kTreeToggleSize, kTreeToggleSize};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
        && CheckCollisionPointRec(mouse, headerToggle))
    {
        toggleExpandAllSceneRoots();
        return;
    }

    const std::vector<ConversationVisibleRow>& rows = visibleConversationRows();
    const float contentHeight = static_cast<float>(rows.size()) * kTreeRowHeight + 8.0f;
    const float maxScroll = std::max(0.0f, contentHeight - (listBounds.height - 20.0f));
    if ((*leftScroll) > maxScroll)
        (*leftScroll) = maxScroll;

    const Rectangle treeBounds = {
        listBounds.x,
        listBounds.y + 20.0f,
        listBounds.width,
        listBounds.height - 20.0f};

    if (CheckCollisionPointRec(mouse, treeBounds) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        const float localY = (mouse.y - treeBounds.y - 4.0f) + (*leftScroll);
        if (localY >= 0.0f)
        {
            const int index = static_cast<int>(localY / kTreeRowHeight);
            if (index >= 0 && index < static_cast<int>(rows.size())
                && rows[static_cast<size_t>(index)].node != nullptr)
            {
                const ConversationVisibleRow& hit = rows[static_cast<size_t>(index)];
                const ConversationTreeNode& node = *hit.node;
                selectedKey = node.key;

                const float rowTop = treeBounds.y + 4.0f - (*leftScroll)
                    + static_cast<float>(index) * kTreeRowHeight;
                const float toggleX =
                    treeBounds.x + 8.0f + static_cast<float>(hit.depth) * kTreeIndent;
                const float midY = rowTop + kTreeRowHeight * 0.5f;
                const Rectangle toggleBounds = {
                    toggleX,
                    midY - kTreeToggleSize * 0.5f,
                    kTreeToggleSize,
                    kTreeToggleSize};

                // Scene root: select scene for walkthrough + expand/collapse.
                const std::string sceneFromKey = sceneIdFromTreeKey(node.key);
                if (!sceneFromKey.empty())
                {
                    if (onSelectScene)
                        onSelectScene(sceneFromKey);
                    if (!node.children.empty() && CheckCollisionPointRec(mouse, toggleBounds))
                        toggleConversationExpanded(node.key);
                    else if (!node.children.empty() && !isConversationExpanded(node.key))
                        toggleConversationExpanded(node.key);
                    return;
                }

                // Nested under a scene: ensure parent scene is active first.
                if (hit.depth > 0 && selectionSceneId != nullptr)
                {
                    // Walk is not available; parse scene from known key prefixes.
                    std::string sceneId;
                    if (node.key.rfind("actor:", 0) == 0)
                    {
                        // actor:<scene>:<actorId>
                        const size_t a = node.key.find(':', 6);
                        if (a != std::string::npos)
                            sceneId = node.key.substr(6, a - 6);
                    }
                    else if (node.key.rfind("section:main_character:", 0) == 0)
                        sceneId = node.key.substr(std::string("section:main_character:").size());
                    else if (node.key.rfind("section:actors:", 0) == 0)
                        sceneId = node.key.substr(std::string("section:actors:").size());
                    else if (node.key.rfind("section:subscene:", 0) == 0)
                    {
                        const std::string rest =
                            node.key.substr(std::string("section:subscene:").size());
                        const size_t colon = rest.find(':');
                        if (colon != std::string::npos)
                            sceneId = rest.substr(0, colon);
                    }
                    else if (node.key.rfind("phase:/", 0) == 0)
                    {
                        // phase:/sceneId/speakPhases/...
                        const std::string rest = node.key.substr(std::string("phase:/").size());
                        const size_t slash = rest.find('/');
                        if (slash != std::string::npos)
                            sceneId = rest.substr(0, slash);
                    }
                    else if (node.key.rfind("narrative-conv:/", 0) == 0)
                    {
                        const std::string rest =
                            node.key.substr(std::string("narrative-conv:/").size());
                        const size_t slash = rest.find('/');
                        if (slash != std::string::npos)
                            sceneId = rest.substr(0, slash);
                    }
                    else if (node.key.rfind("choice:/", 0) == 0)
                    {
                        const std::string rest = node.key.substr(std::string("choice:/").size());
                        const size_t slash = rest.find('/');
                        if (slash != std::string::npos)
                            sceneId = rest.substr(0, slash);
                    }
                    if (!sceneId.empty() && onSelectScene
                        && (selectionSceneId->empty() || *selectionSceneId != sceneId))
                        onSelectScene(sceneId);
                }

                if (!node.children.empty() && CheckCollisionPointRec(mouse, toggleBounds))
                    toggleConversationExpanded(node.key);
                else if (
                    walkthrough != nullptr
                    && (node.kind == ConversationNodeKind::Dialog
                        || node.kind == ConversationNodeKind::Narrative
                        || node.kind == ConversationNodeKind::Milestone)
                    && walkthrough->selectTreeKey(node.key))
                {
                    selectedKey = node.key;
                }
                else if (node.editDoc != ConversationEditDoc::None && !node.jsonPointer.empty())
                    openConversationNodeEditor(node);
                else if (!node.children.empty())
                    toggleConversationExpanded(node.key);
            }
        }
    }

    if (CheckCollisionPointRec(mouse, treeBounds))
        (*leftScroll) -= GetMouseWheelMove() * 24.0f;
    if ((*leftScroll) < 0.0f)
        (*leftScroll) = 0.0f;
    if ((*leftScroll) > maxScroll)
        (*leftScroll) = maxScroll;
}


void ConversationTree::drawConversationTree(Rectangle listBounds)
{
    listBounds = listBounds;
    listBoundsValid = true;

    if (docs == nullptr || !docs->conversationsLoaded)
    {
        const std::string message = docs == nullptr || docs->loadError.empty()
            ? "Loading conversations..."
            : docs->loadError;
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
            "No scenes with speakPhases in conversations.json",
            {listBounds.x + 12.0f, listBounds.y + 12.0f},
            kFontBody,
            1.0f,
            kTextMuted);
        return;
    }

    const std::vector<ConversationVisibleRow>& rows = visibleConversationRows();
    const float contentHeight = static_cast<float>(rows.size()) * kTreeRowHeight + 8.0f;
    const float maxScroll = std::max(0.0f, contentHeight - (listBounds.height - 20.0f));
    if ((*leftScroll) > maxScroll)
        (*leftScroll) = maxScroll;

    const Vector2 mouse = GetMousePosition();
    const bool canInteract =
        !(stackDialogOpen != nullptr && *stackDialogOpen) &&
        !(text != nullptr && text->open) &&
        !(draggingDivider && draggingDivider());

    // Header: [+/-] Scenes (N) — expand/collapse all scene roots (items-tab pattern).
    const float headerToggleX = listBounds.x + 8.0f;
    const float headerToggleY = listBounds.y + (20.0f - kTreeToggleSize) * 0.5f;
    const Rectangle headerToggle = {
        headerToggleX, headerToggleY, kTreeToggleSize, kTreeToggleSize};
    const bool allOpen = allSceneRootsExpanded();
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

    const std::string headerLabel =
        "Scenes (" + std::to_string(roots.size()) + ")"
        + (selectionSceneId != nullptr && !selectionSceneId->empty()
               ? ("  ·  " + truncateForTree(*selectionSceneId, 22))
               : "");
    DrawTextEx(
        (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
        headerLabel.c_str(),
        {headerToggle.x + kTreeToggleSize + kTreeTogglePad + 2.0f, listBounds.y + 4.0f},
        kFontTiny,
        1.0f,
        kPanelBorder);

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

    float y = treeBounds.y + 4.0f - (*leftScroll);
    for (const ConversationVisibleRow& row : rows)
    {
        if (row.node == nullptr)
            continue;

        const ConversationTreeNode& node = *row.node;
        const float rowTop = y;
        const Rectangle rowBounds = {
            treeBounds.x + 2.0f,
            rowTop,
            treeBounds.width - 4.0f,
            kTreeRowHeight - 1.0f};

        const bool isSceneRoot = !sceneIdFromTreeKey(node.key).empty();
        const bool selected = node.key == selectedKey
            || (isSceneRoot && selectionSceneId != nullptr
                && *selectionSceneId == sceneIdFromTreeKey(node.key));
        const bool hovered =
            canInteract &&
            CheckCollisionPointRec(mouse, treeBounds) &&
            CheckCollisionPointRec(mouse, rowBounds);

        if (selected)
            DrawRectangleRec(rowBounds, kSelection);
        else if (hovered)
            DrawRectangleRec(rowBounds, Color{60, 54, 72, 180});

        // Indent guides / tree lines
        const float baseX = treeBounds.x + 8.0f;
        const Color lineColor = {96, 86, 72, 220};
        for (int d = 0; d < row.depth; ++d)
        {
            if (d >= static_cast<int>(row.ancestorContinues.size()))
                break;
            if (!row.ancestorContinues[static_cast<size_t>(d)])
                continue;
            const float guideX = baseX + static_cast<float>(d) * kTreeIndent + kTreeToggleSize * 0.5f;
            DrawLineEx(
                {guideX, rowTop},
                {guideX, rowTop + kTreeRowHeight},
                1.0f,
                lineColor);
        }

        const float toggleX = baseX + static_cast<float>(row.depth) * kTreeIndent;
        const float midY = rowTop + kTreeRowHeight * 0.5f;
        if (row.depth > 0)
        {
            const float parentGuideX =
                baseX + static_cast<float>(row.depth - 1) * kTreeIndent + kTreeToggleSize * 0.5f;
            const float elbowY = midY;
            DrawLineEx(
                {parentGuideX, rowTop},
                {parentGuideX, elbowY},
                1.0f,
                lineColor);
            DrawLineEx(
                {parentGuideX, elbowY},
                {toggleX + kTreeToggleSize * 0.5f, elbowY},
                1.0f,
                lineColor);
        }

        const bool hasChildren = !node.children.empty();
        const Rectangle toggleBounds = {
            toggleX,
            midY - kTreeToggleSize * 0.5f,
            kTreeToggleSize,
            kTreeToggleSize};

        if (hasChildren)
        {
            DrawRectangleRec(toggleBounds, Color{40, 36, 48, 255});
            DrawRectangleLinesEx(toggleBounds, 1.0f, kPanelBorder);
            const bool expanded = isConversationExpanded(node.key);
            const char* glyph = expanded ? "-" : "+";
            const float glyphW = text->measureUiTextWidth(glyph, kFontSmall);
            const float glyphH = kFontSmall;
            DrawTextEx(
                (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
                glyph,
                {
                    toggleBounds.x + (toggleBounds.width - glyphW) * 0.5f,
                    toggleBounds.y + (toggleBounds.height - glyphH) * 0.5f - 1.0f
                },
                kFontSmall,
                1.0f,
                kTextPrimary);
        }
        else
        {
            DrawCircleV({toggleX + kTreeToggleSize * 0.5f, midY}, 2.0f, lineColor);
        }

        const float textX = toggleX + kTreeToggleSize + kTreeTogglePad + 2.0f;
        Color labelColor = kTextPrimary;
        if (node.kind == ConversationNodeKind::Section)
            labelColor = kPanelBorder;
        else if (node.kind == ConversationNodeKind::Actor)
            labelColor = Color{200, 180, 120, 255};
        else if (node.kind == ConversationNodeKind::Milestone)
            labelColor = kTextPrimary;
        else if (node.kind == ConversationNodeKind::Narrative)
            labelColor = Color{180, 200, 190, 255};
        else
            labelColor = kTextMuted;

        const std::string display = truncateForTree(node.label, 48);
        DrawTextEx(
            (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
            display.c_str(),
            {textX, rowTop + 4.0f},
            kFontSmall,
            1.0f,
            labelColor);

        if (!node.detail.empty() && node.kind != ConversationNodeKind::Dialog)
        {
            const float labelW = text->measureUiTextWidth(display, kFontSmall);
            const std::string detail = truncateForTree(node.detail, 36);
            DrawTextEx(
                (uiFont.texture.id != 0 ? uiFont : GetFontDefault()),
                detail.c_str(),
                {textX + labelW + 8.0f, rowTop + 5.0f},
                kFontTiny,
                1.0f,
                kTextMuted);
        }

        y += kTreeRowHeight;
    }

    EndScissorMode();
}


void ConversationTree::openConversationNodeEditor(const ConversationTreeNode& node)
{
    if (node.editDoc == ConversationEditDoc::None || node.jsonPointer.empty())
        return;

    const nlohmann::json* value = nullptr;
    if (node.editDoc == ConversationEditDoc::Conversations)
        value = docs->conversationJsonAt(node.jsonPointer);
    else if (node.editDoc == ConversationEditDoc::Scenes)
        value = docs->sceneFieldAt(node.editSceneId, node.jsonPointer);

    if (value == nullptr)
    {
        TraceLog(
            LOG_WARNING,
            "TIMBERLINE: edit path missing %s",
            node.jsonPointer.c_str());
        return;
    }

    text->docTarget = node.editDoc;
    text->jsonPointer = node.jsonPointer;
    text->editorSceneId = node.editSceneId;
    text->editorItemId.clear();
    text->editorKey = node.label;
    text->scrollY = 0.0f;
    text->error.clear();
    selectedKey = node.key;
    TraceLog(LOG_INFO, "TIMBERLINE: editing %s", node.jsonPointer.c_str());

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


std::string ConversationTree::truncateForTree(const std::string& text, size_t maxLen)
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
        lastSpace = (ch == ' ');
    }
    if (compact.size() <= maxLen)
        return compact;
    return compact.substr(0, maxLen - 1) + "…";
}
} // namespace timberline_editor
