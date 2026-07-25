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

#include "PlayerStats.h"

#include <algorithm>

namespace timberline_engine
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