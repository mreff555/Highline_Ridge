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

void autoLayoutAllLevels();

void ensureDefaultLayouts();

void applyStackLink(bool placeAbove);

void closeStackDialog();

std::string findStackTarget(const Rectangle& ghost, Rectangle canvasBounds, const std::string& excludeId) const;

std::string oppositeDirection(const std::string& direction) const;
};

} // namespace timberline_editor
#endif
