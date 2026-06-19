#include <InventoryMgr.h>
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
    const Color kCloseHover = {210, 178, 108, 255};

    const char* kWalletExamineText =
        "The wallet is worked from thick full-grain leather, hand-stitched along the edges "
        "with waxed thread the color of strong tea. The dye is uneven in the way of good hides, "
        "not factory perfect. A maker's stamp inside the flap is too worn to read. It sits heavy "
        "in your palm, the sort of piece a man buys once and carries for years.\n\n"
        "You count what it holds. Twenty dollars in worn notes, nothing more. No credit cards. "
        "No identification. The pockets are otherwise empty except for a small slip of paper, "
        "tucked deep into a corner seam as though someone meant to forget it and could not quite.";

    Texture2D loadItemTexture(const char* filename)
    {
        const std::string paths[] = {
            std::string("../resources/") + filename,
            std::string("resources/") + filename
        };

        for (const std::string& path : paths)
        {
            if (!FileExists(path.c_str()))
                continue;

            const Texture2D texture = LoadTexture(path.c_str());
            if (texture.id != 0)
                return texture;
        }

        TraceLog(LOG_ERROR, "Failed to load inventory image: %s", filename);
        return Texture2D{};
    }
}

const float InventoryMgr::kScrollbarWidth = 16.0f;
const float InventoryMgr::kCloseButtonSize = 28.0f;
const float InventoryMgr::kItemSlotSize = 76.0f;
const float InventoryMgr::kItemGap = 12.0f;

InventoryMgr::InventoryMgr()
{
    createDefaultItems();
}

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

void InventoryMgr::createDefaultItems()
{
    InventoryItem wallet;
    wallet.id = "wallet";
    wallet.name = "Wallet";
    wallet.icon = loadItemTexture("wallet_icon.jpg");
    wallet.examineImage = loadItemTexture("wallet_examine.jpg");
    wallet.examineText = kWalletExamineText;
    items.push_back(wallet);
}

void InventoryMgr::open()
{
    viewState = InventoryViewState::ItemList;
    selectedItemId.clear();
    inventoryScrollY = 0.0f;
}

void InventoryMgr::close()
{
    viewState = InventoryViewState::Closed;
    selectedItemId.clear();
    inventoryScrollY = 0.0f;
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

    viewState = InventoryViewState::ExaminingItem;
}

const InventoryItem* InventoryMgr::findItem(const std::string& id) const
{
    for (const InventoryItem& item : items)
    {
        if (item.id == id)
            return &item;
    }
    return nullptr;
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
        panelBounds.x + panelBounds.width - pad - kCloseButtonSize,
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
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), closeBounds))
    {
        close();
    }
}

void InventoryMgr::handleItemGridInput()
{
    if (viewState != InventoryViewState::ItemList)
        return;

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;

    const Vector2 mousePos = GetMousePosition();
    for (size_t i = 0; i < itemSlotBounds.size(); ++i)
    {
        if (CheckCollisionPointRec(mousePos, itemSlotBounds[i]))
        {
            if (i < items.size())
                selectedItemId = items[i].id;
            return;
        }
    }
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

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
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

    if (CheckCollisionPointRec(mousePos, scrollArea))
        inventoryScrollY -= GetMouseWheelMove() * lineHeight * 2.0f;

    inventoryScrollY = std::max(0.0f, std::min(inventoryScrollY, maxScroll));
}

void InventoryMgr::layoutItemSlots()
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
    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float contentX = panelBounds.x + pad;
    const float contentY = panelBounds.y + pad + headerHeight;
    const float contentW = panelBounds.width - pad * 2.0f - kScrollbarWidth - 4.0f;
    const float visibleHeight = getInventoryVisibleHeight();

    BeginScissorMode(
        (int)contentX,
        (int)contentY,
        (int)contentW,
        (int)visibleHeight);

    for (size_t i = 0; i < items.size() && i < itemSlotBounds.size(); ++i)
    {
        Rectangle slot = itemSlotBounds[i];
        slot.y -= inventoryScrollY;

        if (slot.y + kItemSlotSize < contentY || slot.y > contentY + visibleHeight)
            continue;

        const bool selected = items[i].id == selectedItemId;
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), slot);
        const Color fill = selected ? kSlotSelected : (hovered ? kSlotHover : kSlotFill);

        DrawRectangleRounded(slot, 0.18f, 8, fill);
        DrawRectangleRoundedLines(slot, 0.18f, 8, 2.0f, selected ? kPanelBorder : kPanelAccent);

        if (items[i].icon.id != 0)
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
    DrawRectangleRoundedLines(panelBounds, 0.04f, 10, 3.0f, kPanelBorder);

    Rectangle accentBar = {
        panelBounds.x + 8.0f,
        panelBounds.y + 8.0f,
        panelBounds.width - 16.0f,
        4.0f
    };
    DrawRectangleRounded(accentBar, 1.0f, 4, kPanelAccent);

    const float pad = 14.0f;
    DrawTextEx(panelFont, "INVENTORY", { panelBounds.x + pad, panelBounds.y + pad }, 14.0f, 1, kSectionLabel);

    drawCloseButton();
    drawItemGrid();
    drawInventoryScrollbar();
}

const InventoryItem* InventoryMgr::getSelectedItem() const
{
    return findItem(selectedItemId);
}

}