#ifndef SPEAK_TARGET_MGR_H
#define SPEAK_TARGET_MGR_H

#include <SpeakTargetDef.h>
#include <UiBackdrop.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace testgame
{

class SpeakTargetMgr
{
    public:
    SpeakTargetMgr();
    ~SpeakTargetMgr() = default;

    void setPanelBounds(Rectangle bounds);
    void setFont(Font font);
    void setUiBackdrop(const UiBackdrop* backdrop);

    bool isOpen() const { return openState; }
    void open();
    void close();

    void setAvailableTargets(const std::vector<SpeakTargetDef>& targets);
    bool isEmpty() const { return options.empty(); }

    SpeakTargetDef consumePendingTarget();

    void update();
    void draw() const;

    private:
    void drawCloseButton() const;
    void drawOptionList() const;
    void handleCloseButtonInput();
    void handleOptionInput();
    void layoutOptionBounds();
    Rectangle getCloseButtonBounds() const;
    float getRowHeight() const;

    static const float kCloseButtonSize;
    static const float kRowGap;

    Font panelFont{};
    Rectangle panelBounds{};
    const UiBackdrop* uiBackdrop = nullptr;
    bool openState = false;
    std::vector<SpeakTargetDef> options;
    mutable std::vector<Rectangle> optionBounds;
    SpeakTargetDef pendingTarget;
};

}

#endif /* SPEAK_TARGET_MGR_H */