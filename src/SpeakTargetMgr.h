#ifndef SPEAK_TARGET_MGR_H
#define SPEAK_TARGET_MGR_H

#include <ScrollPanel.h>
#include <SpeakTargetDef.h>
#include <UiBackdrop.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace highline_ridge
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
    void handleScrollInput();
    void layoutOptionBounds();
    Rectangle getContentBounds() const;
    Rectangle getCloseButtonBounds() const;
    float getRowHeight() const;
    float getContentHeight() const;

    static const float kCloseButtonSize;
    static const float kRowGap;
    static const float kScrollbarWidth;

    Font panelFont{};
    Rectangle panelBounds{};
    const UiBackdrop* uiBackdrop = nullptr;
    ScrollPanel optionScroll;
    bool openState = false;
    std::vector<SpeakTargetDef> options;
    mutable std::vector<Rectangle> optionBounds;
    SpeakTargetDef pendingTarget;
};

}

#endif /* SPEAK_TARGET_MGR_H */