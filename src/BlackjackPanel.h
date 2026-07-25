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

#ifndef BLACKJACK_PANEL_H
#define BLACKJACK_PANEL_H

#include "BlackjackGame.h"
#include "Button.h"
#include <memory>
#include <raylib.h>
#include <vector>

namespace timberline_engine
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