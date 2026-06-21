#ifndef ITEM_INSTANCE_H
#define ITEM_INSTANCE_H

#include <ItemDef.h>
#include <string>
#include <vector>

namespace testgame
{

struct ItemInstance
{
    std::string instanceId;
    std::string defId;
    int quantity = 1;
    float usedFraction = 0.0f;
    std::vector<std::string> activeFlags;
    std::vector<ItemInstance> contents;

    bool hasFlag(const std::string& flag) const;
};

float computeItemWeightLb(const ItemDef& def, const ItemInstance& instance);
float computeContainerTotalWeightLb(
    const ItemDef& def,
    const ItemInstance& instance,
    const ItemDef* (*resolveDef)(const std::string& defId));

}

#endif /* ITEM_INSTANCE_H */