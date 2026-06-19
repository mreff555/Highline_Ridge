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

        bool isForwardButtonPressed() const { return forwardButtonPressed; }
        bool isBackButtonPressed() const { return backButtonPressed; }
        bool isLeftButtonPressed() const { return leftButtonPressed; }
        bool isRightButtonPressed() const { return rightButtonPressed; }
        bool isExamineButtonPressed() const { return examineButtonPressed; }
        bool consumeExamineButtonClick();
        bool isSpeakButtonPressed() const { return speakButtonPressed; }
        bool isInventoryButtonPressed() const { return inventoryButtonPressed; }
        bool isHitButtonPressed() const { return hitButtonPressed; }
        bool isUseButtonPressed() const { return useButtonPressed; }

        private:
        void addButton(const char* label, Rectangle bounds);
        void drawSectionLabel(const char* label, float x, float y) const;
        void updatePressedFlags();

        bool forwardButtonPressed = false;
        bool backButtonPressed = false;
        bool leftButtonPressed = false;
        bool rightButtonPressed = false;
        bool examineButtonPressed = false;
        bool examineButtonClicked = false;
        bool speakButtonPressed = false;
        bool inventoryButtonPressed = false;
        bool hitButtonPressed = false;
        bool useButtonPressed = false;

        Rectangle buttonBox;
        Font buttonFont;
        ButtonStyle buttonStyle;
        std::vector<Button> buttons;
    };
}

#endif /* BUTTON_MGR_H */