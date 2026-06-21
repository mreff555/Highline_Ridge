#ifndef INTERACTION_MGR_H
#define INTERACTION_MGR_H

#include <SceneInteractionDef.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace testgame
{

class InteractionMgr
{
    public:
    InteractionMgr();
    ~InteractionMgr() = default;

    void setPanelBounds(Rectangle bounds);
    void setFont(Font font);

    bool isOpen() const { return openState; }
    void open();
    void close();

    void setAvailableInteractions(const std::vector<SceneInteractionDef>& interactions);
    bool isEmpty() const { return options.empty(); }

    std::string consumePendingInteractionId();

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
    bool openState = false;
    std::vector<SceneInteractionDef> options;
    mutable std::vector<Rectangle> optionBounds;
    std::string pendingInteractionId;
};

}

#endif /* INTERACTION_MGR_H */