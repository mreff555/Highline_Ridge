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

#ifndef TIMBERLINE_SCENE_GRAPH_MODEL_H
#define TIMBERLINE_SCENE_GRAPH_MODEL_H

#include "DocumentWorkspace.h"
#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_editor
{

struct SceneMapCanvas; // circular

struct SceneGraphModel
{
    bool stackDialogOpen = false;
    std::string stackSourceId;
    std::string stackTargetId;
    float stackPendingX = 0.0f;
    float stackPendingY = 0.0f;

    DocumentWorkspace* docs = nullptr;
    SceneMapCanvas* canvas = nullptr;
    std::string* selectionSceneId = nullptr;
    int* canvasLevel = nullptr;
    Vector2* canvasScroll = nullptr;


std::string getExitTarget(const std::string& sceneId, const std::string& direction) const;

void setExitTarget(const std::string& sceneId, const std::string& direction, const std::string& targetId);

// Remove exits[direction] and clear movement[direction].
void clearExitTarget(const std::string& sceneId, const std::string& direction);

/**
 * Delete fromId --direction--> target. When clearReciprocal is true and the
 * reverse exit points back, clears that too. Does not touch audio.sfx.
 */
bool deleteExitLink(
    const std::string& fromId,
    const std::string& direction,
    bool clearReciprocal = true);

struct TransitionSfxPaths
{
    std::string enterPath; // on_enter + from_room=neighbor
    std::string exitPath;  // on_exit + to_room=neighbor
};

/** Count constrained enter/exit clips on sceneId that name neighborId. */
int countConstrainedTransitionSfx(
    const std::string& sceneId,
    const std::string& neighborId) const;

/**
 * Prefer the endpoint that already owns door SFX for this edge; ties / none →
 * preferDefaultOwner (usually the wire's toId).
 */
std::string preferTransitionSfxOwner(
    const std::string& sceneA,
    const std::string& sceneB,
    const std::string& preferDefaultOwner) const;

/** Read enter/exit paths stored on owner for neighbor. */
TransitionSfxPaths readConstrainedTransitionSfx(
    const std::string& ownerId,
    const std::string& neighborId) const;

/**
 * Upsert/remove constrained enter/exit clips on owner for neighbor.
 * Empty path removes that matching entry; unrelated sfx are left alone.
 */
bool upsertConstrainedTransitionSfx(
    const std::string& ownerId,
    const std::string& neighborId,
    const std::string& enterPath,
    const std::string& exitPath);

// True if some scene other than ignoreFromId already has exits[direction] == targetId.
bool exitDirectionAlreadyLeadsTo(
    const std::string& direction,
    const std::string& targetId,
    const std::string& ignoreFromId) const;

// Retarget fromId --direction--> old target to newToId. When reciprocal was true,
// also moves the reverse exit. Returns false if the drop is invalid.
bool retargetExitLink(
    const std::string& fromId,
    const std::string& direction,
    const std::string& newToId,
    bool maintainReciprocal);

/**
 * Create (or replace) fromId --direction--> toId on the same level.
 * When reciprocalIfFree is true, also sets the opposite direction on toId if empty.
 */
bool createExitLink(
    const std::string& fromId,
    const std::string& direction,
    const std::string& toId,
    bool reciprocalIfFree = true);

void recomputeLevelsFromExits();

void getLevelRange(int& outMin, int& outMax) const;

int countScenesOnLevel(int level) const;

std::vector<std::string> scenesOnLevel(int level) const;

bool isSameLevelLink(const std::string& fromId, const std::string& toId) const;

bool directionDelta(const std::string& direction, int& outDCol, int& outDRow) const;

std::string cellKey(int col, int row) const;

void autoLayoutLevel(int level);

/**
 * Straighten wires on one floor without reshuffling: align mid-edge ports of
 * linked F/B/L/R pairs (shared X or Y), then gently separate overlaps.
 */
void cleanupLayoutLevel(int level);

void autoLayoutAllLevels();

void ensureDefaultLayouts();

void applyStackLink(bool placeAbove);

void closeStackDialog();

std::string findStackTarget(const Rectangle& ghost, Rectangle canvasBounds, const std::string& excludeId) const;

std::string oppositeDirection(const std::string& direction) const;
};

} // namespace timberline_editor
#endif
