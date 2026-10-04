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

#ifndef BUTTON_MGR_H
#define BUTTON_MGR_H

#include "Button.h"
#include <ActionStruct.h>
#include <MovementBlockReason.h>
#include <MovementStruct.h>
#include <UiBackdrop.h>
#include <raylib.h>
#include <vector>

namespace timberline_engine
{

    class ButtonMgr
    {
        public:
        ButtonMgr(Rectangle _buttonBox, Font buttonFont, Font boldButtonFont);
        ~ButtonMgr();
        void update();
        void draw() const;
        void setAvailability(
            const MovementStruct& movement,
            const ActionStruct& actions,
            bool enableInventory = true);
        void setMovementBlockOverlays(const MovementBlockOverlays& overlays);
        void setStatus(
            float healthPercent,
            float energyPercent,
            float resolvePercent,
            float lucidityPercent,
            float charismaPercent);
        void relayout(Rectangle newButtonBox);
        void setClickHoldDuration(float seconds);
        void setUiBackdrop(const UiBackdrop* backdrop);

        bool isUpButtonPressed() const { return upButtonPressed; }
        bool isDownButtonPressed() const { return downButtonPressed; }
        bool isForwardButtonPressed() const { return forwardButtonPressed; }
        bool isBackButtonPressed() const { return backButtonPressed; }
        bool isLeftButtonPressed() const { return leftButtonPressed; }
        bool isRightButtonPressed() const { return rightButtonPressed; }
        bool isExamineButtonPressed() const { return examineButtonPressed; }
        bool consumeExamineButtonClick();
        bool consumeSpeakButtonClick();
        bool consumeUseButtonClick();
        bool consumeTakeButtonClick();
        bool consumeUpButtonClick();
        bool consumeDownButtonClick();
        bool consumeForwardButtonClick();
        bool consumeBackwardButtonClick();
        bool consumeLeftButtonClick();
        bool consumeRightButtonClick();
        bool isSpeakButtonPressed() const { return speakButtonPressed; }
        bool isInventoryButtonPressed() const { return inventoryButtonPressed; }
        bool consumeInventoryButtonClick();
        bool consumeMoveOrActionButtonClick();
        bool consumeHitButtonClick();
        bool isHitButtonPressed() const { return hitButtonPressed; }
        bool isUseButtonPressed() const { return useButtonPressed; }

        private:
        void addButton(const char* label, Rectangle bounds);
        void buildButtonLayout();
        void drawSectionLabel(const char* label, float x, float y) const;
        void drawStatusBar(const char* label, Rectangle bounds, float percent) const;
        void drawDisabledMovementChrome(Rectangle bounds) const;
        void drawMovementBlockBadge(int buttonIndex, Rectangle bounds) const;
        bool movementIndexHasBlockIcon(int buttonIndex) const;
        void updatePressedFlags();
        void registerButtonClick(int buttonIndex);
        int findEnabledButtonUnderMouse(Vector2 mousePos) const;
        void refreshButtonStyles();
        void ensureBlockIconsLoaded() const;
        const Texture2D* iconForBlockReason(MovementBlockReason reason) const;
        MovementBlockReason blockReasonForMovementIndex(int buttonIndex) const;

        bool upButtonPressed = false;
        bool downButtonPressed = false;
        bool forwardButtonPressed = false;
        bool backButtonPressed = false;
        bool leftButtonPressed = false;
        bool rightButtonPressed = false;
        bool examineButtonPressed = false;
        bool examineButtonClicked = false;
        bool speakButtonClicked = false;
        bool useButtonClicked = false;
        bool takeButtonClicked = false;
        bool hitButtonClicked = false;
        bool inventoryButtonClicked = false;
        bool moveOrActionButtonClicked = false;
        bool upButtonClicked = false;
        bool downButtonClicked = false;
        bool forwardButtonClicked = false;
        bool backwardButtonClicked = false;
        bool leftButtonClicked = false;
        bool rightButtonClicked = false;
        bool speakButtonPressed = false;
        bool inventoryButtonPressed = false;
        bool hitButtonPressed = false;
        bool useButtonPressed = false;

        float healthPercent = 90.0f;
        float energyPercent = 20.0f;
        float resolvePercent = 50.0f;
        float lucidityPercent = 30.0f;
        float charismaPercent = 50.0f;
        int activePressButtonIndex = -1;
        double activePressStartTime = 0.0;
        bool activePressClickFired = false;
        bool mouseWasDownLastFrame = false;
        float clickHoldDurationSeconds = 0.07f;

        Rectangle buttonBox;
        Rectangle healthBarBounds;
        Rectangle energyBarBounds;
        Rectangle resolveBarBounds;
        Rectangle lucidityBarBounds;
        Rectangle charismaBarBounds;
        Rectangle reservedBarBounds;
        Font buttonFont;
        Font boldButtonFont;
        ButtonStyle baseButtonStyle;
        ButtonStyle buttonStyle;
        const UiBackdrop* uiBackdrop = nullptr;
        std::vector<Button> buttons;
        MovementBlockOverlays movementBlockOverlays{};
        mutable bool blockIconsLoaded = false;
        mutable Texture2D needsLightIcon{};
        mutable Texture2D needsGearIcon{};
        mutable Texture2D needsLockIcon{};
    };
}

#endif /* BUTTON_MGR_H */
