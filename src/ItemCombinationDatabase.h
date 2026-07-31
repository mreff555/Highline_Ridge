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
#include <ItemDatabase.h>
#include <ItemInstance.h>
#include <string>
#include <vector>

namespace timberline_engine
{

/**
 * Product craft resolver. Recipes live on item defs (components);
 * there is no separate combinations resource file.
 */
class ItemCombinationDatabase
{
    public:
    /**
     * Build a craft application when exactly one affordable product matches.
     * Returns false if none or more than one product matches (use the chooser).
     */
    bool tryCombine(
        const ItemDatabase& itemDatabase,
        const std::string& firstItemId,
        const std::string& secondItemId,
        const ItemInstance& firstInstance,
        const ItemInstance& secondInstance,
        ItemCombineApplication& outApplication) const;

    /** All product craft candidates that the two instances can afford. */
    std::vector<ItemCraftCandidate> findAffordableCraftProducts(
        const ItemDatabase& itemDatabase,
        const std::string& firstItemId,
        const std::string& secondItemId,
        const ItemInstance& firstInstance,
        const ItemInstance& secondInstance) const;

    bool buildProductCraftApplication(
        const ItemDatabase& itemDatabase,
        const std::string& productId,
        const std::string& firstItemId,
        const std::string& secondItemId,
        const ItemInstance& firstInstance,
        const ItemInstance& secondInstance,
        ItemCombineApplication& outApplication) const;

    private:
    static int instanceQty(const ItemInstance& instance);
    static int minRequiredQty(const ItemComponentDef& component);
    static int spendQtyFor(const ItemComponentDef& component);
    static const ItemInstance* instanceForComponent(
        const ItemComponentDef& component,
        const std::string& firstItemId,
        const std::string& secondItemId,
        const ItemInstance& firstInstance,
        const ItemInstance& secondInstance,
        std::string* outMatchedItemId);
    static bool productAffordable(
        const ItemDef& product,
        const std::string& firstItemId,
        const std::string& secondItemId,
        const ItemInstance& firstInstance,
        const ItemInstance& secondInstance);
};

}

#endif /* ITEM_COMBINATION_DATABASE_H */
