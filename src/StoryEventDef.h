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

#ifndef STORY_EVENT_DEF_H
#define STORY_EVENT_DEF_H

#include <ItemDef.h>

#include <string>
#include <vector>

namespace timberline_engine
{

enum class StoryEventWhen
{
    Enter,
    Exit,
    Examine
};

/**
 * Data-authored one-shot (or gated) story beat on a scene.
 * Fired by GameSession at enter / exit / examine hooks (#33 Phase A).
 */
struct StoryEventDef
{
    std::string id;
    StoryEventWhen when = StoryEventWhen::Enter;
    /** Required for Exit triggers (forward/backward/left/right/up/down). */
    std::string direction;
    bool requiresExamined = false;
    std::vector<std::string> requiresFlags;
    std::vector<std::string> unlessFlags;
    std::vector<std::string> setsFlags;
    std::vector<std::string> clearsFlags;
    /**
     * Keys in playerStats.consumedStatusActions, typically sceneId:interactionId
     * (same form as GameSession::interactionKey).
     */
    std::vector<std::string> requiresConsumedStatus;
    std::string narrativeHeader;
    std::string narrative;
    ItemTtsDef narrativeTts;
    /** If true on Exit, intercept movement (stay in room). */
    bool blockMovement = false;
    bool refreshTakeables = false;
    /**
     * When true (default), after firing auto-set unlessFlags behavior via
     * setsFlags — authors should include a once-flag in setsFlags/unlessFlags.
     * Kept for forward compatibility; gating is flag-based.
     */
    bool once = true;
};

}

#endif /* STORY_EVENT_DEF_H */
