#ifndef BUTTON_MGR_H
#define BUTTON_MGR_H

#include "Button.h"
#include <ActionStruct.h>
#include <MovementStruct.h>
#include <UiBackdrop.h>
#include <raylib.h>
#include <vector>

namespace highline_ridge
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
        void updatePressedFlags();
        void registerButtonClick(int buttonIndex);
        int findEnabledButtonUnderMouse(Vector2 mousePos) const;
        void refreshButtonStyles();

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
    };
}

#endif /* BUTTON_MGR_H */
