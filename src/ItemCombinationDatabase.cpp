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

#include "ItemCombinationDatabase.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace timberline_engine
{

namespace
{

bool parseRequirements(const nlohmann::json& json, ItemCombineRequirements& out)
{
    if (!json.is_object())
        return true;

    out.requiredFlags.clear();
    out.forbiddenFlags.clear();

    const nlohmann::json& required = json.value("flags", json.value("requiredFlags", nlohmann::json::array()));
    if (required.is_array())
    {
        for (const nlohmann::json& flag : required)
        {
            if (flag.is_string() && !flag.get<std::string>().empty())
                out.requiredFlags.push_back(flag.get<std::string>());
        }
    }

    const nlohmann::json& forbidden = json.value(
        "notFlags",
        json.value("forbiddenFlags", nlohmann::json::array()));
    if (forbidden.is_array())
    {
        for (const nlohmann::json& flag : forbidden)
        {
            if (flag.is_string() && !flag.get<std::string>().empty())
                out.forbiddenFlags.push_back(flag.get<std::string>());
        }
    }

    return true;
}

bool parseEffect(const nlohmann::json& json, ItemCombineEffect& out)
{
    if (!json.is_object())
        return false;

    if (json.contains("remove"))
    {
        out.type = ItemCombineEffect::Type::Remove;
        out.itemId = json.value("remove", "");
        return !out.itemId.empty();
    }

    if (json.contains("addFlag"))
    {
        const nlohmann::json& flagEntry = json["addFlag"];
        if (!flagEntry.is_object())
            return false;

        out.type = ItemCombineEffect::Type::AddFlag;
        out.itemId = flagEntry.value("item", "");
        out.flag = flagEntry.value("flag", "");
        return !out.itemId.empty() && !out.flag.empty();
    }

    if (json.contains("grant"))
    {
        out.type = ItemCombineEffect::Type::Grant;
        out.itemId = json.value("grant", "");
        return !out.itemId.empty();
    }

    return false;
}

bool parseRecipe(const nlohmann::json& json, ItemCombineRecipe& out)
{
    if (!json.is_object())
        return false;

    out.id = json.value("id", "");
    out.itemA = json.value("itemA", "");
    out.itemB = json.value("itemB", "");
    if (out.itemA.empty() || out.itemB.empty())
        return false;

    const nlohmann::json& requirements = json.value("requirements", nlohmann::json::object());
    if (requirements.is_object())
    {
        parseRequirements(requirements.value(out.itemA, nlohmann::json::object()), out.requirementsA);
        parseRequirements(requirements.value(out.itemB, nlohmann::json::object()), out.requirementsB);
    }

    out.narrative = json.value("narrative", "");
    out.effects.clear();
    const nlohmann::json& effects = json.value("effects", nlohmann::json::array());
    if (!effects.is_array())
        return false;

    for (const nlohmann::json& entry : effects)
    {
        ItemCombineEffect effect;
        if (parseEffect(entry, effect))
            out.effects.push_back(effect);
    }

    return !out.effects.empty();
}

}

bool ItemCombinationDatabase::load(const std::string& path)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return false;

    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }

    recipes.clear();

    const nlohmann::json& combinations = root.value("combinations", nlohmann::json::array());
    if (!combinations.is_array())
        return false;

    for (const nlohmann::json& entry : combinations)
    {
        ItemCombineRecipe recipe;
        if (parseRecipe(entry, recipe))
            recipes.push_back(recipe);
    }

    return !recipes.empty();
}

bool ItemCombinationDatabase::requirementsMet(
    const ItemCombineRequirements& requirements,
    const ItemInstance& instance) const
{
    for (const std::string& flag : requirements.requiredFlags)
    {
        if (!instance.hasFlag(flag))
            return false;
    }

    for (const std::string& flag : requirements.forbiddenFlags)
    {
        if (instance.hasFlag(flag))
            return false;
    }

    return true;
}

bool ItemCombinationDatabase::recipeMatches(
    const ItemCombineRecipe& recipe,
    const std::string& firstItemId,
    const std::string& secondItemId,
    const ItemInstance& firstInstance,
    const ItemInstance& secondInstance) const
{
    const bool directMatch = firstItemId == recipe.itemA && secondItemId == recipe.itemB;
    const bool swappedMatch = firstItemId == recipe.itemB && secondItemId == recipe.itemA;
    if (!directMatch && !swappedMatch)
        return false;

    if (directMatch)
    {
        return requirementsMet(recipe.requirementsA, firstInstance)
            && requirementsMet(recipe.requirementsB, secondInstance);
    }

    return requirementsMet(recipe.requirementsA, secondInstance)
        && requirementsMet(recipe.requirementsB, firstInstance);
}

bool ItemCombinationDatabase::tryCombine(
    const std::string& firstItemId,
    const std::string& secondItemId,
    const ItemInstance& firstInstance,
    const ItemInstance& secondInstance,
    ItemCombineApplication& outApplication) const
{
    outApplication = ItemCombineApplication{};

    if (firstItemId.empty() || secondItemId.empty() || firstItemId == secondItemId)
        return false;

    for (const ItemCombineRecipe& recipe : recipes)
    {
        if (!recipeMatches(recipe, firstItemId, secondItemId, firstInstance, secondInstance))
            continue;

        outApplication.success = true;
        outApplication.narrative = recipe.narrative;
        for (const ItemCombineEffect& effect : recipe.effects)
        {
            switch (effect.type)
            {
                case ItemCombineEffect::Type::Remove:
                    outApplication.removeItemIds.push_back(effect.itemId);
                    break;
                case ItemCombineEffect::Type::AddFlag:
                    outApplication.addFlags.push_back({ effect.itemId, effect.flag });
                    break;
                case ItemCombineEffect::Type::Grant:
                    outApplication.grantItemIds.push_back(effect.itemId);
                    break;
            }
        }
        return true;
    }

    return false;
}

}