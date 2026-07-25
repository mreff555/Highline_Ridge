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

#ifndef SUB_SCENE_DEF_H
#define SUB_SCENE_DEF_H

#include <ActionStruct.h>
#include <ItemDef.h>
#include <MaskConditionDef.h>
#include <AudioTypes.h>
#include <MovementStruct.h>
#include <SceneInteractionDef.h>
#include <string>
#include <vector>

namespace timberline_engine
{

struct SubSceneRuleDef
{
    MaskCondition when;
    std::string subSceneId;
    std::string untilPhase;
    bool isDefault = false;
    bool applyOnEnter = true;
};

struct SubSceneDef
{
    std::string id;
    std::string imagePath;
    std::string description;
    ItemTtsDef descriptionTts;
    std::string examineDetails;
    ItemTtsDef examineTts;
    std::string examineFlag;
    float examineLucidityDelta = 0.0f;
    bool examineLucidityOncePerDay = false;
    std::string speakDetails;
    std::string useDetails;
    float useHealthDelta = 0.0f;
    float useEnergyDelta = 0.0f;
    bool useRepeatStatus = false;
    bool useRequiresExamine = true;
    bool useAdvancesDay = false;
    std::string useExit;
    MovementStruct movement;
    ActionStruct actions;
    MovementStruct movementBlanket;
    RoomAudioConfig audio;
    std::vector<std::string> flags;
    std::vector<std::string> actorsPresent;
    std::vector<SceneInteractionDef> interactions;
    bool focus = false;

    bool hasFlag(const std::string& flag) const;
    bool isDark() const { return hasFlag("dark"); }
    bool providesLight() const { return hasFlag("provides_light"); }
};

}

#endif /* SUB_SCENE_DEF_H */