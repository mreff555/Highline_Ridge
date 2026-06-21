#ifndef ITEM_DATABASE_H
#define ITEM_DATABASE_H

#include <InventoryItem.h>
#include <ItemDef.h>
#include <ItemInstance.h>
#include <map>
#include <string>
#include <vector>

namespace testgame
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
    std::string resolveIconPath(const ItemDef& def, const ItemInstance& instance) const;
    std::string resolveImagePath(const ItemDef& def, const ItemInstance& instance) const;
    float resolveWeightLb(const ItemDef& def, const ItemInstance& instance) const;

    InventoryItem buildInventoryItem(
        const ItemInstance& instance,
        const ItemDefOverrides& overrides = {}) const;

    private:
    static const ItemDef* resolveDefCallback(const std::string& defId);

    std::map<std::string, ItemDef> items;
    static ItemDatabase* activeDatabase;
};

}

#endif /* ITEM_DATABASE_H */