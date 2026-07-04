#include "BlackjackPanel.h"
#include "UiBackdrop.h"

#include <RaylibCompat.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace highline_ridge
{

namespace
{
    struct PanelLayout
    {
        float pad = 16.0f;
        float backHeight = 46.0f;
        float actionHeight = 42.0f;
        float actionGap = 8.0f;
        float contentX = 0.0f;
        float contentW = 0.0f;
        float contentRight = 0.0f;
        float actionY = 0.0f;
        float resultY = 0.0f;
        float cardsY = 0.0f;
    };

    constexpr float kCardAreaLift = 48.0f;

    PanelLayout computePanelLayout(Rectangle panelBounds)
    {
        PanelLayout layout;
        layout.contentX = panelBounds.x + layout.pad;
        layout.contentW = panelBounds.width - layout.pad * 2.0f;
        layout.contentRight = panelBounds.x + panelBounds.width - layout.pad;
        const float backY = panelBounds.y + panelBounds.height - layout.pad - layout.backHeight;
        layout.actionY = backY - 8.0f - layout.actionHeight;
        layout.resultY = layout.actionY - 20.0f - kCardAreaLift;
        layout.cardsY = layout.resultY - 96.0f;
        return layout;
    }

    float handSpan(size_t cardCount, float cardW, float overlap)
    {
        if (cardCount == 0)
            return 0.0f;

        return cardW + overlap * static_cast<float>(cardCount - 1);
    }

    const Color kPanelFill = { 22, 48, 36, 255 };
    const Color kPanelBorder = { 168, 138, 72, 255 };
    const Color kSectionLabel = { 168, 198, 168, 255 };
    const Color kCardFace = { 248, 244, 232, 255 };
    const Color kCardBack = { 88, 36, 36, 255 };
    const Color kMutedText = { 180, 196, 180, 255 };

    std::unique_ptr<Button> makeButtonPtr(
        const char* label,
        Rectangle bounds,
        Font font,
        const ButtonStyle& style)
    {
        return std::make_unique<Button>(
            label,
            Vector2{ bounds.x, bounds.y },
            Vector2{ bounds.width, bounds.height },
            font,
            style);
    }
}

void BlackjackPanel::setFonts(Font regular, Font bold)
{
    regularFont = regular;
    boldFont = bold.texture.id != 0 ? bold : regular;
    baseButtonStyle = {
        { 236, 228, 206, 255 },
        { 54, 92, 68, 255 },
        { 72, 118, 88, 255 },
        { 42, 78, 58, 255 },
        { 34, 52, 42, 255 },
        { 168, 138, 72, 255 },
        { 62, 52, 34, 255 },
        { 128, 142, 128, 255 },
        0.18f,
        18.0f
    };
    buttonStyle = baseButtonStyle;
    rebuildLayout();
}

void BlackjackPanel::setPanelBounds(Rectangle bounds)
{
    panelBounds = bounds;
    rebuildLayout();
}

void BlackjackPanel::setUiBackdrop(const UiBackdrop* backdrop)
{
    uiBackdrop = backdrop;
    if (uiBackdrop != nullptr)
        buttonStyle = uiBackdrop->contrastedButtonStyle(baseButtonStyle);
    else
        buttonStyle = baseButtonStyle;
    rebuildLayout();
}

void BlackjackPanel::rebuildLayout()
{
    actionButtons.clear();

    if (regularFont.texture.id == 0 || panelBounds.width <= 0.0f)
        return;

    const PanelLayout layout = computePanelLayout(panelBounds);
    const float actionW = (layout.contentW - layout.actionGap * 3.0f) / 4.0f;

    const char* labels[] = { "Hit", "Stand", "Cheat", "Accuse" };
    const BlackjackSeatAction actions[] = {
        BlackjackSeatAction::Hit,
        BlackjackSeatAction::Stand,
        BlackjackSeatAction::AttemptCheat,
        BlackjackSeatAction::AccuseCheat
    };

    for (int i = 0; i < 4; ++i)
    {
        Rectangle bounds = {
            layout.contentX + (actionW + layout.actionGap) * static_cast<float>(i),
            layout.actionY,
            actionW,
            layout.actionHeight
        };
        actionButtons.push_back({
            makeButtonPtr(labels[i], bounds, regularFont, buttonStyle),
            actions[i]
        });
    }

    Rectangle nextBounds = {
        layout.contentX,
        layout.actionY,
        layout.contentW,
        layout.actionHeight
    };
    actionButtons.push_back({
        makeButtonPtr("Next Hand", nextBounds, regularFont, buttonStyle),
        BlackjackSeatAction::NextHand
    });

    Rectangle backBounds = {
        layout.contentX,
        panelBounds.y + panelBounds.height - layout.pad - layout.backHeight,
        layout.contentW,
        layout.backHeight
    };
    actionButtons.push_back({
        makeButtonPtr("Back", backBounds, regularFont, buttonStyle),
        BlackjackSeatAction::LeaveTable
    });
}

void BlackjackPanel::refreshButtons()
{
    if (gameState == nullptr || !gameState->active)
        return;

    const bool playerTurn = gameState->phase == BlackjackHandState::PlayerTurn;
    const bool handComplete = gameState->phase == BlackjackHandState::HandComplete;
    const bool canCheat = playerStats != nullptr
        && playerStats->resolve >= kBlackjackResolveCheatThreshold;
    const bool canAccuse = canCheat && gameState->opponentCheatingSuspected;

    for (ActionSlot& entry : actionButtons)
    {
        if (entry.button == nullptr)
            continue;

        bool enabled = false;
        switch (entry.action)
        {
            case BlackjackSeatAction::Hit:
            case BlackjackSeatAction::Stand:
                enabled = playerTurn;
                break;
            case BlackjackSeatAction::AttemptCheat:
                enabled = playerTurn && canCheat && !gameState->playerCheatingThisHand;
                break;
            case BlackjackSeatAction::AccuseCheat:
                enabled = playerTurn && canAccuse;
                break;
            case BlackjackSeatAction::NextHand:
                enabled = handComplete;
                break;
            case BlackjackSeatAction::LeaveTable:
                enabled = true;
                break;
            default:
                break;
        }

        entry.button->setEnabled(enabled);
        entry.button->setStyle(buttonStyle);
    }
}

void BlackjackPanel::update()
{
    if (gameState == nullptr || !gameState->active)
        return;

    refreshButtons();

    const Vector2 mouse = GetMousePosition();
    const bool mouseDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON);

    for (ActionSlot& entry : actionButtons)
    {
        if (entry.button == nullptr || !entry.button->isEnabled())
        {
            if (entry.button != nullptr)
                entry.button->setState(NORMAL);
            continue;
        }

        const Rectangle bounds = entry.button->getBounds();
        if (CheckCollisionPointRec(mouse, bounds))
            entry.button->setState(mouseDown ? PRESSED : HOVERED);
        else
            entry.button->setState(NORMAL);
    }
}

Color BlackjackPanel::suitColor(int suit) const
{
    return (suit % 4 == 1 || suit % 4 == 2)
        ? Color{ 176, 48, 48, 255 }
        : Color{ 32, 32, 40, 255 };
}

void BlackjackPanel::drawSuitGlyph(int suit, Vector2 center, float size, Color color) const
{
    const float half = size * 0.5f;
    switch (suit % 4)
    {
        case 0: // Spades
        {
            const float r = size * 0.22f;
            DrawCircleV({ center.x - r * 0.95f, center.y - half * 0.18f }, r, color);
            DrawCircleV({ center.x + r * 0.95f, center.y - half * 0.18f }, r, color);
            DrawTriangle(
                { center.x, center.y - half * 0.95f },
                { center.x - half * 0.72f, center.y + half * 0.18f },
                { center.x + half * 0.72f, center.y + half * 0.18f },
                color);
            DrawRectangle(
                static_cast<int>(center.x - size * 0.08f),
                static_cast<int>(center.y + half * 0.18f),
                static_cast<int>(size * 0.16f),
                static_cast<int>(size * 0.22f),
                color);
            break;
        }
        case 1: // Hearts
        {
            const float r = size * 0.24f;
            DrawCircleV({ center.x - r * 0.9f, center.y - half * 0.05f }, r, color);
            DrawCircleV({ center.x + r * 0.9f, center.y - half * 0.05f }, r, color);
            DrawTriangle(
                { center.x - half * 0.82f, center.y - half * 0.02f },
                { center.x + half * 0.82f, center.y - half * 0.02f },
                { center.x, center.y + half * 0.92f },
                color);
            break;
        }
        case 2: // Diamonds
        {
            DrawTriangle(
                { center.x, center.y - half * 0.9f },
                { center.x - half * 0.62f, center.y },
                { center.x, center.y + half * 0.9f },
                color);
            DrawTriangle(
                { center.x, center.y - half * 0.9f },
                { center.x + half * 0.62f, center.y },
                { center.x, center.y + half * 0.9f },
                color);
            break;
        }
        case 3: // Clubs
        {
            const float r = size * 0.19f;
            DrawCircleV({ center.x, center.y - half * 0.34f }, r, color);
            DrawCircleV({ center.x - r * 1.05f, center.y + half * 0.02f }, r, color);
            DrawCircleV({ center.x + r * 1.05f, center.y + half * 0.02f }, r, color);
            DrawTriangle(
                { center.x, center.y + half * 0.12f },
                { center.x - half * 0.34f, center.y + half * 0.72f },
                { center.x + half * 0.34f, center.y + half * 0.72f },
                color);
            DrawRectangle(
                static_cast<int>(center.x - size * 0.07f),
                static_cast<int>(center.y + half * 0.58f),
                static_cast<int>(size * 0.14f),
                static_cast<int>(size * 0.2f),
                color);
            break;
        }
        default:
            break;
    }
}

void BlackjackPanel::drawSectionLabel(const char* label, float x, float y) const
{
    DrawTextEx(boldFont, label, { x, y }, 16.0f, 1.0f, kSectionLabel);
}

void BlackjackPanel::drawCard(const BlackjackCard& card, Rectangle bounds, bool faceDown) const
{
    DrawRectangleRounded(bounds, 0.12f, 8, faceDown ? kCardBack : kCardFace);
    DrawRoundedBorder(bounds, 0.12f, 8, 2.0f, kPanelBorder);

    if (faceDown)
    {
        Rectangle inset = {
            bounds.x + 8.0f,
            bounds.y + 8.0f,
            bounds.width - 16.0f,
            bounds.height - 16.0f
        };
        DrawRectangleRounded(inset, 0.1f, 6, Color{ 58, 24, 24, 255 });
        return;
    }

    const char* rank = blackjackCardRankLabel(card.rank);
    const Color ink = suitColor(card.suit);
    const float cornerRankSize = 18.0f;
    const float cornerSuitSize = 14.0f;
    const float centerSuitSize = bounds.width * 0.42f;

    DrawTextEx(
        boldFont,
        rank,
        { bounds.x + 7.0f, bounds.y + 5.0f },
        cornerRankSize,
        1.0f,
        ink);
    drawSuitGlyph(
        card.suit,
        { bounds.x + 14.0f, bounds.y + 30.0f },
        cornerSuitSize,
        ink);
    drawSuitGlyph(
        card.suit,
        {
            bounds.x + bounds.width * 0.5f,
            bounds.y + bounds.height * 0.56f
        },
        centerSuitSize,
        ink);

    Vector2 rankSize = MeasureTextEx(boldFont, rank, cornerRankSize, 1.0f);
    DrawTextEx(
        boldFont,
        rank,
        {
            bounds.x + bounds.width - rankSize.x - 7.0f,
            bounds.y + bounds.height - 24.0f
        },
        cornerRankSize,
        1.0f,
        ink);
    drawSuitGlyph(
        card.suit,
        {
            bounds.x + bounds.width - 14.0f,
            bounds.y + bounds.height - 11.0f
        },
        cornerSuitSize,
        ink);
}

void BlackjackPanel::drawHand(
    const std::vector<BlackjackCard>& hand,
    float x,
    float y,
    float cardW,
    float cardH,
    float overlap,
    bool hideHoleCard) const
{
    for (size_t i = 0; i < hand.size(); ++i)
    {
        const bool faceDown = hideHoleCard && i == 1;
        Rectangle cardBounds = {
            x + static_cast<float>(i) * overlap,
            y,
            cardW,
            cardH
        };
        drawCard(hand[i], cardBounds, faceDown);
    }
}

void BlackjackPanel::draw() const
{
    if (gameState == nullptr || !gameState->active || regularFont.texture.id == 0)
        return;

    DrawRectangleRounded(panelBounds, 0.04f, 10, kPanelFill);
    DrawRoundedBorder(panelBounds, 0.04f, 10, 3.0f, kPanelBorder);

    const PanelLayout layout = computePanelLayout(panelBounds);
    const float x = layout.contentX;
    float y = panelBounds.y + layout.pad;

    const char* dealerName = blackjackOpponentName(gameState->seatedOpponentIndex);
    drawSectionLabel("Blackjack Table", x, y);
    y += 24.0f;

    char headerLine[192];
    const int walletDollars = playerStats != nullptr
        ? static_cast<int>(playerStats->walletCash)
        : 0;
    snprintf(
        headerLine,
        sizeof(headerLine),
        "Dealer: %s   Pot: $%d   Bet: $%d   Cash: $%d",
        dealerName,
        gameState->potDollars,
        gameState->currentBetDollars,
        walletDollars);
    DrawTextEx(regularFont, headerLine, { x, y }, 16.0f, 1.0f, kMutedText);
    y += 24.0f;

    if (!gameState->statusLine.empty())
    {
        DrawTextEx(regularFont, gameState->statusLine.c_str(), { x, y }, 15.0f, 1.0f, LIGHTGRAY);
        y += 22.0f;
    }

    const float cardW = 52.0f;
    const float cardH = 74.0f;
    const float overlap = 36.0f;
    const float cardsY = std::min(std::max(y + 8.0f, layout.cardsY), layout.cardsY);
    const float labelY = cardsY - 18.0f;

    const float opponentSpan = handSpan(gameState->opponentHand.size(), cardW, overlap);
    const float playerSpan = handSpan(gameState->playerHand.size(), cardW, overlap);
    const float playerX = layout.contentRight - playerSpan;

    drawSectionLabel("Opponent", x, labelY);
    if (!gameState->opponentHand.empty())
    {
        drawHand(
            gameState->opponentHand,
            x,
            cardsY,
            cardW,
            cardH,
            overlap,
            gameState->hideOpponentHole && !gameState->showAllOpponentCards);

        std::string opponentTotalText = "?";
        if (gameState->showAllOpponentCards || !gameState->hideOpponentHole)
            opponentTotalText = blackjackFormatHandTotal(
                blackjackEvaluateHand(gameState->opponentHand));

        DrawTextEx(
            regularFont,
            opponentTotalText.c_str(),
            { x + opponentSpan + 8.0f, cardsY + cardH + 4.0f },
            16.0f,
            1.0f,
            kMutedText);
    }

    {
        const char* playerLabel = "Your Hand";
        Vector2 labelSize = MeasureTextEx(boldFont, playerLabel, 16.0f, 1.0f);
        drawSectionLabel(
            playerLabel,
            layout.contentRight - labelSize.x,
            labelY);
    }

    if (!gameState->playerHand.empty())
    {
        drawHand(gameState->playerHand, playerX, cardsY, cardW, cardH, overlap, false);
        const std::string total = blackjackFormatHandTotal(
            blackjackEvaluateHand(gameState->playerHand));
        Vector2 totalSize = MeasureTextEx(regularFont, total.c_str(), 16.0f, 1.0f);
        DrawTextEx(
            regularFont,
            total.c_str(),
            { playerX - totalSize.x - 8.0f, cardsY + cardH + 4.0f },
            16.0f,
            1.0f,
            LIGHTGRAY);
    }

    float footerY = layout.resultY;
    if (!gameState->resultLine.empty())
    {
        DrawTextEx(boldFont, gameState->resultLine.c_str(), { x, footerY }, 17.0f, 1.0f, GOLD);
        footerY += 22.0f;
    }

    if (gameState->opponentCheatingSuspected)
    {
        DrawTextEx(
            regularFont,
            "You suspect a sleight.",
            { x, footerY },
            14.0f,
            1.0f,
            ORANGE);
    }

    for (const ActionSlot& entry : actionButtons)
    {
        if (entry.button == nullptr)
            continue;

        if (entry.action == BlackjackSeatAction::LeaveTable)
        {
            entry.button->draw();
            continue;
        }

        if (entry.action == BlackjackSeatAction::NextHand)
        {
            if (gameState->phase == BlackjackHandState::HandComplete)
                entry.button->draw();
            continue;
        }

        if (gameState->phase == BlackjackHandState::PlayerTurn)
            entry.button->draw();
    }
}

BlackjackSeatAction BlackjackPanel::consumeClickedAction()
{
    if (gameState == nullptr || !gameState->active)
        return BlackjackSeatAction::None;

    if (!IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        return BlackjackSeatAction::None;

    const Vector2 mouse = GetMousePosition();

    for (const ActionSlot& entry : actionButtons)
    {
        if (entry.button == nullptr || !entry.button->isEnabled())
            continue;

        if (entry.action == BlackjackSeatAction::NextHand
            && gameState->phase != BlackjackHandState::HandComplete)
            continue;

        if (entry.action != BlackjackSeatAction::NextHand
            && entry.action != BlackjackSeatAction::LeaveTable
            && gameState->phase == BlackjackHandState::HandComplete)
            continue;

        if (entry.action != BlackjackSeatAction::NextHand
            && entry.action != BlackjackSeatAction::LeaveTable
            && gameState->phase != BlackjackHandState::PlayerTurn)
            continue;

        if (CheckCollisionPointRec(mouse, entry.button->getBounds()))
            return entry.action;
    }

    return BlackjackSeatAction::None;
}

}