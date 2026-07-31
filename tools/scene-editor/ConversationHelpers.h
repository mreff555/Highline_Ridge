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

#ifndef TIMBERLINE_CONVERSATION_HELPERS_H
#define TIMBERLINE_CONVERSATION_HELPERS_H
#include "EditorTypes.h"
#include <nlohmann/json.hpp>
#include <string>

namespace timberline_editor
{

std::string conversationJsonEscape(const std::string& key);
std::string conversationPointerJoin(const std::string& parent, const std::string& token);
std::string conversationPointerIndex(const std::string& parent, size_t index);
std::string phaseActorId(const nlohmann::json& phase);
std::string phaseActorName(const nlohmann::json& phase, const std::string& actorId);
std::string choiceTreeLabel(const nlohmann::json& choice);
ConversationTreeNode buildChoiceTreeNode(const nlohmann::json& choice, const std::string& pointer);

} // namespace timberline_editor

#endif
