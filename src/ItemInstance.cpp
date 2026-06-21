#include "ItemInstance.h"

#include <algorithm>
#include <cmath>

namespace testgame
{

namespace
{
    float clamp01(float value)
    {
        return std::max(0.0f, std::min(1.0f, value));
    }
}

float roundItemWeightLb(float weightLb)
{
    return std::round(weightLb * 100.0f) / 100.0f;
}

bool hasItemFlag(const std::vector<std::string>& activeFlags, const std::string& flag)
{
    if (flag.empty())
        return false;

    return std::find(activeFlags.begin(), activeFlags.end(), flag) != activeFlags.end();
}

std::string resolveItemPath(
    const std::string& primaryPath,
    const std::string& alternatePath,
    const std::string& alternateFlag,
    const std::vector<std::string>& activeFlags)
{
    if (!alternatePath.empty() && hasItemFlag(activeFlags, alternateFlag))
        return alternatePath;
    return primaryPath;
}

bool ItemInstance::hasFlag(const std::string& flag) const
{
    return hasItemFlag(activeFlags, flag);
}

float computeItemWeightLb(const ItemDef& def, const ItemInstance& instance)
{
    if (def.quantity.stackable)
    {
        const float unitWeight = def.quantity.unitWeightLb > 0.0f
            ? def.quantity.unitWeightLb
            : def.weightLb;
        const float remaining = std::max(0.0f, 1.0f - clamp01(instance.usedFraction));
        return roundItemWeightLb(unitWeight * static_cast<float>(instance.quantity) * remaining);
    }

    return roundItemWeightLb(def.weightLb);
}

float computeContainerTotalWeightLb(
    const ItemDef& def,
    const ItemInstance& instance,
    const ItemDef* (*resolveDef)(const std::string& defId))
{
    float total = computeItemWeightLb(def, instance);
    if (!def.container.isContainer || resolveDef == nullptr)
        return total;

    for (const ItemInstance& child : instance.contents)
    {
        const ItemDef* childDef = resolveDef(child.defId);
        if (childDef == nullptr)
            continue;

        total += computeContainerTotalWeightLb(*childDef, child, resolveDef);
    }

    return roundItemWeightLb(total);
}

}