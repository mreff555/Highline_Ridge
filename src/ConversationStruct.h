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

#ifndef CONVERSATION_STRUCT_H
#define CONVERSATION_STRUCT_H

#include <SceneOverlayDef.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace timberline_engine
{

/** Extra per-actor reputation rows on a status bag (in addition to legacy actor/actorOpinion). */
struct ActorOpinionDelta
{
    std::string actor;
    int delta = 0;
};

struct StatusEffect
{
    std::string key;
    float health = 0.0f;
    float energy = 0.0f;
    float resolve = 0.0f;
    float lucidity = 0.0f;
    float charisma = 0.0f;
    float money = 0.0f;
    std::string actor;
    int actorOpinion = 0;
    float actorTab = 0.0f;
    bool repeat = false;
    std::string onZeroLucidity;
    /** Multi-actor reputation; when non-empty, prefer these over a single actorOpinion apply. */
    std::vector<ActorOpinionDelta> opinions;

    bool hasPlayerDelta() const
    {
        return health != 0.0f || energy != 0.0f || resolve != 0.0f || lucidity != 0.0f
            || charisma != 0.0f || money != 0.0f;
    }

    bool hasActorOpinionDelta() const
    {
        if (!opinions.empty())
        {
            for (const ActorOpinionDelta& row : opinions)
            {
                if (!row.actor.empty() && row.delta != 0)
                    return true;
            }
            return false;
        }
        return !actor.empty() && actorOpinion != 0;
    }

    bool hasActorTabDelta() const
    {
        return !actor.empty() && actorTab != 0.0f;
    }

    bool hasDelta() const
    {
        return hasPlayerDelta() || hasActorOpinionDelta() || hasActorTabDelta();
    }
};

/**
 * Expand an authored status bag into one or more apply-ready StatusEffect entries
 * (player/tab/key on one; each opinions[] row as its own actorOpinion effect).
 */
inline void appendExpandedStatusEffects(
    const StatusEffect& authored,
    std::vector<StatusEffect>& out)
{
    StatusEffect player = authored;
    player.opinions.clear();
    player.actorOpinion = 0;
    // Keep actor/actorTab on the player bag for tab ledger; strip opinion so it
    // is not double-applied when opinions[] is also present.
    if (!authored.opinions.empty())
        player.actorOpinion = 0;

    const bool playerUseful = player.hasPlayerDelta() || player.hasActorTabDelta()
        || !player.key.empty() || !player.onZeroLucidity.empty();
    // Legacy single opinion with no opinions[] still rides on the player bag.
    if (authored.opinions.empty() && authored.hasActorOpinionDelta())
    {
        player.actorOpinion = authored.actorOpinion;
        out.push_back(player);
    }
    else if (playerUseful)
    {
        out.push_back(player);
    }

    for (const ActorOpinionDelta& row : authored.opinions)
    {
        if (row.actor.empty() || row.delta == 0)
            continue;
        StatusEffect opinion;
        opinion.actor = row.actor;
        opinion.actorOpinion = row.delta;
        out.push_back(opinion);
    }
}

struct DialogRequirementFields
{
    int minDay = 0;
    int maxDay = 0;
    float requiresLucidityBelow = 0.0f;
    float requiresLucidityAbove = 0.0f;
    bool requiresRecentCollapse = false;
    bool requiresRecentSleep = false;
    std::string requiresDaysSinceFlag;
    int requiresDaysSinceMin = 0;
    int requiresDaysSinceMax = 0;
    std::string opinionActor;
    bool hasActorOpinionAtLeast = false;
    int requiresActorOpinionAtLeast = 0;
    bool hasActorOpinionAtMost = false;
    int requiresActorOpinionAtMost = 0;
};

struct ConversationRequirementContext
{
    int currentDay = 1;
    float lucidity = 30.0f;
    int lastLucidityCollapseDay = 0;
    int lastSleepDay = 0;
    const std::map<std::string, int>* flagGrantedDay = nullptr;
    const std::map<std::string, int>* actorOpinions = nullptr;
};

bool meetsDialogRequirements(
    const DialogRequirementFields& requirements,
    const ConversationRequirementContext& context);

struct ChoiceAvailabilityContext
{
    float walletCash = 0.0f;
    const std::map<std::string, float>* actorTabOwed = nullptr;
    int currentDay = 1;
    int saloonRoomPurchasedDay = 0;

    bool hasSaloonRoomTonight() const
    {
        return saloonRoomPurchasedDay > 0 && saloonRoomPurchasedDay == currentDay;
    }

    float actorTabOwedTo(const std::string& actorId) const
    {
        if (actorId.empty() || actorTabOwed == nullptr)
            return 0.0f;

        std::map<std::string, float>::const_iterator it = actorTabOwed->find(actorId);
        if (it == actorTabOwed->end())
            return 0.0f;

        return std::max(0.0f, it->second);
    }
};

struct GrantedInventoryItemDef
{
    std::string id;
    std::string name;
    std::string iconPath;
    std::string examineImagePath;
    std::string examineText;

    bool isValid() const { return !id.empty(); }
};

/** Authored / runtime slot in an actor's priced inventory (#60). */
struct ActorInventorySlot
{
    std::string id;
    float price = 0.0f;
    /** When true, quantity is ignored and stock never depletes. */
    bool infinite = false;
    int quantity = 1;

    bool isInStock() const { return infinite || quantity > 0; }
};

/**
 * Explicit actor↔player exchange (#64). Money is wallet cash, not an item id.
 * giveToPlayer: actor → player (L→R). takeFromPlayer: player → actor (R→L).
 */
struct ItemExchangeDef
{
    std::string itemId;
    int quantity = 1;
    float money = 0.0f;
    /**
     * Give: consume from actorInventories when true; narrative grant when false.
     * Take: push into actorInventories when true; sink (remove only) when false.
     */
    bool useActorInventory = false;
    /** Empty = phase / spoken actor. */
    std::string actorId;

    bool isValid() const { return !itemId.empty() || money > 0.0f; }
};

struct ConversationChoiceDef
{
    std::string id;
    std::string label;
    std::string response;
    std::string responseAudio;
    std::string sketchPath;
    bool tts = false;
    std::string ttsVoice;
    std::string ttsText;
    std::string ttsAudio;
    std::vector<std::string> ttsAudioSegments;
    bool ttsAfter = false;
    std::string ttsAfterVoice;
    std::string ttsAfterText;
    std::string ttsAfterAudio;
    std::vector<std::string> ttsAfterAudioSegments;
    StatusEffect status;
    GrantedInventoryItemDef grantItem;
    float requiresMoney = 0.0f;
    float requiresInsufficientMoney = 0.0f;
    std::string tabActor;
    bool requiresPositiveActorTab = false;
    bool requiresPayActorTabInFull = false;
    bool requiresInsufficientForActorTab = false;
    bool payActorTabInFull = false;
    bool requiresSaloonRoomAvailable = false;
    bool closePhase = true;
    bool consumeOnSelect = false;
    bool persistConsumed = false;
    bool resumeTopLevel = false;
    std::string resumeChoiceId;
    std::string grantStoryFlag;
    std::string startPhase;
    bool skipIntroOnStartPhase = false;
    /** Leave to this scene (or scene#sub) after the response path. MOVE-like. */
    std::string exitSceneId;
    /**
     * Open this actor's priced inventory after the response (synthesized browse
     * choices). Empty = normal choice tree (#60).
     */
    std::string openActorInventory;
    /**
     * Synthesized buy leaf: after grant/money, decrement this actor's stock for
     * grantItem.id (#60).
     */
    std::string purchaseFromActor;
    /** Actor → player exchange (#64). Applied after response; fail → no transfer. */
    ItemExchangeDef giveToPlayer;
    /** Player → actor exchange (#64). */
    ItemExchangeDef takeFromPlayer;
    std::vector<OverlaySequenceStep> overlaySequence;
    std::vector<ConversationChoiceDef> followUpChoices;

    bool isAvailable(const ChoiceAvailabilityContext& context) const
    {
        if (requiresMoney > 0.0f && context.walletCash < requiresMoney)
            return false;

        if (requiresInsufficientMoney > 0.0f && context.walletCash >= requiresInsufficientMoney)
            return false;

        const std::string actorId = tabActor;
        const float tabOwed = context.actorTabOwedTo(actorId);

        if (requiresPositiveActorTab && tabOwed <= 0.0f)
            return false;

        if (requiresPayActorTabInFull && (tabOwed <= 0.0f || context.walletCash < tabOwed))
            return false;

        if (requiresInsufficientForActorTab && (tabOwed <= 0.0f || context.walletCash >= tabOwed))
            return false;

        if (requiresSaloonRoomAvailable && context.hasSaloonRoomTonight())
            return false;

        return true;
    }

    bool isAvailable(float walletCash) const
    {
        ChoiceAvailabilityContext context;
        context.walletCash = walletCash;
        return isAvailable(context);
    }
};

struct SceneActorDef
{
    std::string id;
    std::string name;
};

struct RandomConversationLine
{
    std::string id;
    std::string actorId;
    std::string actorName;
    DialogRequirementFields requirements;
    std::string text;
    std::string sketchPath;
    std::string audio;
    bool tts = false;
    std::string ttsVoice;
    std::string ttsText;
    std::string ttsAudio;
    std::vector<std::string> ttsAudioSegments;
    bool ttsAfter = false;
    std::string ttsAfterVoice;
    std::string ttsAfterText;
    std::string ttsAfterAudio;
    std::vector<std::string> ttsAfterAudioSegments;
    StatusEffect status;
    int weight = 1;
    bool once = false;
    std::string grantStoryFlag;
    bool allowAttack = false;
    std::string attackEncounterId;
    std::vector<ConversationChoiceDef> choices;
};

enum class ConversationPhaseType
{
    Once,
    Scripted,
    Random
};

struct ConversationPhase
{
    std::string id;
    std::string actorId;
    std::string actorName;
    ConversationPhaseType type = ConversationPhaseType::Once;
    std::string requiresPhaseId;
    std::string requiresFlag;
    DialogRequirementFields requirements;
    bool resetOnSceneEnter = true;
    bool repeatable = false;

    std::string text;
    std::string audio;
    StatusEffect status;
    GrantedInventoryItemDef grantItem;

    std::string intro;
    std::string introAudio;
    std::string sketchPath;
    bool tts = false;
    std::string ttsVoice;
    std::string ttsText;
    std::string ttsAudio;
    std::vector<std::string> ttsAudioSegments;
    bool ttsAfter = false;
    std::string ttsAfterVoice;
    std::string ttsAfterText;
    std::string ttsAfterAudio;
    std::vector<std::string> ttsAfterAudioSegments;
    bool resumeTts = false;
    std::string resumeTtsVoice;
    std::string resumeTtsText;
    std::string resumeTtsAudio;
    std::string resumeIntro;
    std::string resumeIntroAudio;
    std::vector<ConversationChoiceDef> choices;

    std::string poolId;
    std::vector<RandomConversationLine> lines;
    bool avoidRepeat = true;
};

/** One spoken bag for fight lead-up / win / lose. */
struct AttackDialogBag
{
    std::string text;
    bool tts = false;
    std::string ttsVoice;
    std::string ttsText;
    std::string ttsAudio;
    std::vector<std::string> ttsAudioSegments;
};

struct AttackStatLoss
{
    bool enabled = false;
    float percentOfCurrent = 50.0f;
};

/**
 * Authored fight encounter. Lines set allowAttack + attackEncounterId to arm
 * the Attack button; clicking Attack plays leadUp → scrape → win/lose.
 */
struct AttackEncounterDef
{
    std::string id;
    std::string combatantId;
    AttackDialogBag leadUp;
    AttackDialogBag win;
    AttackDialogBag lose;

    int actorEdgeHealth = 0;
    int actorEdgeEnergy = 0;
    int actorEdgeResolve = 0;
    int actorEdgeLucidity = 0;
    int actorEdgeCharisma = 0;

    bool cannotLose = false;
    bool playerDeath = false;
    AttackStatLoss lossHealth;
    AttackStatLoss lossEnergy;
    AttackStatLoss lossResolve;
    AttackStatLoss lossLucidity;
    AttackStatLoss lossCharisma;

    bool reputationChange = false;
    int reputationDelta = 0;

    /** When Attack may be offered (choice-style money/tab/room + dialog requirements). */
    DialogRequirementFields requirements;
    float requiresMoney = 0.0f;
    float requiresInsufficientMoney = 0.0f;
    bool requiresPositiveActorTab = false;
    bool requiresPayActorTabInFull = false;
    bool requiresInsufficientForActorTab = false;
    bool requiresSaloonRoomAvailable = false;
    std::string tabActor;
    bool requiresLightSource = false;
    std::string requiresInventoryItem;
    std::string requiresStoryFlag;

    /** Money / tab / room gates — same semantics as ConversationChoiceDef::isAvailable. */
    bool isAvailable(const ChoiceAvailabilityContext& context) const
    {
        if (requiresMoney > 0.0f && context.walletCash < requiresMoney)
            return false;
        if (requiresInsufficientMoney > 0.0f && context.walletCash >= requiresInsufficientMoney)
            return false;
        const std::string actorId = tabActor;
        const float tabOwed = context.actorTabOwedTo(actorId);
        if (requiresPositiveActorTab && tabOwed <= 0.0f)
            return false;
        if (requiresPayActorTabInFull && (tabOwed <= 0.0f || context.walletCash < tabOwed))
            return false;
        if (requiresInsufficientForActorTab && (tabOwed <= 0.0f || context.walletCash >= tabOwed))
            return false;
        if (requiresSaloonRoomAvailable && context.hasSaloonRoomTonight())
            return false;
        return true;
    }
};

struct SceneSpeakConfig
{
    std::vector<ConversationPhase> phases;
    /** Default bags keyed by actor id (seeded into WorldState on first use). */
    std::map<std::string, std::vector<ActorInventorySlot>> actorInventories;
    /** Fight defs keyed by attackEncounterId. */
    std::map<std::string, AttackEncounterDef> attackEncounters;

    bool hasPhases() const { return !phases.empty(); }

    const AttackEncounterDef* findAttackEncounter(const std::string& id) const
    {
        if (id.empty())
            return nullptr;
        const auto it = attackEncounters.find(id);
        return it == attackEncounters.end() ? nullptr : &it->second;
    }
};

struct SpeakResult
{
    enum class Action
    {
        None,
        ShowNarrative,
        ShowChoices,
        LucidityCollapse
    };

    Action action = Action::None;
    std::string narrative;
    std::string sketchPath;
    std::vector<ConversationChoiceDef> choices;
    std::vector<StatusEffect> statusEffects;
    GrantedInventoryItemDef grantItem;
    std::vector<std::string> dialogAudioTracks;
    bool useTts = false;
    std::string ttsText;
    std::string ttsVoice;
    std::vector<std::string> ttsAudioPaths;
    std::string grantStoryFlag;
    std::string startPhaseId;
    bool skipIntroOnStartPhase = false;
    /** Non-empty → leave room after response (MOVE-like; skips startPhase chain). */
    std::string exitSceneId;
    std::vector<OverlaySequenceStep> overlaySequence;
    std::string spokenActorId;
    /** When set with grantItem, GameSession depletes this actor's stock (#60). */
    std::string purchaseFromActor;
    ItemExchangeDef giveToPlayer;
    ItemExchangeDef takeFromPlayer;
    /**
     * Set by GameSession after applying give/take. True when no exchange was
     * requested, or when the exchange succeeded (#64).
     */
    bool exchangeSucceeded = true;
};

}

#endif /* CONVERSATION_STRUCT_H */