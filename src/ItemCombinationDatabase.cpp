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

#include <algorithm>

namespace timberline_engine
{

int ItemCombinationDatabase::instanceQty(const ItemInstance& instance)
{
    return std::max(0, instance.quantity);
}

int ItemCombinationDatabase::minRequiredQty(const ItemComponentDef& component)
{
    return component.reqQty > 0 ? component.reqQty : 1;
}

int ItemCombinationDatabase::spendQtyFor(const ItemComponentDef& component)
{
    if (!component.consume)
        return 0;
    return component.reqQty > 0 ? component.reqQty : 0;
}

const ItemInstance* ItemCombinationDatabase::instanceForComponent(
    const ItemComponentDef& component,
    const std::string& firstItemId,
    const std::string& secondItemId,
    const ItemInstance& firstInstance,
    const ItemInstance& secondInstance,
    std::string* outMatchedItemId)
{
    if (component.acceptsItemId(firstItemId))
    {
        if (outMatchedItemId != nullptr)
            *outMatchedItemId = firstItemId;
        return &firstInstance;
    }
    if (component.acceptsItemId(secondItemId))
    {
        if (outMatchedItemId != nullptr)
            *outMatchedItemId = secondItemId;
        return &secondInstance;
    }
    return nullptr;
}

bool ItemCombinationDatabase::productAffordable(
    const ItemDef& product,
    const std::string& firstItemId,
    const std::string& secondItemId,
    const ItemInstance& firstInstance,
    const ItemInstance& secondInstance)
{
    if (product.components.size() != 2)
        return false;

    const ItemComponentDef& c0 = product.components[0];
    const ItemComponentDef& c1 = product.components[1];
    const bool assignmentOk =
        (c0.acceptsItemId(firstItemId) && c1.acceptsItemId(secondItemId))
        || (c0.acceptsItemId(secondItemId) && c1.acceptsItemId(firstItemId));
    if (!assignmentOk)
        return false;

    for (const ItemComponentDef& component : product.components)
    {
        const ItemInstance* instance = instanceForComponent(
            component,
            firstItemId,
            secondItemId,
            firstInstance,
            secondInstance,
            nullptr);
        if (instance == nullptr)
            return false;
        if (instanceQty(*instance) < minRequiredQty(component))
            return false;
    }
    return true;
}

bool ItemCombinationDatabase::buildProductCraftApplication(
    const ItemDatabase& itemDatabase,
    const std::string& productId,
    const std::string& firstItemId,
    const std::string& secondItemId,
    const ItemInstance& firstInstance,
    const ItemInstance& secondInstance,
    ItemCombineApplication& outApplication) const
{
    outApplication = ItemCombineApplication{};
    const ItemDef* product = itemDatabase.getDef(productId);
    if (product == nullptr || product->components.size() != 2)
        return false;
    if (!productAffordable(
            *product,
            firstItemId,
            secondItemId,
            firstInstance,
            secondInstance))
        return false;

    outApplication.success = true;
    outApplication.grantProductId = productId;
    outApplication.grantQuantity = 1;
    outApplication.narrative = product->assembleNarrative;
    outApplication.ttsOwnerEnabled = product->ttsPolicy.enabled;
    outApplication.narrativeTts = product->assembleTts;

    for (const ItemComponentDef& component : product->components)
    {
        std::string matchedId;
        if (instanceForComponent(
                component,
                firstItemId,
                secondItemId,
                firstInstance,
                secondInstance,
                &matchedId)
            == nullptr)
            return false;

        ItemCombineComponentSpend spend;
        spend.itemId = matchedId;
        spend.spendQty = spendQtyFor(component);
        spend.consume = component.consume;
        outApplication.componentSpends.push_back(spend);
    }
    return true;
}

std::vector<ItemCraftCandidate> ItemCombinationDatabase::findAffordableCraftProducts(
    const ItemDatabase& itemDatabase,
    const std::string& firstItemId,
    const std::string& secondItemId,
    const ItemInstance& firstInstance,
    const ItemInstance& secondInstance) const
{
    std::vector<ItemCraftCandidate> candidates;
    const std::vector<const ItemDef*> products =
        itemDatabase.findCraftProductsForPair(firstItemId, secondItemId);
    for (const ItemDef* product : products)
    {
        if (product == nullptr)
            continue;
        if (!productAffordable(
                *product,
                firstItemId,
                secondItemId,
                firstInstance,
                secondInstance))
            continue;

        ItemCraftCandidate candidate;
        candidate.productId = product->id;
        candidate.productName = product->name;
        candidate.iconPath = product->icons.icon;
        candidates.push_back(candidate);
    }
    return candidates;
}

bool ItemCombinationDatabase::tryCombine(
    const ItemDatabase& itemDatabase,
    const std::string& firstItemId,
    const std::string& secondItemId,
    const ItemInstance& firstInstance,
    const ItemInstance& secondInstance,
    ItemCombineApplication& outApplication) const
{
    outApplication = ItemCombineApplication{};
    if (firstItemId.empty() || secondItemId.empty() || firstItemId == secondItemId)
        return false;

    const std::vector<ItemCraftCandidate> products = findAffordableCraftProducts(
        itemDatabase,
        firstItemId,
        secondItemId,
        firstInstance,
        secondInstance);

    if (products.size() != 1)
        return false;

    return buildProductCraftApplication(
        itemDatabase,
        products.front().productId,
        firstItemId,
        secondItemId,
        firstInstance,
        secondInstance,
        outApplication);
}

}
