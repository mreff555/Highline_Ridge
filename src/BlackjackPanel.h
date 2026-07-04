#ifndef BLACKJACK_PANEL_H
#define BLACKJACK_PANEL_H

#include "BlackjackGame.h"
#include "Button.h"
#include <memory>
#include <raylib.h>
#include <vector>

namespace highline_ridge
{

class UiBackdrop;

class BlackjackPanel
{
    public:
    void setGameState(const BlackjackGameState* state) { gameState = state; }
    void setPlayerStats(const PlayerStats* stats) { playerStats = stats; }
    void setFonts(Font regularFont, Font boldFont);
    void setPanelBounds(Rectangle bounds);
    void setUiBackdrop(const UiBackdrop* backdrop);
    void update();
    void draw() const;
    BlackjackSeatAction consumeClickedAction();

    private:
    struct ActionSlot
    {
        std::unique_ptr<Button> button;
        BlackjackSeatAction action = BlackjackSeatAction::None;
    };

    void rebuildLayout();
    void refreshButtons();
    void drawCard(const BlackjackCard& card, Rectangle bounds, bool faceDown) const;
    void drawSuitGlyph(int suit, Vector2 center, float size, Color color) const;
    void drawHand(
        const std::vector<BlackjackCard>& hand,
        float x,
        float y,
        float cardW,
        float cardH,
        float overlap,
        bool hideHoleCard) const;
    void drawSectionLabel(const char* label, float x, float y) const;
    Color suitColor(int suit) const;

    const BlackjackGameState* gameState = nullptr;
    const PlayerStats* playerStats = nullptr;
    const UiBackdrop* uiBackdrop = nullptr;
    Rectangle panelBounds{};
    Font regularFont{};
    Font boldFont{};
    ButtonStyle buttonStyle{};
    ButtonStyle baseButtonStyle{};
    std::vector<ActionSlot> actionButtons;
};

}

#endif /* BLACKJACK_PANEL_H */