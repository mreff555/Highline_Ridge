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

#ifndef ITEM_COMBINATION_DEF_H
#define ITEM_COMBINATION_DEF_H

#include <string>
#include <vector>

namespace timberline_engine
{

struct ItemCombineRequirements
{
    std::vector<std::string> requiredFlags;
    std::vector<std::string> forbiddenFlags;
};

struct ItemCombineEffect
{
    enum class Type
    {
        Remove,
        AddFlag,
        Grant
    };

    Type type = Type::Grant;
    std::string itemId;
    std::string flag;
};

struct ItemCombineRecipe
{
    std::string id;
    std::string itemA;
    std::string itemB;
    ItemCombineRequirements requirementsA;
    ItemCombineRequirements requirementsB;
    std::vector<ItemCombineEffect> effects;
    std::string narrative;
};

struct ItemCombineApplication
{
    bool success = false;
    std::string narrative;
    std::vector<std::string> removeItemIds;
    std::vector<std::pair<std::string, std::string>> addFlags;
    std::vector<std::string> grantItemIds;
};

}

#endif /* ITEM_COMBINATION_DEF_H */