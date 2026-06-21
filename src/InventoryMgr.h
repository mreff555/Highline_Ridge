#ifndef INVENTORY_MGR_H
#define INVENTORY_MGR_H

#include <InventoryItem.h>
#include <ItemDatabase.h>
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
    void setAssetRoots(const std::string& primaryAssetRoot, const std::string& fallbackAssetRoot);
    void setItemDatabase(const ItemDatabase* database);
    bool ensureAssetsLoaded();
    bool ensureIconAssetsLoaded();
    void reloadItemIconsIfNeeded();

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
    const InventoryItem* getItemById(const std::string& id) const;
    bool hasItem(const std::string& id) const;
    void addItem(const InventoryItem& item);
    bool removeItem(const std::string& id);
    std::vector<InventoryItem> exportItemSnapshots() const;
    void restoreFromSnapshots(const std::vector<InventoryItem>& savedItems);

    std::string consumePendingDropItemId();

    private:
    void createDefaultItems();
    void loadItemTextures();
    void loadItemAssets(InventoryItem& item);
    void loadItemIcon(InventoryItem& item);
    void loadItemExamineImage(InventoryItem& item);
    void ensureItemIconLoaded(InventoryItem& item);
    void ensureItemExamineImageLoaded(InventoryItem& item);
    bool loadItemTexture(const char* filename, Texture2D& outTexture) const;
    bool hasLoadedAssets() const;
    bool isItemIconReady(const InventoryItem& item) const;
    void drawCloseButton() const;
    void drawItemGrid() const;
    void handleItemGridInput();
    void handleCloseButtonInput();
    void handleInventoryScrollInput();
    void drawInventoryScrollbar() const;
    void layoutItemSlots() const;
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
    std::string pendingDropItemId;
    std::vector<InventoryItem> items;
    mutable std::vector<Rectangle> itemSlotBounds;
    std::string primaryAssetRoot;
    std::string fallbackAssetRoot;
    const ItemDatabase* itemDatabase = nullptr;

    float inventoryScrollY = 0.0f;
    mutable float inventoryContentHeight = 0.0f;
    bool inventoryScrollbarDragging = false;
    float inventoryScrollbarDragOffsetY = 0.0f;
};

}

#endif /* INVENTORY_MGR_H */
