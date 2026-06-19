#ifndef INVENTORY_MGR_H
#define INVENTORY_MGR_H

#include <InventoryItem.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace testgame
{

enum class InventoryViewState
{
    Closed,
    ItemList,
    ExaminingItem
};

class InventoryMgr
{
    public:
    InventoryMgr();
    ~InventoryMgr();

    void setPanelBounds(Rectangle bounds);
    void setFont(Font font);
    bool initializeAssets(const std::string& primaryAssetRoot, const std::string& fallbackAssetRoot);

    bool isOpen() const { return viewState != InventoryViewState::Closed; }
    bool isExaminingItem() const { return viewState == InventoryViewState::ExaminingItem; }
    InventoryViewState getViewState() const { return viewState; }

    void open();
    void close();
    void returnToItemList();
    const std::string& getSelectedItemId() const { return selectedItemId; }
    bool hasSelectedItem() const { return !selectedItemId.empty(); }
    bool canExamineSelectedItem() const;

    void update();
    void draw() const;

    void examineSelectedItem();
    const InventoryItem* getSelectedItem() const;

    private:
    void createDefaultItems();
    void loadItemTextures(const std::string& primaryAssetRoot, const std::string& fallbackAssetRoot);
    Texture2D loadItemTexture(const char* filename, const std::string& primaryAssetRoot, const std::string& fallbackAssetRoot) const;
    void drawCloseButton() const;
    void drawItemGrid() const;
    void handleItemGridInput();
    void handleCloseButtonInput();
    void handleInventoryScrollInput();
    void drawInventoryScrollbar() const;
    void layoutItemSlots();
    Rectangle getCloseButtonBounds() const;
    float getInventoryVisibleHeight() const;
    const InventoryItem* findItem(const std::string& id) const;
    int findItemIndex(const std::string& id) const;

    static const float kScrollbarWidth;
    static const float kCloseButtonSize;
    static const float kItemSlotSize;
    static const float kItemGap;

    Font panelFont{};
    Rectangle panelBounds{};
    InventoryViewState viewState = InventoryViewState::Closed;
    std::string selectedItemId;
    std::vector<InventoryItem> items;
    mutable std::vector<Rectangle> itemSlotBounds;

    float inventoryScrollY = 0.0f;
    float inventoryContentHeight = 0.0f;
    bool inventoryScrollbarDragging = false;
    float inventoryScrollbarDragOffsetY = 0.0f;
    bool mouseWasDownLastFrame = false;
};

}

#endif /* INVENTORY_MGR_H */
