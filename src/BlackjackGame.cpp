#include "BlackjackGame.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace highline_ridge
{

namespace
{
    const char* kOpponentNames[kBlackjackOpponentCount] = {
        "Cole",
        "Hank",
        "Silas",
        "Rourke"
    };

    int cardValue(int rank)
    {
        if (rank == 1)
            return 11;
        if (rank >= 11)
            return 10;
        return rank;
    }

    bool rollChance(float probability)
    {
        const float clamped = std::max(0.0f, std::min(1.0f, probability));
        const int threshold = static_cast<int>(clamped * 1000.0f);
        return (rand() % 1000) < threshold;
    }

    void ensureDeck(BlackjackGameState& state)
    {
        if (state.deck.size() < 12)
            blackjackShuffleDeck(state.deck);
    }

    BlackjackCard drawCard(BlackjackGameState& state)
    {
        ensureDeck(state);
        BlackjackCard card = state.deck.back();
        state.deck.pop_back();
        return card;
    }

    void refreshTotals(BlackjackGameState& state)
    {
        state.playerTotal = blackjackEvaluateHand(state.playerHand).total;
        state.opponentTotal = blackjackEvaluateHand(state.opponentHand).total;
    }

    void setPhaseStatus(BlackjackGameState& state, const std::string& status)
    {
        state.statusLine = status;
    }

    void rotateOpponent(BlackjackGameState& state)
    {
        state.seatedOpponentIndex = (state.seatedOpponentIndex + 1) % kBlackjackOpponentCount;
    }

    void payoutWin(BlackjackGameState& state, float& walletCash, bool blackjackWin)
    {
        const int payout = blackjackWin ? 5 : 4;
        walletCash += static_cast<float>(payout);
        state.potDollars = 0;
        state.lastOutcome = blackjackWin ? BlackjackHandOutcome::PlayerBlackjack : BlackjackHandOutcome::PlayerWin;
        state.resultLine = blackjackWin
            ? "Blackjack! The table pays out."
            : "You take the pot.";
    }

    void payoutPush(BlackjackGameState& state, float& walletCash)
    {
        walletCash += static_cast<float>(state.currentBetDollars);
        state.potDollars = 0;
        state.lastOutcome = BlackjackHandOutcome::Push;
        state.resultLine = "A tie. Your stake comes back.";
    }

    void payoutLoss(BlackjackGameState& state, const std::string& reason)
    {
        state.potDollars = 0;
        state.lastOutcome = BlackjackHandOutcome::PlayerLoss;
        state.resultLine = reason;
    }

    void finishHand(
        BlackjackGameState& state,
        float& walletCash,
        int& tableOpinion,
        const PlayerStats& stats,
        BlackjackActionResult& result)
    {
        state.showAllOpponentCards = true;
        state.hideOpponentHole = false;
        refreshTotals(state);
        state.phase = BlackjackHandState::HandComplete;
        state.handsPlayed += 1;

        if (state.lastOutcome == BlackjackHandOutcome::PlayerWin
            || state.lastOutcome == BlackjackHandOutcome::PlayerBlackjack
            || state.lastOutcome == BlackjackHandOutcome::OpponentBust)
        {
            if (rollChance(0.55f + blackjackLuckSkill(stats) * 0.25f))
            {
                tableOpinion += 1;
                result.opinionDelta += 1;
                result.opinionChanged = true;
            }
            result.resolveDelta = 1.5f;
            result.charismaDelta = 0.5f;
        }
        else if (state.lastOutcome == BlackjackHandOutcome::PlayerLoss
            || state.lastOutcome == BlackjackHandOutcome::PlayerBust)
        {
            if (rollChance(0.35f))
            {
                tableOpinion -= 1;
                result.opinionDelta -= 1;
                result.opinionChanged = true;
            }
            result.resolveDelta = -0.5f;
        }

        if (!canContinueBlackjackSession(walletCash, tableOpinion))
        {
            state.phase = BlackjackHandState::SessionEnded;
            state.active = false;
            result.sessionEnded = true;
            state.statusLine = walletCash < kBlackjackBuyInDollars
                ? "You're out of cash for another buy-in."
                : "The table isn't having you back tonight.";
        }
        else
        {
            state.statusLine = "Hand over. Deal again or leave the table.";
        }

        result.handled = true;
        result.walletChanged = state.lastOutcome == BlackjackHandOutcome::PlayerWin
            || state.lastOutcome == BlackjackHandOutcome::PlayerBlackjack
            || state.lastOutcome == BlackjackHandOutcome::OpponentBust
            || state.lastOutcome == BlackjackHandOutcome::Push;
    }

    void resolveShowdown(
        BlackjackGameState& state,
        float& walletCash,
        int& tableOpinion,
        const PlayerStats& stats,
        BlackjackActionResult& result)
    {
        state.phase = BlackjackHandState::Resolving;
        state.showAllOpponentCards = true;
        state.hideOpponentHole = false;
        refreshTotals(state);

        const BlackjackHandValue playerValue = blackjackEvaluateHand(state.playerHand);
        const BlackjackHandValue opponentValue = blackjackEvaluateHand(state.opponentHand);

        if (playerValue.bust)
        {
            payoutLoss(state, "You bust. The pot stays on the felt.");
            state.lastOutcome = BlackjackHandOutcome::PlayerBust;
        }
        else if (opponentValue.bust)
        {
            payoutWin(state, walletCash, false);
            state.lastOutcome = BlackjackHandOutcome::OpponentBust;
            state.resultLine = blackjackOpponentName(state.seatedOpponentIndex)
                + std::string(" busts. You take the pot.");
        }
        else if (playerValue.blackjack && !opponentValue.blackjack)
        {
            payoutWin(state, walletCash, true);
        }
        else if (opponentValue.blackjack && !playerValue.blackjack)
        {
            payoutLoss(state, blackjackOpponentName(state.seatedOpponentIndex) + std::string(" flips blackjack."));
            state.lastOutcome = BlackjackHandOutcome::PlayerLoss;
        }
        else if (playerValue.total > opponentValue.total)
        {
            payoutWin(state, walletCash, false);
        }
        else if (playerValue.total < opponentValue.total)
        {
            const float luck = blackjackLuckSkill(stats);
            if (playerValue.total == opponentValue.total - 1 && rollChance(luck * 0.35f))
                payoutPush(state, walletCash);
            else
                payoutLoss(state, blackjackOpponentName(state.seatedOpponentIndex) + std::string(" takes the hand."));
        }
        else
        {
            payoutPush(state, walletCash);
        }

        finishHand(state, walletCash, tableOpinion, stats, result);
        rotateOpponent(state);
    }

    void playOpponentTurn(
        BlackjackGameState& state,
        float& walletCash,
        int& tableOpinion,
        const PlayerStats& stats,
        BlackjackActionResult& result)
    {
        state.phase = BlackjackHandState::OpponentTurn;
        setPhaseStatus(state, blackjackOpponentName(state.seatedOpponentIndex) + std::string(" plays out the hand..."));

        while (true)
        {
            BlackjackHandValue opponentValue = blackjackEvaluateHand(state.opponentHand);
            if (opponentValue.bust)
                break;

            const bool shouldStand = opponentValue.total >= 17
                || (opponentValue.soft && opponentValue.total >= 18);
            if (shouldStand)
                break;

            state.opponentHand.push_back(drawCard(state));
            opponentValue = blackjackEvaluateHand(state.opponentHand);
            if (opponentValue.bust)
                break;
        }

        resolveShowdown(state, walletCash, tableOpinion, stats, result);
    }

    void dealOpeningHands(BlackjackGameState& state, const PlayerStats& stats)
    {
        state.playerHand.clear();
        state.opponentHand.clear();
        state.playerCheatingThisHand = false;
        state.opponentCheatingSuspected = false;
        state.opponentCheatingActive = false;
        state.hideOpponentHole = true;
        state.showAllOpponentCards = false;
        state.lastOutcome = BlackjackHandOutcome::None;
        state.resultLine.clear();

        for (int i = 0; i < 2; ++i)
        {
            state.playerHand.push_back(drawCard(state));
            state.opponentHand.push_back(drawCard(state));
        }

        const float luck = blackjackLuckSkill(stats);
        const float suspicionChance = 0.10f + (1.0f - luck) * 0.18f;
        if (rollChance(suspicionChance))
        {
            state.opponentCheatingActive = rollChance(0.55f);
            if (state.opponentCheatingActive || stats.resolve >= kBlackjackResolveCheatThreshold)
                state.opponentCheatingSuspected = state.opponentCheatingActive || rollChance(luck * 0.45f);
        }

        refreshTotals(state);
        state.phase = BlackjackHandState::PlayerTurn;
        setPhaseStatus(
            state,
            blackjackOpponentName(state.seatedOpponentIndex) + std::string(" deals this hand. Hit or stand."));

    }
}

const char* blackjackOpponentName(int opponentIndex)
{
    if (opponentIndex < 0 || opponentIndex >= kBlackjackOpponentCount)
        return "Player";
    return kOpponentNames[opponentIndex];
}

const char* blackjackCardRankLabel(int rank)
{
    switch (rank)
    {
        case 1: return "A";
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        default:
            break;
    }

    static thread_local char buffer[4];
    if (rank >= 2 && rank <= 10)
    {
        snprintf(buffer, sizeof(buffer), "%d", rank);
        return buffer;
    }
    return "?";
}

const char* blackjackCardSuitLabel(int suit)
{
    switch (suit % 4)
    {
        case 0: return "S";
        case 1: return "H";
        case 2: return "D";
        case 3: return "C";
        default: return "?";
    }
}

BlackjackHandValue blackjackEvaluateHand(const std::vector<BlackjackCard>& hand)
{
    BlackjackHandValue value{};
    if (hand.empty())
        return value;

    int total = 0;
    int aceCount = 0;
    for (const BlackjackCard& card : hand)
    {
        if (card.rank == 1)
            ++aceCount;
        total += cardValue(card.rank);
    }

    while (total > 21 && aceCount > 0)
    {
        total -= 10;
        --aceCount;
    }

    value.total = total;
    value.soft = aceCount > 0 && total <= 21;
    value.bust = total > 21;
    value.blackjack = hand.size() == 2 && total == 21;
    return value;
}

std::string blackjackFormatHandTotal(const BlackjackHandValue& value)
{
    if (value.bust)
        return std::to_string(value.total) + " (bust)";
    if (value.blackjack)
        return "21 (blackjack)";
    if (value.soft)
        return std::to_string(value.total) + " (soft)";
    return std::to_string(value.total);
}

std::string blackjackHandOutcomeLabel(BlackjackHandOutcome outcome)
{
    switch (outcome)
    {
        case BlackjackHandOutcome::PlayerWin: return "You won";
        case BlackjackHandOutcome::PlayerLoss: return "You lost";
        case BlackjackHandOutcome::Push: return "Push";
        case BlackjackHandOutcome::PlayerBlackjack: return "Blackjack";
        case BlackjackHandOutcome::PlayerBust: return "Bust";
        case BlackjackHandOutcome::OpponentBust: return "Opponent bust";
        default: return "";
    }
}

void blackjackShuffleDeck(std::vector<BlackjackCard>& deck)
{
    deck.clear();
    deck.reserve(52);
    for (int suit = 0; suit < 4; ++suit)
    {
        for (int rank = 1; rank <= 13; ++rank)
            deck.push_back(BlackjackCard{ rank, suit });
    }

    for (int i = static_cast<int>(deck.size()) - 1; i > 0; --i)
    {
        const int j = rand() % (i + 1);
        std::swap(deck[i], deck[j]);
    }
}

void blackjackStartSession(BlackjackGameState& state)
{
    state = BlackjackGameState{};
    state.active = true;
    state.seatedOpponentIndex = rand() % kBlackjackOpponentCount;
    state.currentBetDollars = static_cast<int>(kBlackjackBuyInDollars);
    state.potDollars = state.currentBetDollars;
    state.phase = BlackjackHandState::Betting;
    blackjackShuffleDeck(state.deck);
    setPhaseStatus(state, "You take the empty chair. The lamp warms the felt.");
}

bool blackjackBeginHand(
    BlackjackGameState& state,
    float& walletCash,
    int tableOpinion,
    const PlayerStats& stats)
{
    if (!state.active)
        return false;

    if (!canContinueBlackjackSession(walletCash, tableOpinion))
    {
        state.phase = BlackjackHandState::SessionEnded;
        state.active = false;
        state.statusLine = "You can't afford another hand, or the table won't have you.";
        return false;
    }

    if (state.handsPlayed > 0)
    {
        if (walletCash < kBlackjackBuyInDollars)
        {
            state.phase = BlackjackHandState::SessionEnded;
            state.active = false;
            state.statusLine = "You don't have another two dollars.";
            return false;
        }

        walletCash -= kBlackjackBuyInDollars;
    }

    state.potDollars = state.currentBetDollars;
    state.phase = BlackjackHandState::Betting;
    dealOpeningHands(state, stats);

    const BlackjackHandValue playerValue = blackjackEvaluateHand(state.playerHand);
    const BlackjackHandValue opponentValue = blackjackEvaluateHand(state.opponentHand);
    if (playerValue.blackjack || opponentValue.blackjack)
    {
        BlackjackActionResult immediate{};
        resolveShowdown(state, walletCash, tableOpinion, stats, immediate);
    }

    return state.phase == BlackjackHandState::PlayerTurn
        || state.phase == BlackjackHandState::HandComplete
        || state.phase == BlackjackHandState::SessionEnded;
}

BlackjackActionResult blackjackHandleAction(
    BlackjackGameState& state,
    BlackjackSeatAction action,
    const PlayerStats& stats,
    float& walletCash,
    int& tableOpinion)
{
    BlackjackActionResult result{};

    if (!state.active)
        return result;

    if (action == BlackjackSeatAction::NextHand)
    {
        if (state.phase != BlackjackHandState::HandComplete)
            return result;

        if (!blackjackBeginHand(state, walletCash, tableOpinion, stats))
        {
            result.sessionEnded = true;
            result.handled = true;
            return result;
        }

        result.handled = true;
        result.walletChanged = state.handsPlayed > 1;
        return result;
    }

    if (state.phase != BlackjackHandState::PlayerTurn)
        return result;

    if (action == BlackjackSeatAction::Hit)
    {
        state.playerHand.push_back(drawCard(state));
        refreshTotals(state);

        const BlackjackHandValue playerValue = blackjackEvaluateHand(state.playerHand);
        if (playerValue.bust)
        {
            payoutLoss(state, "You bust. The miners collect the pot without a word.");
            state.lastOutcome = BlackjackHandOutcome::PlayerBust;
            finishHand(state, walletCash, tableOpinion, stats, result);
            rotateOpponent(state);
            return result;
        }

        setPhaseStatus(state, "Another card. Hit or stand.");
        result.handled = true;
        return result;
    }

    if (action == BlackjackSeatAction::Stand)
    {
        playOpponentTurn(state, walletCash, tableOpinion, stats, result);
        return result;
    }

    if (action == BlackjackSeatAction::AttemptCheat)
    {
        if (stats.resolve < kBlackjackResolveCheatThreshold)
            return result;

        const float luck = blackjackLuckSkill(stats);
        const float successChance = 0.18f + luck * 0.42f + (stats.resolve - 60.0f) * 0.004f;
        if (rollChance(successChance))
        {
            state.playerCheatingThisHand = true;
            if (!state.playerHand.empty())
            {
                state.playerHand.back() = drawCard(state);
                refreshTotals(state);
            }
            setPhaseStatus(state, "You palm a fresh card when no one is looking.");
            result.handled = true;
            result.appendNarrative = true;
            result.narrative = "You palm a fresh card when no one is looking.";
            return result;
        }

        tableOpinion -= 2;
        result.opinionDelta -= 2;
        result.opinionChanged = true;
        payoutLoss(state, "Someone catches the sleight. The pot is pulled away.");
        state.lastOutcome = BlackjackHandOutcome::PlayerLoss;
        finishHand(state, walletCash, tableOpinion, stats, result);
        rotateOpponent(state);
        result.appendNarrative = true;
        result.narrative = "A callused hand clamps down on your wrist. \"Try that again and you'll leave teeth in the sawdust.\"";
        return result;
    }

    if (action == BlackjackSeatAction::AccuseCheat)
    {
        if (stats.resolve < kBlackjackResolveCheatThreshold || !state.opponentCheatingSuspected)
            return result;

        const float luck = blackjackLuckSkill(stats);
        const bool caught = state.opponentCheatingActive && rollChance(0.40f + luck * 0.35f);
        if (caught)
        {
            tableOpinion += 2;
            result.opinionDelta += 2;
            result.opinionChanged = true;
            payoutWin(state, walletCash, false);
            state.resultLine = "The table turns on "
                + std::string(blackjackOpponentName(state.seatedOpponentIndex))
                + ". You keep the pot.";
            finishHand(state, walletCash, tableOpinion, stats, result);
            rotateOpponent(state);
            result.appendNarrative = true;
            result.narrative = "You call the sleight. The other players see it too. The cheater mutters and shoves his chair back an inch.";
            result.handled = true;
            return result;
        }

        tableOpinion -= 2;
        result.opinionDelta -= 2;
        result.opinionChanged = true;
        setPhaseStatus(state, "They stare you down. Nobody believes you.");
        result.handled = true;
        result.appendNarrative = true;
        result.narrative = "You call foul play. The table goes quiet, then ugly. \"Prove it,\" someone says. You can't.";
        state.opponentCheatingSuspected = false;
        return result;
    }

    return result;
}

bool canSitAtBlackjackTable(float walletCash, int tableOpinion)
{
    return walletCash >= kBlackjackBuyInDollars
        && tableOpinion > kBlackjackTableOpinionFloor;
}

bool canContinueBlackjackSession(float walletCash, int tableOpinion)
{
    return canSitAtBlackjackTable(walletCash, tableOpinion);
}

bool isBlackjackHandInProgress(const BlackjackGameState& state)
{
    return state.active
        && (state.phase == BlackjackHandState::PlayerTurn
            || state.phase == BlackjackHandState::OpponentTurn
            || state.phase == BlackjackHandState::Resolving);
}

}