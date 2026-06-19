#ifndef BUTTON_MGR_H
#define BUTTON_MGR_H

#include "Button.h"
#include <ActionStruct.h>
#include <MovementStruct.h>
#include <raylib.h>
#include <vector>

namespace testgame
{

    class ButtonMgr
    {
        public:
        ButtonMgr(Rectangle _buttonBox, Font buttonFont);
        virtual ~ButtonMgr();
        void update();
        void draw() const;
        void setAvailability(const MovementStruct& movement, const ActionStruct& actions);
        void setStatus(float healthPercent, float energyPercent, float tenacityPercent, float lucidityPercent);

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
        bool consumeUpButtonClick();
        bool consumeDownButtonClick();
        bool consumeForwardButtonClick();
        bool consumeBackwardButtonClick();
        bool consumeLeftButtonClick();
        bool consumeRightButtonClick();
        bool isSpeakButtonPressed() const { return speakButtonPressed; }
        bool isInventoryButtonPressed() const { return inventoryButtonPressed; }
        bool consumeInventoryButtonClick();
        bool isHitButtonPressed() const { return hitButtonPressed; }
        bool isUseButtonPressed() const { return useButtonPressed; }

        private:
        void addButton(const char* label, Rectangle bounds);
        void drawSectionLabel(const char* label, float x, float y) const;
        void drawStatusBar(const char* label, Rectangle bounds, float percent) const;
        void updatePressedFlags();
        void registerButtonClick(int buttonIndex);
        int findEnabledButtonUnderMouse(Vector2 mousePos) const;

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
        bool inventoryButtonClicked = false;
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
        float tenacityPercent = 50.0f;
        float lucidityPercent = 30.0f;
        int activePressButtonIndex = -1;
        double activePressStartTime = 0.0;
        bool activePressClickFired = false;
        bool mouseWasDownLastFrame = false;

        Rectangle buttonBox;
        Rectangle healthBarBounds;
        Rectangle energyBarBounds;
        Rectangle tenacityBarBounds;
        Rectangle lucidityBarBounds;
        Font buttonFont;
        ButtonStyle buttonStyle;
        std::vector<Button> buttons;
    };
}

#endif /* BUTTON_MGR_H */
