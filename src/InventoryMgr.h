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

#ifndef INVENTORY_MGR_H
#define INVENTORY_MGR_H

#include <InventoryItem.h>
#include <ItemCombinationDatabase.h>
#include <ItemDatabase.h>
#include <ScrollPanel.h>
#include <UiBackdrop.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_engine
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
    void setItemCombinationDatabase(const ItemCombinationDatabase* database);
    void setUiBackdrop(const UiBackdrop* backdrop);
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
    bool canExtractFromExaminedItem(const ItemDatabase& database) const;
    bool extractFromExaminedItem(const ItemDatabase& database, InventoryItem& outExtracted);
    void refreshItemFromDatabase(const std::string& id);
    void syncWalletDisplay(float walletCash);
    bool applyExamineRevealFlag(const std::string& itemId, const std::string& flag);
    const InventoryItem* getSelectedItem() const;
    const InventoryItem* getItemById(const std::string& id) const;
    bool hasItem(const std::string& id) const;
    void addItem(const InventoryItem& item);
    /**
     * Developer / grant helper: add a new item, or if stackable and already
     * owned, raise quantity. Non-stackable duplicates are ignored.
     */
    bool giveOrStackItem(const InventoryItem& item, std::string& message);
    bool removeItem(const std::string& id);
    std::vector<InventoryItem> exportItemSnapshots() const;
    void restoreFromSnapshots(const std::vector<InventoryItem>& savedItems);

    std::string consumePendingDropItemId();
    bool consumeItemCombinationApplied();
    std::string consumePendingCombinationNarrative();
    ItemTtsDef consumePendingCombinationNarrativeTts();
    bool consumePendingCombinationTtsOwnerEnabled();

    float totalCarryWeightLb() const;
    static float maxCarryWeightLb() { return kMaxCarryWeightLb; }

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
    void handleItemCombineInput();
    bool applyItemCombination(const ItemCombineApplication& application);
    bool applyComponentSpends(const std::vector<ItemCombineComponentSpend>& spends);
    void handleCraftChooserInput();
    void drawCraftChooser() const;
    void openCraftChooser(
        const std::string& firstItemId,
        const std::string& secondItemId,
        std::vector<ItemCraftCandidate> candidates);
    void closeCraftChooser();
    void drawDragGhost() const;
    void drawUndefinedItemIcon(const Rectangle& iconArea) const;
    void drawWalletCashBadge(const Rectangle& slot) const;
    int findItemSlotAtMouse() const;
    void handleCloseButtonInput();
    void handleInventoryScrollInput();
    void drawInventoryScrollbar() const;
    void layoutItemSlots() const;
    Rectangle getCloseButtonBounds() const;
    float getInventoryVisibleHeight() const;
    const InventoryItem* findItem(const std::string& id) const;
    InventoryItem* findMutableItem(const std::string& id);
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
    const ItemCombinationDatabase* itemCombinationDatabase = nullptr;
    const UiBackdrop* uiBackdrop = nullptr;

    std::string dragItemId;
    std::string pendingPressItemId;
    Vector2 pressStartPos{};
    bool isDraggingItem = false;
    bool pendingItemCombinationApplied = false;
    std::string pendingCombinationNarrative;
    ItemTtsDef pendingCombinationNarrativeTts{};
    bool pendingCombinationTtsOwnerEnabled = false;

    // Multi-product craft chooser (same component pair, several products).
    bool craftChooserOpen = false;
    std::string craftChooserFirstId;
    std::string craftChooserSecondId;
    std::vector<ItemCraftCandidate> craftChooserCandidates;

    ScrollPanel inventoryScroll;
    mutable float inventoryContentHeight = 0.0f;
    float walletCash = 20.0f;
};

}

#endif /* INVENTORY_MGR_H */
