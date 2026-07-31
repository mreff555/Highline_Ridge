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

#include <ItemDef.h>
#include <string>
#include <vector>

namespace timberline_engine
{

/** Runtime spend against a live inventory instance for product crafts. */
struct ItemCombineComponentSpend
{
    std::string itemId;
    int spendQty = 0;
    bool consume = true;
};

/** Result of resolving a product craft from item.components. */
struct ItemCombineApplication
{
    bool success = false;
    std::string narrative;
    ItemTtsDef narrativeTts;
    bool ttsOwnerEnabled = false;
    std::string grantProductId;
    int grantQuantity = 1;
    std::vector<ItemCombineComponentSpend> componentSpends;
};

struct ItemCraftCandidate
{
    std::string productId;
    std::string productName;
    std::string iconPath;
};

}

#endif /* ITEM_COMBINATION_DEF_H */
