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

#ifndef ITEM_COMBINATION_DATABASE_H
#define ITEM_COMBINATION_DATABASE_H

#include <ItemCombinationDef.h>
#include <ItemInstance.h>
#include <string>
#include <vector>

namespace timberline_engine
{

class ItemCombinationDatabase
{
    public:
    bool load(const std::string& path);

    bool tryCombine(
        const std::string& firstItemId,
        const std::string& secondItemId,
        const ItemInstance& firstInstance,
        const ItemInstance& secondInstance,
        ItemCombineApplication& outApplication) const;

    private:
    bool requirementsMet(
        const ItemCombineRequirements& requirements,
        const ItemInstance& instance) const;

    bool recipeMatches(
        const ItemCombineRecipe& recipe,
        const std::string& firstItemId,
        const std::string& secondItemId,
        const ItemInstance& firstInstance,
        const ItemInstance& secondInstance) const;

    std::vector<ItemCombineRecipe> recipes;
};

}

#endif /* ITEM_COMBINATION_DATABASE_H */