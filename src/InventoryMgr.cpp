#include <InventoryMgr.h>
#include <RaylibCompat.h>
#include <SceneLoader.h>
#include <algorithm>

namespace testgame
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

bool InventoryMgr::isItemIconReady(const InventoryItem& item) const
{
    return item.icon.id != 0 && IsTextureValid(item.icon);
}

bool InventoryMgr::hasLoadedAssets() const
{
    for (const InventoryItem& item : items)
    {
        if (!isItemIconReady(item))
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

    if (itemDatabase != nullptr && itemDatabase->hasDef("wallet"))
    {
        items.push_back(itemDatabase->buildInventoryItem(itemDatabase->createInstance("wallet")));
        return;
    }

    InventoryItem wallet;
    wallet.id = "wallet";
    wallet.instance.defId = "wallet";
    wallet.instance.instanceId = "wallet";
    wallet.name = "Wallet";
    wallet.examineText =
        "The wallet is worked from thick full-grain leather, hand-stitched along the edges "
        "with waxed thread the color of strong tea.";
    items.push_back(wallet);
}

void InventoryMgr::open()
{
    if (!ensureIconAssetsLoaded())
        TraceLog(LOG_WARNING, "Some inventory icons failed to load");

    viewState = InventoryViewState::ItemList;
    selectedItemId.clear();
    inventoryScrollY = 0.0f;
    dragItemId.clear();
    pendingPressItemId.clear();
    isDraggingItem = false;
}

void InventoryMgr::close()
{
    viewState = InventoryViewState::Closed;
    selectedItemId.clear();
    inventoryScrollY = 0.0f;
    dragItemId.clear();
    pendingPressItemId.clear();
    isDraggingItem = false;
}

void InventoryMgr::returnToItemList()
{
    viewState = InventoryViewState::ItemList;
    inventoryScrollY = 0.0f;
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
    if (item == nullptr)
        return;

    const Texture2D icon = item->icon;
    const Texture2D examineImage = item->examineImage;
    InventoryItem refreshed = itemDatabase->buildInventoryItem(item->instance);
    refreshed.icon = icon;
    refreshed.examineImage = examineImage;
    *item = refreshed;
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
        slot.y -= inventoryScrollY;
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

void InventoryMgr::handleItemCombineInput()
{
    if (viewState != InventoryViewState::ItemList)
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
                    ItemCombineApplication application;
                    if (itemCombinationDatabase->tryCombine(
                            dragItemId,
                            targetItemId,
                            sourceItem->instance,
                            targetItem->instance,
                            application)
                        && applyItemCombination(application))
                    {
                        selectedItemId.clear();
                        for (const std::string& grantId : application.grantItemIds)
                        {
                            if (hasItem(grantId))
                                selectedItemId = grantId;
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

bool InventoryMgr::applyItemCombination(const ItemCombineApplication& application)
{
    if (!application.success || itemDatabase == nullptr)
        return false;

    for (const std::string& itemId : application.removeItemIds)
    {
        if (!hasItem(itemId))
            return false;
    }

    for (const std::pair<std::string, std::string>& flagEntry : application.addFlags)
    {
        const InventoryItem* item = findItem(flagEntry.first);
        if (item == nullptr)
            return false;
        (void)item;
    }

    for (const std::string& grantId : application.grantItemIds)
    {
        if (grantId.empty() || hasItem(grantId) || !itemDatabase->hasDef(grantId))
            return false;
    }

    for (const std::string& itemId : application.removeItemIds)
        removeItem(itemId);

    for (const std::pair<std::string, std::string>& flagEntry : application.addFlags)
    {
        InventoryItem* item = findMutableItem(flagEntry.first);
        if (item == nullptr)
            return false;

        if (!item->instance.hasFlag(flagEntry.second))
            item->instance.activeFlags.push_back(flagEntry.second);

        const std::string previousIconPath = item->iconPath;
        const std::string previousExaminePath = item->examineImagePath;
        const Texture2D previousIcon = item->icon;
        const Texture2D previousExamineImage = item->examineImage;

        InventoryItem refreshed = itemDatabase->buildInventoryItem(item->instance);
        refreshed.icon = previousIcon;
        refreshed.examineImage = previousExamineImage;
        *item = refreshed;

        if (item->iconPath != previousIconPath)
        {
            if (item->icon.id != 0)
                UnloadTexture(item->icon);
            item->icon = Texture2D{};
            loadItemIcon(*item);
        }

        if (item->examineImagePath != previousExaminePath)
        {
            if (item->examineImage.id != 0)
                UnloadTexture(item->examineImage);
            item->examineImage = Texture2D{};
            loadItemExamineImage(*item);
        }
    }

    for (const std::string& grantId : application.grantItemIds)
    {
        ItemInstance instance = itemDatabase->createInstance(grantId);
        InventoryItem granted = itemDatabase->buildInventoryItem(instance);
        addItem(granted);
    }

    return true;
}

void InventoryMgr::handleInventoryScrollInput()
{
    if (viewState == InventoryViewState::Closed)
        return;

    const float visibleHeight = getInventoryVisibleHeight();
    const float maxScroll = std::max(0.0f, inventoryContentHeight - visibleHeight);
    const float lineHeight = 24.0f;
    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float contentY = panelBounds.y + pad + headerHeight;

    const Rectangle scrollTrack = {
        panelBounds.x + panelBounds.width - kScrollbarWidth - pad,
        contentY,
        kScrollbarWidth,
        visibleHeight
    };

    const float thumbHeight = (inventoryContentHeight <= 0.0f)
        ? visibleHeight
        : std::max(24.0f, visibleHeight * (visibleHeight / inventoryContentHeight));
    const float thumbTravel = std::max(0.0f, visibleHeight - thumbHeight);
    const float thumbY = scrollTrack.y + (maxScroll > 0.0f
        ? (inventoryScrollY / maxScroll) * thumbTravel
        : 0.0f);

    const Rectangle scrollThumb = {
        scrollTrack.x + 2.0f,
        thumbY,
        scrollTrack.width - 4.0f,
        thumbHeight
    };

    const Vector2 mousePos = GetMousePosition();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !isDraggingItem)
    {
        if (CheckCollisionPointRec(mousePos, scrollThumb))
        {
            inventoryScrollbarDragging = true;
            inventoryScrollbarDragOffsetY = mousePos.y - scrollThumb.y;
        }
        else if (CheckCollisionPointRec(mousePos, scrollTrack) && thumbTravel > 0.0f)
        {
            inventoryScrollY = ((mousePos.y - scrollTrack.y - thumbHeight * 0.5f) / thumbTravel) * maxScroll;
        }
    }

    if (inventoryScrollbarDragging)
    {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && thumbTravel > 0.0f)
            inventoryScrollY = ((mousePos.y - scrollTrack.y - inventoryScrollbarDragOffsetY) / thumbTravel) * maxScroll;
        else
            inventoryScrollbarDragging = false;
    }

    const Rectangle scrollArea = {
        panelBounds.x + pad,
        contentY,
        panelBounds.width - pad * 2.0f - kScrollbarWidth - 4.0f,
        visibleHeight
    };

    if (!isDraggingItem && CheckCollisionPointRec(mousePos, scrollArea))
        inventoryScrollY -= GetMouseWheelMove() * lineHeight * 2.0f;

    inventoryScrollY = std::max(0.0f, std::min(inventoryScrollY, maxScroll));
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
        slot.y -= inventoryScrollY;

        if (slot.y + kItemSlotSize < contentY || slot.y > contentY + visibleHeight)
            continue;

        const bool selected = items[i].id == selectedItemId;
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), slot);
        const bool combineTarget = isDraggingItem
            && !dragItemId.empty()
            && items[i].id != dragItemId
            && hovered;
        const Color fill = combineTarget
            ? kSlotCombineTarget
            : (selected ? kSlotSelected : (hovered ? kSlotHover : kSlotFill));

        DrawRectangleRounded(slot, 0.18f, 8, fill);

        if (isItemIconReady(items[i]) && !(isDraggingItem && items[i].id == dragItemId))
        {
            const float iconPad = 10.0f;
            const Rectangle iconArea = {
                slot.x + iconPad,
                slot.y + iconPad,
                slot.width - iconPad * 2.0f,
                slot.height - iconPad * 2.0f
            };
            DrawTexturePro(
                items[i].icon,
                { 0.0f, 0.0f, (float)items[i].icon.width, (float)items[i].icon.height },
                iconArea,
                { 0.0f, 0.0f },
                0.0f,
                WHITE);
        }
    }

    EndScissorMode();

    for (size_t i = 0; i < items.size() && i < itemSlotBounds.size(); ++i)
    {
        Rectangle slot = itemSlotBounds[i];
        slot.y -= inventoryScrollY;

        if (slot.y + kItemSlotSize < contentY || slot.y > contentY + visibleHeight)
            continue;

        const bool selected = items[i].id == selectedItemId;
        DrawRoundedBorder(slot, 0.18f, 8, 2.0f, selected ? kPanelBorder : kPanelAccent);
    }
}

void InventoryMgr::drawDragGhost() const
{
    if (!isDraggingItem || dragItemId.empty())
        return;

    const InventoryItem* item = findItem(dragItemId);
    if (item == nullptr || !isItemIconReady(*item))
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
    DrawTexturePro(
        item->icon,
        { 0.0f, 0.0f, (float)item->icon.width, (float)item->icon.height },
        iconArea,
        { 0.0f, 0.0f },
        0.0f,
        { 255, 255, 255, 220 });
}

void InventoryMgr::drawInventoryScrollbar() const
{
    const float visibleHeight = getInventoryVisibleHeight();
    const float maxScroll = std::max(0.0f, inventoryContentHeight - visibleHeight);
    if (maxScroll <= 0.0f)
        return;

    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float contentY = panelBounds.y + pad + headerHeight;

    const Rectangle scrollTrack = {
        panelBounds.x + panelBounds.width - kScrollbarWidth - pad,
        contentY,
        kScrollbarWidth,
        visibleHeight
    };

    DrawRectangleRec(scrollTrack, kScrollTrack);

    const float thumbHeight = std::max(24.0f, visibleHeight * (visibleHeight / inventoryContentHeight));
    const float thumbTravel = std::max(0.0f, visibleHeight - thumbHeight);
    const float thumbY = scrollTrack.y + (inventoryScrollY / maxScroll) * thumbTravel;

    const Rectangle scrollThumb = {
        scrollTrack.x + 2.0f,
        thumbY,
        scrollTrack.width - 4.0f,
        thumbHeight
    };

    const bool thumbHovered = CheckCollisionPointRec(GetMousePosition(), scrollThumb);
    DrawRectangleRounded(scrollThumb, 0.4f, 6, thumbHovered ? kScrollThumbHover : kScrollThumb);
}

void InventoryMgr::draw() const
{
    if (viewState == InventoryViewState::Closed)
        return;

    DrawRectangleRounded(panelBounds, 0.04f, 10, kPanelFill);
    DrawRoundedBorder(panelBounds, 0.04f, 10, 3.0f, kPanelBorder);

    Rectangle accentBar = {
        panelBounds.x + 8.0f,
        panelBounds.y + 8.0f,
        panelBounds.width - 16.0f,
        4.0f
    };
    DrawRectangleRounded(accentBar, 1.0f, 4, kPanelAccent);

    const float pad = 14.0f;
    DrawTextEx(panelFont, "INVENTORY", { panelBounds.x + pad, panelBounds.y + pad }, 17.0f, 1, kSectionLabel);

    drawCloseButton();
    drawItemGrid();
    drawDragGhost();
    drawInventoryScrollbar();
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
        if (itemDatabase != nullptr && itemDatabase->hasDef(savedItem.id))
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