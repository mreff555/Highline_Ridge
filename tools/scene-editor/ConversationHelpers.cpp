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

#include "ConversationHelpers.h"
#include <sstream>
#include <string>

namespace timberline_editor
{

std::string conversationJsonEscape(const std::string& key)
{
    std::string out;
    out.reserve(key.size());
    for (char ch : key)
    {
        if (ch == '~')
            out += "~0";
        else if (ch == '/')
            out += "~1";
        else
            out += ch;
    }
    return out;
}
std::string conversationPointerJoin(const std::string& parent, const std::string& token)
{
    return parent + "/" + conversationJsonEscape(token);
}
std::string conversationPointerIndex(const std::string& parent, size_t index)
{
    return parent + "/" + std::to_string(index);
}
std::string phaseActorId(const nlohmann::json& phase)
{
    if (phase.contains("actorId") && phase["actorId"].is_string())
        return phase["actorId"].get<std::string>();

    if (phase.contains("actor"))
    {
        const nlohmann::json& actor = phase["actor"];
        if (actor.is_string())
            return actor.get<std::string>();
        if (actor.is_object() && actor.contains("id") && actor["id"].is_string())
            return actor["id"].get<std::string>();
    }

    if (phase.contains("id") && phase["id"].is_string())
        return phase["id"].get<std::string>();

    return "(unknown)";
}
std::string phaseActorName(const nlohmann::json& phase, const std::string& actorId)
{
    if (phase.contains("actorName") && phase["actorName"].is_string())
    {
        const std::string name = phase["actorName"].get<std::string>();
        if (!name.empty())
            return name;
    }

    if (phase.contains("actor") && phase["actor"].is_object() &&
        phase["actor"].contains("name") && phase["actor"]["name"].is_string())
    {
        const std::string name = phase["actor"]["name"].get<std::string>();
        if (!name.empty())
            return name;
    }

    return actorId;
}
std::string choiceTreeLabel(const nlohmann::json& choice)
{
    if (choice.contains("label") && choice["label"].is_string())
    {
        const std::string label = choice["label"].get<std::string>();
        if (!label.empty())
            return label;
    }
    if (choice.contains("id") && choice["id"].is_string())
    {
        const std::string id = choice["id"].get<std::string>();
        if (!id.empty())
            return id;
    }
    if (choice.contains("text") && choice["text"].is_string())
    {
        const std::string text = choice["text"].get<std::string>();
        if (!text.empty())
            return text;
    }
    return "(dialog)";
}
ConversationTreeNode buildChoiceTreeNode(const nlohmann::json& choice, const std::string& pointer)
{
    ConversationTreeNode node;
    node.kind = ConversationNodeKind::Dialog;
    node.key = "choice:" + pointer;
    node.editDoc = ConversationEditDoc::Conversations;
    node.jsonPointer = pointer;
    node.label = choiceTreeLabel(choice);

    if (choice.contains("id") && choice["id"].is_string())
        node.detail = choice["id"].get<std::string>();

    if (choice.contains("choices") && choice["choices"].is_array())
    {
        const nlohmann::json& nested = choice["choices"];
        for (size_t i = 0; i < nested.size(); ++i)
        {
            if (!nested[i].is_object())
                continue;
            node.children.push_back(
                buildChoiceTreeNode(nested[i], conversationPointerIndex(pointer + "/choices", i)));
        }
    }

    return node;
}

} // namespace timberline_editor
