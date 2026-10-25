/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Weighted “scrape” fight resolution: all player stats vs actor edge.
 ******************************************************************************/

#ifndef TIMBERLINE_COMBAT_RESOLVER_H
#define TIMBERLINE_COMBAT_RESOLVER_H

#include <PlayerStats.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace timberline_engine
{

/** Weights sum to 1.0 — Resolve heaviest, Charisma material, Lucidity lightest. */
struct CombatStatWeights
{
    float health = 0.20f;
    float energy = 0.20f;
    float resolve = 0.35f;
    float lucidity = 0.10f;
    float charisma = 0.15f;
};

inline CombatStatWeights combatDefaultWeights()
{
    return {};
}

/**
 * Authored actor advantage / disadvantage per stat.
 * Positive = actor advantage (harder fight); negative = disadvantage.
 * Applied as clamp(50 + mod, 0, 100) against the mid-stat baseline.
 */
struct CombatActorEdge
{
    int health = 0;
    int energy = 0;
    int resolve = 0;
    int lucidity = 0;
    int charisma = 0;
};

struct CombatResolution
{
    bool playerWon = false;
    float playerScore = 0.0f;
    float actorScore = 0.0f;
    float margin = 0.0f;
    float roll = 0.0f;
    float check = 0.0f;
};

inline float combatClampStat(float v)
{
    if (!std::isfinite(v))
        return 0.0f;
    return std::clamp(v, 0.0f, 100.0f);
}

inline float combatWeightedScore(
    float health,
    float energy,
    float resolve,
    float lucidity,
    float charisma,
    const CombatStatWeights& w = combatDefaultWeights())
{
    return w.health * combatClampStat(health)
        + w.energy * combatClampStat(energy)
        + w.resolve * combatClampStat(resolve)
        + w.lucidity * combatClampStat(lucidity)
        + w.charisma * combatClampStat(charisma);
}

inline float combatPlayerScore(
    const PlayerStats& player, const CombatStatWeights& w = combatDefaultWeights())
{
    return combatWeightedScore(
        player.health,
        player.energy,
        player.resolve,
        player.lucidity,
        player.charisma,
        w);
}

inline float combatActorScore(
    const CombatActorEdge& edge, const CombatStatWeights& w = combatDefaultWeights())
{
    return combatWeightedScore(
        50.0f + static_cast<float>(edge.health),
        50.0f + static_cast<float>(edge.energy),
        50.0f + static_cast<float>(edge.resolve),
        50.0f + static_cast<float>(edge.lucidity),
        50.0f + static_cast<float>(edge.charisma),
        w);
}

/** Even matchup needs roll ≥ this (midpoint of Uniform 0..20). */
inline constexpr float kCombatWinThreshold = 10.0f;

/**
 * Resolve a scrape fight.
 * @param rollOverride  If >= 0, use this instead of RNG (tests / determinism).
 */
inline CombatResolution resolveScrapeFight(
    const PlayerStats& player,
    const CombatActorEdge& actorEdge,
    bool cannotLose,
    std::mt19937& rng,
    float rollOverride = -1.0f)
{
    CombatResolution out;
    out.playerScore = combatPlayerScore(player);
    out.actorScore = combatActorScore(actorEdge);
    out.margin = out.playerScore - out.actorScore;

    if (cannotLose)
    {
        out.roll = 0.0f;
        out.check = out.margin;
        out.playerWon = true;
        return out;
    }

    if (rollOverride >= 0.0f)
        out.roll = rollOverride;
    else
    {
        std::uniform_real_distribution<float> dist(0.0f, 20.0f);
        out.roll = dist(rng);
    }
    out.check = out.margin + out.roll;
    out.playerWon = out.check >= kCombatWinThreshold;
    return out;
}

/** Default edge for the saloon drunk_patron brawl (soaked but mean). */
inline CombatActorEdge drunkPatronCombatEdge()
{
    CombatActorEdge e;
    e.health = 10;   // still sturdy enough
    e.energy = -5;   // winded / drunk
    e.resolve = -10; // soaked grit
    e.lucidity = -15;
    e.charisma = 5;  // crowd-pleasing swagger
    return e;
}

} // namespace timberline_engine

#endif
