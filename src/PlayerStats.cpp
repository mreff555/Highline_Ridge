#include "PlayerStats.h"

#include <algorithm>

namespace testgame
{

bool PlayerStats::apply(const StatusEffect& effect, bool allowRepeat)
{
    if (!effect.hasPlayerDelta())
        return false;

    if (!allowRepeat && !effect.key.empty() && consumedStatusActions.count(effect.key) > 0)
        return false;

    health = std::min(100.0f, std::max(0.0f, health + effect.health));
    energy = std::min(100.0f, std::max(0.0f, energy + effect.energy));
    resolve = std::min(100.0f, std::max(0.0f, resolve + effect.resolve));
    lucidity = std::min(100.0f, std::max(0.0f, lucidity + effect.lucidity));
    charisma = std::min(100.0f, std::max(0.0f, charisma + effect.charisma));
    walletCash = std::max(0.0f, walletCash + effect.money);

    if (!allowRepeat && !effect.key.empty())
        consumedStatusActions.insert(effect.key);

    return true;
}

PlayerStatPercents PlayerStats::toPercents() const
{
    PlayerStatPercents percents;
    percents.health = health;
    percents.energy = energy;
    percents.resolve = resolve;
    percents.lucidity = lucidity;
    percents.charisma = charisma;
    return percents;
}

}