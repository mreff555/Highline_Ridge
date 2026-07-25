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

#ifndef SCENE_INTERACTION_DEF_H
#define SCENE_INTERACTION_DEF_H

#include <ConversationStruct.h>
#include <SceneOverlayDef.h>
#include <string>
#include <vector>

namespace timberline_engine
{

struct SceneInteractionDef
{
    std::string id;
    std::string label;
    std::string useDetails;
    std::string sketchPath;
    std::string exitSceneId;
    std::string useFlag;
    std::string hideWhenStoryFlag;
    float useHealthDelta = 0.0f;
    float useEnergyDelta = 0.0f;
    float useResolveDelta = 0.0f;
    float useLucidityDelta = 0.0f;
    float useCharismaDelta = 0.0f;
    bool repeat = false;
    bool oncePerDay = false;
    bool requiresExamine = true;
    bool advancesDay = false;
    std::vector<std::string> requiresAnyInventoryItems;
    GrantedInventoryItemDef grantItem;
    std::vector<OverlaySequenceStep> overlaySequence;
    bool tts = false;
    std::string ttsVoice;
    std::string ttsText;
    std::string ttsAudio;
    bool ttsAfter = false;
    std::string ttsAfterVoice;
    std::string ttsAfterText;
    std::string ttsAfterAudio;
    std::string ttsVariantFlag;
    std::string ttsVariantText;
    std::string ttsVariantAudio;
};

}

#endif /* SCENE_INTERACTION_DEF_H */