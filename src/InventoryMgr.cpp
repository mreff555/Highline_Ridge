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

#include <InventoryMgr.h>
#include <ItemInstance.h>
#include <RaylibCompat.h>
#include <SceneLoader.h>
#include <algorithm>
#include <string>

namespace timberline_engine
{

namespace
{
    const Color kPanelFill = {28, 26, 34, 255};
    const Color kPanelBorder = {168, 138, 72, 255};
    const Color kPanelAccent = {96, 78, 48, 255};
    const Color kSectionLabel = {132, 122, 104, 255};
    const Color kScrollTrack = {48, 44, 56, 255};
    const Color kScrollThumb = {140, 118, 72, 255};
    const Color kScrollThumbHover = {168, 142, 88, 255};
    const Color kSlotFill = {40, 38, 50, 255};
    const Color kSlotHover = {54, 50, 64, 255};
    const Color kSlotSelected = {62, 52, 34, 255};
    const Color kSlotCombineTarget = {72, 58, 36, 255};
    const Color kCloseHover = {210, 178, 108, 255};
    const float kDragStartThreshold = 8.0f;

    bool itemNeedsExamineImage(const InventoryItem& item)
    {
        return !item.examineImagePath.empty();
    }
}

const float InventoryMgr::kScrollbarWidth = 16.0f;
const float InventoryMgr::kCloseButtonSize = 28.0f;
const float InventoryMgr::kItemSlotSize = 76.0f;
const float InventoryMgr::kItemGap = 12.0f;

InventoryMgr::InventoryMgr() = default;

InventoryMgr::~InventoryMgr()
{
    for (InventoryItem& item : items)
    {
        if (item.icon.id != 0)
            UnloadTexture(item.icon);
        if (item.examineImage.id != 0)
            UnloadTexture(item.examineImage);
    }
}

void InventoryMgr::setPanelBounds(Rectangle bounds)
{
    panelBounds = bounds;
}

void InventoryMgr::setFont(Font font)
{
    panelFont = font;
}

void InventoryMgr::setAssetRoots(
    const std::string& primaryRoot,
    const std::string& fallbackRoot)
{
    primaryAssetRoot = primaryRoot;
    fallbackAssetRoot = fallbackRoot;
}

void InventoryMgr::setItemDatabase(const ItemDatabase* database)
{
    itemDatabase = database;
    if (items.empty())
        createDefaultItems();
}

void InventoryMgr::setItemCombinationDatabase(const ItemCombinationDatabase* database)
{
    itemCombinationDatabase = database;
}

void InventoryMgr::setUiBackdrop(const UiBackdrop* backdrop)
{
    uiBackdrop = backdrop;
}

bool InventoryMgr::isItemIconReady(const InventoryItem& item) const
{
    return item.isUndefined || (item.icon.id != 0 && IsTextureValid(item.icon));
}

bool InventoryMgr::hasLoadedAssets() const
{
    for (const InventoryItem& item : items)
    {
        if (!item.isUndefined && !isItemIconReady(item))
            return false;
    }

    return !items.empty();
}

bool InventoryMgr::loadItemTexture(const char* filename, Texture2D& outTexture) const
{
    const std::string relativePath = std::string("resources/") + filename;

    if (loadResourceTexture(primaryAssetRoot, relativePath, outTexture))
        return true;

    if (fallbackAssetRoot != primaryAssetRoot &&
        loadResourceTexture(fallbackAssetRoot, relativePath, outTexture))
    {
        return true;
    }

    TraceLog(LOG_ERROR, "Failed to load inventory image: %s", filename);
    return false;
}

void InventoryMgr::loadItemIcon(InventoryItem& item)
{
    if (item.iconPath.empty())
        return;

    const std::string path = item.iconPath.find("resources/") == 0
        ? item.iconPath.substr(10)
        : item.iconPath;
    if (loadItemTexture(path.c_str(), item.icon))
        SetTextureFilter(item.icon, TEXTURE_FILTER_BILINEAR);
}

void InventoryMgr::loadItemExamineImage(InventoryItem& item)
{
    if (!itemNeedsExamineImage(item))
        return;

    if (item.examineImagePath.empty())
        return;

    const std::string path = item.examineImagePath.find("resources/") == 0
        ? item.examineImagePath.substr(10)
        : item.examineImagePath;
    if (loadItemTexture(path.c_str(), item.examineImage))
        SetTextureFilter(item.examineImage, TEXTURE_FILTER_BILINEAR);
}

void InventoryMgr::loadItemAssets(InventoryItem& item)
{
    if (item.icon.id != 0)
    {
        UnloadTexture(item.icon);
        item.icon = Texture2D{};
    }
    if (item.examineImage.id != 0)
    {
        UnloadTexture(item.examineImage);
        item.examineImage = Texture2D{};
    }

    loadItemIcon(item);
    loadItemExamineImage(item);
}

void InventoryMgr::ensureItemIconLoaded(InventoryItem& item)
{
    if (isItemIconReady(item))
        return;

    if (item.icon.id != 0)
    {
        UnloadTexture(item.icon);
        item.icon = Texture2D{};
    }

    loadItemIcon(item);
}

void InventoryMgr::ensureItemExamineImageLoaded(InventoryItem& item)
{
    if (!itemNeedsExamineImage(item))
        return;

    if (item.examineImage.id != 0 && IsTextureValid(item.examineImage))
        return;

    if (item.examineImage.id != 0)
    {
        UnloadTexture(item.examineImage);
        item.examineImage = Texture2D{};
    }

    loadItemExamineImage(item);
}

void InventoryMgr::loadItemTextures()
{
    for (InventoryItem& item : items)
        loadItemAssets(item);
}

bool InventoryMgr::ensureIconAssetsLoaded()
{
    for (InventoryItem& item : items)
        ensureItemIconLoaded(item);

    return hasLoadedAssets();
}

void InventoryMgr::reloadItemIconsIfNeeded()
{
    ensureIconAssetsLoaded();
}

bool InventoryMgr::ensureAssetsLoaded()
{
    if (hasLoadedAssets())
        return true;

    loadItemTextures();
    return hasLoadedAssets();
}

void InventoryMgr::createDefaultItems()
{
    items.clear();

    if (itemDatabase != nullptr)
    {
        if (itemDatabase->hasDef("wallet"))
        {
            InventoryItem walletItem = itemDatabase->buildInventoryItem(
                itemDatabase->createInstance("wallet"));
            walletItem.examineText = ItemDatabase::appendWalletCashDescription(
                walletItem.examineText,
                walletCash);
            items.push_back(walletItem);
        }
        if (itemDatabase->hasDef("independence_hall_locket"))
        {
            items.push_back(itemDatabase->buildInventoryItem(
                itemDatabase->createInstance("independence_hall_locket")));
        }
        if (!items.empty())
            return;
    }

    InventoryItem wallet;
    wallet.id = "wallet";
    wallet.instance.defId = "wallet";
    wallet.instance.instanceId = "wallet";
    wallet.name = "Wallet";
    wallet.examineText = ItemDatabase::appendWalletCashDescription(
        "The wallet is worked from thick full-grain leather, hand-stitched along the edges "
        "with waxed thread the color of strong tea.",
        walletCash);
    items.push_back(wallet);
}

void InventoryMgr::open()
{
    if (!ensureIconAssetsLoaded())
        TraceLog(LOG_WARNING, "Some inventory icons failed to load");

    viewState = InventoryViewState::ItemList;
    selectedItemId.clear();
    inventoryScroll.resetScroll();
    dragItemId.clear();
    pendingPressItemId.clear();
    isDraggingItem = false;
}

void InventoryMgr::close()
{
    viewState = InventoryViewState::Closed;
    selectedItemId.clear();
    inventoryScroll.resetScroll();
    dragItemId.clear();
    pendingPressItemId.clear();
    isDraggingItem = false;
}

void InventoryMgr::returnToItemList()
{
    viewState = InventoryViewState::ItemList;
    inventoryScroll.resetScroll();
}

bool InventoryMgr::canExamineSelectedItem() const
{
    if (viewState != InventoryViewState::ItemList || selectedItemId.empty())
        return false;

    const InventoryItem* item = findItem(selectedItemId);
    return item != nullptr && !item->examineText.empty();
}

void InventoryMgr::examineSelectedItem()
{
    if (!canExamineSelectedItem())
        return;

    refreshItemFromDatabase(selectedItemId);

    const int itemIndex = findItemIndex(selectedItemId);
    if (itemIndex >= 0)
        ensureItemExamineImageLoaded(items[(size_t)itemIndex]);

    viewState = InventoryViewState::ExaminingItem;
}

const InventoryItem* InventoryMgr::findItem(const std::string& id) const
{
    const int itemIndex = findItemIndex(id);
    if (itemIndex < 0)
        return nullptr;
    return &items[(size_t)itemIndex];
}

InventoryItem* InventoryMgr::findMutableItem(const std::string& id)
{
    const int itemIndex = findItemIndex(id);
    if (itemIndex < 0)
        return nullptr;
    return &items[(size_t)itemIndex];
}

bool InventoryMgr::canExtractFromExaminedItem(const ItemDatabase& database) const
{
    if (viewState != InventoryViewState::ExaminingItem || selectedItemId.empty())
        return false;

    const InventoryItem* parent = findItem(selectedItemId);
    if (parent == nullptr)
        return false;

    const ItemDef* parentDef = database.getDef(parent->id);
    if (parentDef == nullptr || !parentDef->container.isContainer)
        return false;

    for (const ItemInstance& child : parent->instance.contents)
    {
        if (child.defId.empty())
            continue;
        if (hasItem(child.defId))
            continue;
        if (database.isExtractableContainerContent(*parentDef, child.defId))
            return true;
    }

    return false;
}

bool InventoryMgr::extractFromExaminedItem(
    const ItemDatabase& database,
    InventoryItem& outExtracted)
{
    if (!canExtractFromExaminedItem(database))
        return false;

    InventoryItem* parent = findMutableItem(selectedItemId);
    if (parent == nullptr)
        return false;

    const ItemDef* parentDef = database.getDef(parent->id);
    if (parentDef == nullptr)
        return false;

    for (std::vector<ItemInstance>::iterator it = parent->instance.contents.begin();
         it != parent->instance.contents.end();
         ++it)
    {
        if (it->defId.empty() || hasItem(it->defId))
            continue;
        if (!database.isExtractableContainerContent(*parentDef, it->defId))
            continue;

        ItemInstance extractedInstance = *it;
        parent->instance.contents.erase(it);

        const Texture2D parentIcon = parent->icon;
        const Texture2D parentExamineImage = parent->examineImage;
        InventoryItem refreshedParent = database.buildInventoryItem(parent->instance);
        refreshedParent.icon = parentIcon;
        refreshedParent.examineImage = parentExamineImage;
        *parent = refreshedParent;

        outExtracted = database.buildInventoryItem(extractedInstance);
        return true;
    }

    return false;
}

void InventoryMgr::refreshItemFromDatabase(const std::string& id)
{
    if (itemDatabase == nullptr)
        return;

    InventoryItem* item = findMutableItem(id);
    if (item == nullptr || item->isUndefined)
        return;

    const Texture2D icon = item->icon;
    const Texture2D examineImage = item->examineImage;
    InventoryItem refreshed = itemDatabase->buildInventoryItem(item->instance);
    refreshed.icon = icon;
    refreshed.examineImage = examineImage;
    if (id == "wallet")
    {
        refreshed.examineText = ItemDatabase::appendWalletCashDescription(
            refreshed.examineText,
            walletCash);
    }
    *item = refreshed;
}

bool InventoryMgr::applyExamineRevealFlag(const std::string& itemId, const std::string& flag)
{
    if (flag.empty())
        return false;

    InventoryItem* item = findMutableItem(itemId);
    if (item == nullptr || hasItemFlag(item->instance.activeFlags, flag))
        return false;

    item->instance.activeFlags.push_back(flag);

    if (item->examineImage.id != 0 && IsTextureValid(item->examineImage))
    {
        UnloadTexture(item->examineImage);
        item->examineImage = Texture2D{};
    }

    refreshItemFromDatabase(itemId);
    ensureItemExamineImageLoaded(*item);
    return true;
}

int InventoryMgr::findItemIndex(const std::string& id) const
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].id == id)
            return (int)i;
    }
    return -1;
}

Rectangle InventoryMgr::getCloseButtonBounds() const
{
    const float pad = 14.0f;
    return {
        panelBounds.x + panelBounds.width - kCloseButtonSize - pad,
        panelBounds.y + pad,
        kCloseButtonSize,
        kCloseButtonSize
    };
}

float InventoryMgr::getInventoryVisibleHeight() const
{
    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    return panelBounds.height - pad * 2.0f - headerHeight;
}

void InventoryMgr::handleCloseButtonInput()
{
    const Rectangle closeBounds = getCloseButtonBounds();
    if (CheckCollisionPointRec(GetMousePosition(), closeBounds) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        close();
}

int InventoryMgr::findItemSlotAtMouse() const
{
    const Vector2 mousePos = GetMousePosition();
    for (size_t i = 0; i < items.size() && i < itemSlotBounds.size(); ++i)
    {
        Rectangle slot = itemSlotBounds[i];
        slot.y -= inventoryScroll.getScrollY();
        if (CheckCollisionPointRec(mousePos, slot))
            return (int)i;
    }
    return -1;
}

void InventoryMgr::handleItemGridInput()
{
    if (viewState != InventoryViewState::ItemList)
        return;

    const Vector2 mousePos = GetMousePosition();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        const int slotIndex = findItemSlotAtMouse();
        if (slotIndex >= 0)
        {
            pendingPressItemId = items[(size_t)slotIndex].id;
            pressStartPos = mousePos;
        }
        return;
    }

    if (!pendingPressItemId.empty() && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        const float dx = mousePos.x - pressStartPos.x;
        const float dy = mousePos.y - pressStartPos.y;
        const float distanceSq = dx * dx + dy * dy;
        if (!isDraggingItem && distanceSq >= kDragStartThreshold * kDragStartThreshold)
        {
            isDraggingItem = true;
            dragItemId = pendingPressItemId;
            selectedItemId = dragItemId;
        }
    }
}

float InventoryMgr::totalCarryWeightLb() const
{
    if (itemDatabase == nullptr)
        return 0.0f;
    float total = 0.0f;
    for (const InventoryItem& item : items)
    {
        const ItemDef* def = itemDatabase->getDef(item.id);
        if (def == nullptr)
            continue;
        total += computeItemWeightLb(*def, item.instance);
    }
    return roundItemWeightLb(total);
}

void InventoryMgr::openCraftChooser(
    const std::string& firstItemId,
    const std::string& secondItemId,
    std::vector<ItemCraftCandidate> candidates)
{
    craftChooserOpen = true;
    craftChooserFirstId = firstItemId;
    craftChooserSecondId = secondItemId;
    craftChooserCandidates = std::move(candidates);
}

void InventoryMgr::closeCraftChooser()
{
    craftChooserOpen = false;
    craftChooserFirstId.clear();
    craftChooserSecondId.clear();
    craftChooserCandidates.clear();
}

void InventoryMgr::handleItemCombineInput()
{
    if (viewState != InventoryViewState::ItemList)
        return;
    if (craftChooserOpen)
        return;

    if (!IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        return;

    if (isDraggingItem && !dragItemId.empty())
    {
        const int targetSlotIndex = findItemSlotAtMouse();
        if (targetSlotIndex >= 0)
        {
            const std::string& targetItemId = items[(size_t)targetSlotIndex].id;
            if (!targetItemId.empty() && targetItemId != dragItemId)
            {
                const InventoryItem* sourceItem = findItem(dragItemId);
                const InventoryItem* targetItem = findItem(targetItemId);
                if (sourceItem != nullptr
                    && targetItem != nullptr
                    && itemCombinationDatabase != nullptr
                    && itemDatabase != nullptr)
                {
                    const std::vector<ItemCraftCandidate> products =
                        itemCombinationDatabase->findAffordableCraftProducts(
                            *itemDatabase,
                            dragItemId,
                            targetItemId,
                            sourceItem->instance,
                            targetItem->instance);

                    if (products.size() > 1)
                    {
                        openCraftChooser(dragItemId, targetItemId, products);
                    }
                    else
                    {
                        ItemCombineApplication application;
                        if (itemCombinationDatabase->tryCombine(
                                *itemDatabase,
                                dragItemId,
                                targetItemId,
                                sourceItem->instance,
                                targetItem->instance,
                                application)
                            && applyItemCombination(application))
                        {
                            selectedItemId.clear();
                            if (!application.grantProductId.empty()
                                && hasItem(application.grantProductId))
                                selectedItemId = application.grantProductId;
                        }
                    }
                }
            }
        }
    }
    else if (!pendingPressItemId.empty())
    {
        selectedItemId = pendingPressItemId;
    }

    dragItemId.clear();
    pendingPressItemId.clear();
    isDraggingItem = false;
}

bool InventoryMgr::applyComponentSpends(const std::vector<ItemCombineComponentSpend>& spends)
{
    if (itemDatabase == nullptr)
        return false;

    for (const ItemCombineComponentSpend& spend : spends)
    {
        InventoryItem* item = findMutableItem(spend.itemId);
        if (item == nullptr)
            return false;
        if (spend.spendQty > 0)
        {
            if (item->instance.quantity < spend.spendQty)
                return false;
            item->instance.quantity -= spend.spendQty;
        }

        // Cleanup owned by the component instance rules.
        if (item->instance.quantity < 1)
        {
            if (spend.consume)
            {
                if (!removeItem(spend.itemId))
                    return false;
            }
            else
            {
                item->instance.quantity = 1;
                InventoryItem refreshed = itemDatabase->buildInventoryItem(item->instance);
                // Keep loaded textures when possible.
                refreshed.icon = item->icon;
                refreshed.examineImage = item->examineImage;
                *item = refreshed;
            }
        }
        else
        {
            InventoryItem refreshed = itemDatabase->buildInventoryItem(item->instance);
            refreshed.icon = item->icon;
            refreshed.examineImage = item->examineImage;
            *item = refreshed;
        }
    }
    return true;
}

bool InventoryMgr::applyItemCombination(const ItemCombineApplication& application)
{
    if (!application.success || itemDatabase == nullptr)
        return false;

    if (application.grantProductId.empty()
        || !itemDatabase->hasDef(application.grantProductId))
        return false;

    for (const ItemCombineComponentSpend& spend : application.componentSpends)
    {
        if (findItem(spend.itemId) == nullptr)
            return false;
    }

    if (!applyComponentSpends(application.componentSpends))
        return false;

    ItemInstance instance = itemDatabase->createInstance(application.grantProductId);
    instance.quantity = std::max(1, application.grantQuantity);
    InventoryItem granted = itemDatabase->buildInventoryItem(instance);
    InventoryItem* existing = findMutableItem(application.grantProductId);
    if (existing != nullptr)
    {
        existing->instance.quantity += instance.quantity;
        InventoryItem refreshed = itemDatabase->buildInventoryItem(existing->instance);
        refreshed.icon = existing->icon;
        refreshed.examineImage = existing->examineImage;
        *existing = refreshed;
    }
    else
    {
        addItem(granted);
    }

    pendingCombinationNarrative = application.narrative;
    pendingCombinationNarrativeTts = application.narrativeTts;
    pendingCombinationTtsOwnerEnabled = application.ttsOwnerEnabled;
    pendingItemCombinationApplied = true;
    return true;
}

void InventoryMgr::handleCraftChooserInput()
{
    if (!craftChooserOpen)
        return;

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;

    const float dialogW = 360.0f;
    const float rowH = 52.0f;
    const float cancelH = 36.0f;
    const float pad = 16.0f;
    const float dialogH =
        pad + 28.0f
        + rowH * static_cast<float>(craftChooserCandidates.size())
        + cancelH + pad + 8.0f;
    const Rectangle dialog = {
        panelBounds.x + (panelBounds.width - dialogW) * 0.5f,
        panelBounds.y + (panelBounds.height - dialogH) * 0.5f,
        dialogW,
        dialogH};
    const Vector2 mouse = GetMousePosition();

    const Rectangle cancelBtn = {
        dialog.x + pad,
        dialog.y + dialogH - pad - cancelH,
        dialogW - pad * 2.0f,
        cancelH};
    if (CheckCollisionPointRec(mouse, cancelBtn))
    {
        closeCraftChooser();
        return;
    }

    float y = dialog.y + pad + 28.0f;
    for (const ItemCraftCandidate& candidate : craftChooserCandidates)
    {
        const Rectangle row = {dialog.x + pad, y, dialogW - pad * 2.0f, rowH - 4.0f};
        if (CheckCollisionPointRec(mouse, row)
            && itemDatabase != nullptr
            && itemCombinationDatabase != nullptr)
        {
            const InventoryItem* first = findItem(craftChooserFirstId);
            const InventoryItem* second = findItem(craftChooserSecondId);
            if (first != nullptr && second != nullptr)
            {
                ItemCombineApplication application;
                if (itemCombinationDatabase->buildProductCraftApplication(
                        *itemDatabase,
                        candidate.productId,
                        craftChooserFirstId,
                        craftChooserSecondId,
                        first->instance,
                        second->instance,
                        application)
                    && applyItemCombination(application))
                {
                    selectedItemId = candidate.productId;
                }
            }
            closeCraftChooser();
            return;
        }
        y += rowH;
    }

    // Click outside dialog cancels.
    if (!CheckCollisionPointRec(mouse, dialog))
        closeCraftChooser();
}

void InventoryMgr::drawCraftChooser() const
{
    if (!craftChooserOpen)
        return;

    DrawRectangle(
        static_cast<int>(panelBounds.x),
        static_cast<int>(panelBounds.y),
        static_cast<int>(panelBounds.width),
        static_cast<int>(panelBounds.height),
        Color{0, 0, 0, 160});

    const float dialogW = 360.0f;
    const float rowH = 52.0f;
    const float cancelH = 36.0f;
    const float pad = 16.0f;
    const float dialogH =
        pad + 28.0f
        + rowH * static_cast<float>(craftChooserCandidates.size())
        + cancelH + pad + 8.0f;
    const Rectangle dialog = {
        panelBounds.x + (panelBounds.width - dialogW) * 0.5f,
        panelBounds.y + (panelBounds.height - dialogH) * 0.5f,
        dialogW,
        dialogH};

    DrawRectangleRec(dialog, Color{32, 30, 40, 255});
    DrawRectangleLinesEx(dialog, 2.0f, Color{168, 138, 72, 255});
    DrawTextEx(
        panelFont.texture.id != 0 ? panelFont : GetFontDefault(),
        "Choose product",
        {dialog.x + pad, dialog.y + pad},
        16.0f,
        1.0f,
        Color{220, 212, 196, 255});

    float y = dialog.y + pad + 28.0f;
    const Vector2 mouse = GetMousePosition();
    for (const ItemCraftCandidate& candidate : craftChooserCandidates)
    {
        const Rectangle row = {dialog.x + pad, y, dialogW - pad * 2.0f, rowH - 4.0f};
        const bool hover = CheckCollisionPointRec(mouse, row);
        DrawRectangleRec(row, hover ? Color{60, 54, 72, 255} : Color{44, 42, 52, 255});
        DrawRectangleLinesEx(row, 1.0f, Color{168, 138, 72, 255});

        // Icon placeholder / path note (textures load via inventory items when held).
        DrawTextEx(
            panelFont.texture.id != 0 ? panelFont : GetFontDefault(),
            candidate.productName.c_str(),
            {row.x + 12.0f, row.y + 14.0f},
            15.0f,
            1.0f,
            Color{220, 212, 196, 255});
        y += rowH;
    }

    const Rectangle cancelBtn = {
        dialog.x + pad,
        dialog.y + dialogH - pad - cancelH,
        dialogW - pad * 2.0f,
        cancelH};
    const bool cancelHover = CheckCollisionPointRec(mouse, cancelBtn);
    DrawRectangleRec(cancelBtn, cancelHover ? Color{70, 60, 50, 255} : Color{44, 42, 52, 255});
    DrawRectangleLinesEx(cancelBtn, 1.0f, Color{168, 138, 72, 255});
    DrawTextEx(
        panelFont.texture.id != 0 ? panelFont : GetFontDefault(),
        "Cancel",
        {cancelBtn.x + (cancelBtn.width - 48.0f) * 0.5f, cancelBtn.y + 10.0f},
        15.0f,
        1.0f,
        Color{220, 212, 196, 255});
}

void InventoryMgr::handleInventoryScrollInput()
{
    if (viewState == InventoryViewState::Closed)
        return;

    const float visibleHeight = getInventoryVisibleHeight();
    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float contentY = panelBounds.y + pad + headerHeight;

    inventoryScroll.setContentHeight(inventoryContentHeight);
    inventoryScroll.setVisibleArea({
        panelBounds.x + pad,
        contentY,
        panelBounds.width - pad * 2.0f - kScrollbarWidth - 4.0f,
        visibleHeight
    });
    inventoryScroll.setScrollTrack({
        panelBounds.x + panelBounds.width - kScrollbarWidth - pad,
        contentY,
        kScrollbarWidth,
        visibleHeight
    });
    inventoryScroll.setWheelStep(24.0f);
    inventoryScroll.setWheelMultiplier(2.0f);
    inventoryScroll.setInputBlocked(isDraggingItem);
    inventoryScroll.handleInput();
}

void InventoryMgr::layoutItemSlots() const
{
    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float contentX = panelBounds.x + pad;
    const float contentY = panelBounds.y + pad + headerHeight;
    const float contentW = panelBounds.width - pad * 2.0f - kScrollbarWidth - 4.0f;

    const int columns = std::max(1, (int)((contentW + kItemGap) / (kItemSlotSize + kItemGap)));
    const int rows = (int)items.size() / columns + ((int)items.size() % columns > 0 ? 1 : 0);
    inventoryContentHeight = rows > 0
        ? rows * kItemSlotSize + (rows - 1) * kItemGap
        : 0.0f;

    itemSlotBounds.clear();
    itemSlotBounds.reserve(items.size());

    for (size_t i = 0; i < items.size(); ++i)
    {
        const int col = (int)i % columns;
        const int row = (int)i / columns;
        itemSlotBounds.push_back({
            contentX + col * (kItemSlotSize + kItemGap),
            contentY + row * (kItemSlotSize + kItemGap),
            kItemSlotSize,
            kItemSlotSize
        });
    }
}

void InventoryMgr::update()
{
    if (viewState == InventoryViewState::Closed)
        return;

    layoutItemSlots();
    if (craftChooserOpen)
    {
        handleCraftChooserInput();
        return;
    }

    handleCloseButtonInput();
    handleItemGridInput();
    handleItemCombineInput();
    handleInventoryScrollInput();
}

void InventoryMgr::drawCloseButton() const
{
    const Rectangle closeBounds = getCloseButtonBounds();
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), closeBounds);
    const Color lineColor = hovered ? kCloseHover : kSectionLabel;

    DrawLineEx(
        { closeBounds.x + 8.0f, closeBounds.y + 8.0f },
        { closeBounds.x + closeBounds.width - 8.0f, closeBounds.y + closeBounds.height - 8.0f },
        2.0f,
        lineColor);
    DrawLineEx(
        { closeBounds.x + closeBounds.width - 8.0f, closeBounds.y + 8.0f },
        { closeBounds.x + 8.0f, closeBounds.y + closeBounds.height - 8.0f },
        2.0f,
        lineColor);
}

void InventoryMgr::syncWalletDisplay(float cash)
{
    walletCash = cash;

    InventoryItem* wallet = findMutableItem("wallet");
    if (wallet == nullptr || itemDatabase == nullptr)
        return;

    const ItemDef* def = itemDatabase->getDef("wallet");
    if (def == nullptr)
        return;

    const std::string baseDescription = itemDatabase->resolveExamineDescription(
        *def,
        wallet->instance,
        {});
    wallet->examineText = ItemDatabase::appendWalletCashDescription(baseDescription, walletCash);
}

void InventoryMgr::drawWalletCashBadge(const Rectangle& slot) const
{
    if (panelFont.texture.id == 0)
        return;

    const std::string label = ItemDatabase::formatWalletCashIconLabel(walletCash);
    const float fontSize = 13.0f;
    const Vector2 measure = MeasureTextEx(panelFont, label.c_str(), fontSize, 1.0f);
    const float padX = 5.0f;
    const float padY = 2.0f;
    const float margin = 5.0f;

    const Rectangle badge = {
        slot.x + slot.width - measure.x - padX * 2.0f - margin,
        slot.y + slot.height - measure.y - padY * 2.0f - margin,
        measure.x + padX * 2.0f,
        measure.y + padY * 2.0f
    };

    DrawRectangleRounded(badge, 0.35f, 4, { 20, 18, 26, 225 });
    DrawRoundedBorder(badge, 0.35f, 4, 1.0f, { 140, 118, 72, 255 });
    DrawTextEx(
        panelFont,
        label.c_str(),
        { badge.x + padX, badge.y + padY },
        fontSize,
        1.0f,
        { 210, 178, 108, 255 });
}

void InventoryMgr::drawItemGrid() const
{
    if (itemSlotBounds.size() != items.size())
        layoutItemSlots();

    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float contentX = panelBounds.x + pad;
    const float contentY = panelBounds.y + pad + headerHeight;
    const float contentW = panelBounds.width - pad * 2.0f - kScrollbarWidth - 4.0f;
    const float visibleHeight = getInventoryVisibleHeight();

    const float borderPad = 3.0f;
    BeginScissorMode(
        (int)(contentX - borderPad),
        (int)(contentY - borderPad),
        (int)(contentW + borderPad * 2.0f),
        (int)(visibleHeight + borderPad * 2.0f));

    for (size_t i = 0; i < items.size() && i < itemSlotBounds.size(); ++i)
    {
        Rectangle slot = itemSlotBounds[i];
        slot.y -= inventoryScroll.getScrollY();

        if (slot.y + kItemSlotSize < contentY || slot.y > contentY + visibleHeight)
            continue;

        const bool selected = items[i].id == selectedItemId;
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), slot);
        const bool combineTarget = isDraggingItem
            && !dragItemId.empty()
            && items[i].id != dragItemId
            && hovered;
        const Color slotFill = (uiBackdrop != nullptr) ? uiBackdrop->slotFillColor() : kSlotFill;
        const Color slotHover = (uiBackdrop != nullptr) ? uiBackdrop->slotHoverColor() : kSlotHover;
        const Color slotSelected = (uiBackdrop != nullptr) ? uiBackdrop->slotSelectedColor() : kSlotSelected;
        const Color fill = combineTarget
            ? kSlotCombineTarget
            : (selected ? slotSelected : (hovered ? slotHover : slotFill));

        DrawRectangleRounded(slot, 0.18f, 8, fill);

        if (!(isDraggingItem && items[i].id == dragItemId))
        {
            const float iconPad = 10.0f;
            const Rectangle iconArea = {
                slot.x + iconPad,
                slot.y + iconPad,
                slot.width - iconPad * 2.0f,
                slot.height - iconPad * 2.0f
            };

            if (items[i].isUndefined)
                drawUndefinedItemIcon(iconArea);
            else if (isItemIconReady(items[i]))
            {
                DrawTexturePro(
                    items[i].icon,
                    { 0.0f, 0.0f, (float)items[i].icon.width, (float)items[i].icon.height },
                    iconArea,
                    { 0.0f, 0.0f },
                    0.0f,
                    WHITE);

                if (items[i].id == "wallet")
                    drawWalletCashBadge(slot);
            }
        }
    }

    EndScissorMode();

    for (size_t i = 0; i < items.size() && i < itemSlotBounds.size(); ++i)
    {
        Rectangle slot = itemSlotBounds[i];
        slot.y -= inventoryScroll.getScrollY();

        if (slot.y + kItemSlotSize < contentY || slot.y > contentY + visibleHeight)
            continue;

        const bool selected = items[i].id == selectedItemId;
        DrawRoundedBorder(slot, 0.18f, 8, 2.0f, selected ? kPanelBorder : kPanelAccent);
    }
}

void InventoryMgr::drawUndefinedItemIcon(const Rectangle& iconArea) const
{
    const Color xColor = { 220, 48, 48, 255 };
    const float thickness = std::max(4.0f, iconArea.width * 0.12f);
    const float inset = iconArea.width * 0.12f;

    DrawLineEx(
        { iconArea.x + inset, iconArea.y + inset },
        { iconArea.x + iconArea.width - inset, iconArea.y + iconArea.height - inset },
        thickness,
        xColor);
    DrawLineEx(
        { iconArea.x + iconArea.width - inset, iconArea.y + inset },
        { iconArea.x + inset, iconArea.y + iconArea.height - inset },
        thickness,
        xColor);
}

void InventoryMgr::drawDragGhost() const
{
    if (!isDraggingItem || dragItemId.empty())
        return;

    const InventoryItem* item = findItem(dragItemId);
    if (item == nullptr || (!item->isUndefined && !isItemIconReady(*item)))
        return;

    const Vector2 mousePos = GetMousePosition();
    const float ghostSize = kItemSlotSize * 0.72f;
    const Rectangle ghostArea = {
        mousePos.x - ghostSize * 0.5f,
        mousePos.y - ghostSize * 0.5f,
        ghostSize,
        ghostSize
    };

    DrawRectangleRounded(ghostArea, 0.18f, 8, { 40, 38, 50, 210 });
    DrawRoundedBorder(ghostArea, 0.18f, 8, 2.0f, kPanelBorder);

    const float iconPad = 10.0f;
    const Rectangle iconArea = {
        ghostArea.x + iconPad,
        ghostArea.y + iconPad,
        ghostArea.width - iconPad * 2.0f,
        ghostArea.height - iconPad * 2.0f
    };

    if (item->isUndefined)
    {
        drawUndefinedItemIcon(iconArea);
        return;
    }

    DrawTexturePro(
        item->icon,
        { 0.0f, 0.0f, (float)item->icon.width, (float)item->icon.height },
        iconArea,
        { 0.0f, 0.0f },
        0.0f,
        { 255, 255, 255, 220 });

    if (item->id == "wallet")
        drawWalletCashBadge(ghostArea);
}

void InventoryMgr::drawInventoryScrollbar() const
{
    inventoryScroll.drawScrollbar();
}

void InventoryMgr::draw() const
{
    if (viewState == InventoryViewState::Closed)
        return;

    const Color panelBorder = (uiBackdrop != nullptr) ? uiBackdrop->panelBorderColor() : kPanelBorder;
    const Color sectionLabel = (uiBackdrop != nullptr) ? uiBackdrop->sectionLabelColor() : kSectionLabel;

    if (uiBackdrop != nullptr)
        uiBackdrop->drawPanel(panelBounds, 0.04f, 10);
    else
        DrawRectangleRounded(panelBounds, 0.04f, 10, kPanelFill);

    DrawRoundedBorder(panelBounds, 0.04f, 10, 3.0f, panelBorder);

    Rectangle accentBar = {
        panelBounds.x + 8.0f,
        panelBounds.y + 8.0f,
        panelBounds.width - 16.0f,
        4.0f
    };
    if (uiBackdrop != nullptr)
        uiBackdrop->drawAccentBar(accentBar);
    else
        DrawRectangleRounded(accentBar, 1.0f, 4, kPanelAccent);

    const float pad = 14.0f;
    DrawTextEx(panelFont, "INVENTORY", { panelBounds.x + pad, panelBounds.y + pad }, 17.0f, 1, sectionLabel);

    drawCloseButton();
    drawItemGrid();
    drawDragGhost();
    drawInventoryScrollbar();
    drawCraftChooser();
}

const InventoryItem* InventoryMgr::getSelectedItem() const
{
    return findItem(selectedItemId);
}

const InventoryItem* InventoryMgr::getItemById(const std::string& id) const
{
    return findItem(id);
}

bool InventoryMgr::hasItem(const std::string& id) const
{
    return findItem(id) != nullptr;
}

void InventoryMgr::addItem(const InventoryItem& item)
{
    if (item.id.empty() || hasItem(item.id))
        return;

    items.push_back(item);
    ensureItemIconLoaded(items.back());
}

bool InventoryMgr::giveOrStackItem(const InventoryItem& item, std::string& message)
{
    message.clear();
    if (item.id.empty())
    {
        message = "Empty item id";
        return false;
    }

    InventoryItem* existing = findMutableItem(item.id);
    if (existing == nullptr)
    {
        addItem(item);
        if (!hasItem(item.id))
        {
            message = "Failed to add " + item.id;
            return false;
        }
        message = "Gave " + item.id;
        return true;
    }

    const ItemDef* def = itemDatabase != nullptr ? itemDatabase->getDef(item.id) : nullptr;
    const bool stackable = def != nullptr && def->quantity.stackable;
    if (!stackable)
    {
        message = "Already in inventory (not stackable): " + item.id;
        return false;
    }

    const int addQty = std::max(1, item.instance.quantity);
    existing->instance.quantity += addQty;
    if (itemDatabase != nullptr)
    {
        InventoryItem refreshed = itemDatabase->buildInventoryItem(existing->instance);
        refreshed.icon = existing->icon;
        refreshed.examineImage = existing->examineImage;
        *existing = refreshed;
    }
    message = "Stacked +" + std::to_string(addQty) + " → " + item.id
        + " (qty " + std::to_string(existing->instance.quantity) + ")";
    return true;
}

bool InventoryMgr::removeItem(const std::string& id)
{
    const int itemIndex = findItemIndex(id);
    if (itemIndex < 0)
        return false;

    InventoryItem& item = items[(size_t)itemIndex];
    if (item.icon.id != 0)
        UnloadTexture(item.icon);
    if (item.examineImage.id != 0)
        UnloadTexture(item.examineImage);

    items.erase(items.begin() + itemIndex);

    if (selectedItemId == id)
        selectedItemId.clear();

    return true;
}

std::string InventoryMgr::consumePendingDropItemId()
{
    const std::string itemId = pendingDropItemId;
    pendingDropItemId.clear();
    return itemId;
}

bool InventoryMgr::consumeItemCombinationApplied()
{
    const bool applied = pendingItemCombinationApplied;
    pendingItemCombinationApplied = false;
    if (!applied)
    {
        pendingCombinationNarrative.clear();
        pendingCombinationNarrativeTts = ItemTtsDef{};
        pendingCombinationTtsOwnerEnabled = false;
    }
    return applied;
}

std::string InventoryMgr::consumePendingCombinationNarrative()
{
    const std::string narrative = pendingCombinationNarrative;
    pendingCombinationNarrative.clear();
    return narrative;
}

ItemTtsDef InventoryMgr::consumePendingCombinationNarrativeTts()
{
    const ItemTtsDef tts = pendingCombinationNarrativeTts;
    pendingCombinationNarrativeTts = ItemTtsDef{};
    return tts;
}

bool InventoryMgr::consumePendingCombinationTtsOwnerEnabled()
{
    const bool enabled = pendingCombinationTtsOwnerEnabled;
    pendingCombinationTtsOwnerEnabled = false;
    return enabled;
}

std::vector<InventoryItem> InventoryMgr::exportItemSnapshots() const
{
    std::vector<InventoryItem> snapshots;
    snapshots.reserve(items.size());

    for (const InventoryItem& item : items)
    {
        InventoryItem snapshot;
        snapshot.id = item.id;
        snapshot.name = item.name;
        snapshot.iconPath = item.iconPath;
        snapshot.examineImagePath = item.examineImagePath;
        snapshot.examineText = item.examineText;
        snapshot.weightLb = item.weightLb;
        snapshot.isUndefined = item.isUndefined;
        snapshot.undefinedPurchaseSceneId = item.undefinedPurchaseSceneId;
        snapshot.instance = item.instance;
        snapshots.push_back(snapshot);
    }

    return snapshots;
}

void InventoryMgr::restoreFromSnapshots(const std::vector<InventoryItem>& savedItems)
{
    close();

    while (items.size() > 1)
    {
        InventoryItem& item = items.back();
        if (item.icon.id != 0)
            UnloadTexture(item.icon);
        if (item.examineImage.id != 0)
            UnloadTexture(item.examineImage);
        items.pop_back();
    }

    if (items.empty())
        createDefaultItems();

    if (itemDatabase != nullptr)
    {
        InventoryItem* wallet = findMutableItem("wallet");
        if (wallet != nullptr && !hasItem("wallet_slip"))
        {
            bool hasSlipInWallet = false;
            for (const ItemInstance& child : wallet->instance.contents)
            {
                if (child.defId == "wallet_slip")
                {
                    hasSlipInWallet = true;
                    break;
                }
            }

            if (!hasSlipInWallet)
            {
                ItemInstance slip = itemDatabase->createInstance("wallet_slip");
                wallet->instance.contents.push_back(slip);
                refreshItemFromDatabase("wallet");
            }
        }
    }

    for (const InventoryItem& savedItem : savedItems)
    {
        if (savedItem.id.empty() || savedItem.id == "wallet" || savedItem.id == "hand_lantern")
            continue;

        InventoryItem restored = savedItem;
        if (itemDatabase != nullptr
            && itemDatabase->hasDef(savedItem.id)
            && !savedItem.isUndefined)
        {
            ItemDefOverrides overrides;
            overrides.name = savedItem.name;
            overrides.description = savedItem.examineText;
            overrides.iconPath = savedItem.iconPath;
            overrides.examineImagePath = savedItem.examineImagePath;

            ItemInstance instance = savedItem.instance.defId.empty()
                ? itemDatabase->createInstance(savedItem.id)
                : savedItem.instance;
            if (instance.defId.empty())
                instance.defId = savedItem.id;
            if (instance.instanceId.empty())
                instance.instanceId = savedItem.id;

            restored = itemDatabase->buildInventoryItem(instance, overrides);
        }

        addItem(restored);
    }

    ensureIconAssetsLoaded();
}

}