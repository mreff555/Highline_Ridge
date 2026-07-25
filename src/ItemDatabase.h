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

#ifndef ITEM_DATABASE_H
#define ITEM_DATABASE_H

#include <InventoryItem.h>
#include <ItemDef.h>
#include <ItemInstance.h>
#include <map>
#include <string>
#include <vector>

namespace timberline_engine
{

struct ItemDefOverrides
{
    std::string name;
    std::string description;
    std::string iconPath;
    std::string examineImagePath;
};

class ItemDatabase
{
    public:
    bool load(const std::string& path, const std::string& assetRoot = ".");

    const ItemDef* getDef(const std::string& id) const;
    bool hasDef(const std::string& id) const;

    ItemInstance createInstance(
        const std::string& defId,
        const std::vector<std::string>& storyFlags = {}) const;

    ItemInstance createInstanceFromOverrides(
        const std::string& defId,
        const ItemDefOverrides& overrides,
        const std::vector<std::string>& storyFlags = {}) const;

    std::string resolveName(const ItemDef& def, const ItemDefOverrides& overrides) const;
    std::string resolveDescription(const ItemDef& def, const ItemDefOverrides& overrides) const;
    std::string resolveExamineDescription(
        const ItemDef& def,
        const ItemInstance& instance,
        const ItemDefOverrides& overrides) const;
    std::string resolveIconPath(const ItemDef& def, const ItemInstance& instance) const;
    std::string resolveImagePath(const ItemDef& def, const ItemInstance& instance) const;
    float resolveWeightLb(const ItemDef& def, const ItemInstance& instance) const;
    bool isExtractableContainerContent(const ItemDef& containerDef, const std::string& contentDefId) const;

    InventoryItem buildInventoryItem(
        const ItemInstance& instance,
        const ItemDefOverrides& overrides = {}) const;

    static int walletCashRoundedDown(float walletCash);
    static std::string formatWalletCashIconLabel(float walletCash);
    static std::string appendWalletCashDescription(
        const std::string& baseDescription,
        float walletCash);
    static std::string formatUndefinedItemExamineText(
        const std::string& purchaseSceneId,
        const std::string& itemName);

    private:
    std::map<std::string, ItemDef> items;
};

}

#endif /* ITEM_DATABASE_H */