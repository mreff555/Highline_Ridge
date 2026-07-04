#ifndef BLACKJACK_GAME_H
#define BLACKJACK_GAME_H

#include <PlayerStats.h>
#include <string>
#include <vector>

namespace highline_ridge
{

constexpr int kBlackjackBuyInCents = 200;
constexpr float kBlackjackBuyInDollars = 2.0f;
constexpr int kBlackjackTableOpinionFloor = -5;
constexpr int kBlackjackTableOpinionWary = 0;
constexpr int kBlackjackWalkAwayOpinionPenalty = -3;
constexpr int kBlackjackResolveCheatThreshold = 60;
constexpr int kBlackjackOpponentCount = 4;

inline const char* blackjackTableOpinionActor() { return "blackjack_players"; }

inline float blackjackLuckSkill(const PlayerStats& stats)
{
    const float resolve = stats.resolve;
    const float charisma = stats.charisma;
    const float lucidity = stats.lucidity;
    return (resolve * 0.40f + charisma * 0.35f + lucidity * 0.25f) / 100.0f;
}

enum class BlackjackSeatAction
{
    None,
    Hit,
    Stand,
    AttemptCheat,
    AccuseCheat,
    NextHand,
    LeaveTable
};

enum class BlackjackHandState
{
    Betting,
    PlayerTurn,
    OpponentTurn,
    Resolving,
    HandComplete,
    SessionEnded
};

enum class BlackjackHandOutcome
{
    None,
    PlayerWin,
    PlayerLoss,
    Push,
    PlayerBlackjack,
    PlayerBust,
    OpponentBust
};

struct BlackjackCard
{
    int rank = 0;
    int suit = 0;
};

struct BlackjackHandValue
{
    int total = 0;
    bool soft = false;
    bool bust = false;
    bool blackjack = false;
};

struct BlackjackActionResult
{
    bool handled = false;
    bool walletChanged = false;
    bool opinionChanged = false;
    bool sessionEnded = false;
    bool appendNarrative = false;
    std::string narrative;
    float moneyDelta = 0.0f;
    int opinionDelta = 0;
    float resolveDelta = 0.0f;
    float charismaDelta = 0.0f;
};

struct BlackjackGameState
{
    bool active = false;
    int seatedOpponentIndex = 0;
    int potDollars = 0;
    int currentBetDollars = 2;
    BlackjackHandState phase = BlackjackHandState::Betting;
    std::vector<BlackjackCard> deck;
    std::vector<BlackjackCard> playerHand;
    std::vector<BlackjackCard> opponentHand;
    bool opponentCheatingSuspected = false;
    bool opponentCheatingActive = false;
    bool playerCheatingThisHand = false;
    bool hideOpponentHole = true;
    bool showAllOpponentCards = false;
    int handsPlayed = 0;
    int playerTotal = 0;
    int opponentTotal = 0;
    BlackjackHandOutcome lastOutcome = BlackjackHandOutcome::None;
    std::string statusLine;
    std::string resultLine;
};

const char* blackjackOpponentName(int opponentIndex);
const char* blackjackCardRankLabel(int rank);
const char* blackjackCardSuitLabel(int suit);

BlackjackHandValue blackjackEvaluateHand(const std::vector<BlackjackCard>& hand);
std::string blackjackFormatHandTotal(const BlackjackHandValue& value);
std::string blackjackHandOutcomeLabel(BlackjackHandOutcome outcome);

void blackjackShuffleDeck(std::vector<BlackjackCard>& deck);
void blackjackStartSession(BlackjackGameState& state);
bool blackjackBeginHand(
    BlackjackGameState& state,
    float& walletCash,
    int tableOpinion,
    const PlayerStats& stats);
BlackjackActionResult blackjackHandleAction(
    BlackjackGameState& state,
    BlackjackSeatAction action,
    const PlayerStats& stats,
    float& walletCash,
    int& tableOpinion);

bool canSitAtBlackjackTable(float walletCash, int tableOpinion);
bool canContinueBlackjackSession(float walletCash, int tableOpinion);
bool isBlackjackHandInProgress(const BlackjackGameState& state);

}

#endif /* BLACKJACK_GAME_H */