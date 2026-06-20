#include <TakeMgr.h>
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
    const Color kSlotFill = {40, 38, 50, 255};
    const Color kSlotHover = {54, 50, 64, 255};
    const Color kCloseHover = {210, 178, 108, 255};
    const Color kActionFill = {62, 52, 34, 255};
    const Color kActionHover = {88, 72, 44, 255};
    const Color kActionText = {228, 220, 198, 255};
}

const float TakeMgr::kCloseButtonSize = 28.0f;
const float TakeMgr::kItemSlotSize = 76.0f;
const float TakeMgr::kItemGap = 12.0f;
const float TakeMgr::kTakeAllButtonHeight = 40.0f;

TakeMgr::TakeMgr() = default;

TakeMgr::~TakeMgr()
{
    clearSlots();
}

void TakeMgr::clearSlots()
{
    for (TakeSlot& slot : slots)
    {
        if (slot.ownsIcon && slot.icon.id != 0)
            UnloadTexture(slot.icon);
    }
    slots.clear();
    itemSlotBounds.clear();
}

void TakeMgr::setPanelBounds(Rectangle bounds)
{
    panelBounds = bounds;
}

void TakeMgr::setFont(Font font)
{
    panelFont = font;
}

void TakeMgr::setAssetRoots(const std::string& primaryRoot, const std::string& fallbackRoot)
{
    primaryAssetRoot = primaryRoot;
    fallbackAssetRoot = fallbackRoot;
}

void TakeMgr::open()
{
    openState = true;
}

void TakeMgr::close()
{
    openState = false;
}

void TakeMgr::setAvailableItems(const std::vector<TakeableItemDef>& items)
{
    clearSlots();
    slots.reserve(items.size());

    for (const TakeableItemDef& item : items)
    {
        TakeSlot slot;
        slot.def = item;
        loadSlotIcon(slot);
        slots.push_back(slot);
    }
}

bool TakeMgr::loadIconTexture(const std::string& relativePath, Texture2D& outTexture) const
{
    if (relativePath.empty())
        return false;

    const std::string path = relativePath.find("resources/") == 0
        ? relativePath
        : "resources/" + relativePath;

    if (loadResourceTexture(primaryAssetRoot, path, outTexture))
        return true;

    if (fallbackAssetRoot != primaryAssetRoot &&
        loadResourceTexture(fallbackAssetRoot, path, outTexture))
    {
        return true;
    }

    return false;
}

Texture2D TakeMgr::createPlaceholderIcon(const std::string& label) const
{
    const int size = 96;
    RenderTexture2D target = LoadRenderTexture(size, size);
    BeginTextureMode(target);
    ClearBackground((Color){48, 44, 56, 255});

    DrawRectangleRounded({ 18.0f, 10.0f, 60.0f, 72.0f }, 0.2f, 8, (Color){176, 168, 152, 255});
    DrawRectangleRounded({ 24.0f, 18.0f, 48.0f, 46.0f }, 0.15f, 6, (Color){228, 220, 198, 255});
    DrawRectangleRounded({ 30.0f, 6.0f, 36.0f, 12.0f }, 0.4f, 4, (Color){118, 96, 58, 255});

    const char* text = label.empty() ? "?" : label.c_str();
    const float fontSize = 16.0f;
    const Vector2 textSize = MeasureTextEx(panelFont, text, fontSize, 1.0f);
    DrawTextEx(
        panelFont,
        text,
        { (size - textSize.x) * 0.5f, size - textSize.y - 8.0f },
        fontSize,
        1.0f,
        (Color){62, 52, 34, 255});

    EndTextureMode();

    Image iconImage = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&iconImage);
    UnloadRenderTexture(target);

    Texture2D icon = LoadTextureFromImage(iconImage);
    UnloadImage(iconImage);
    if (icon.id != 0)
        SetTextureFilter(icon, TEXTURE_FILTER_BILINEAR);
    return icon;
}

bool TakeMgr::loadSlotIcon(TakeSlot& slot)
{
    Texture2D texture{};
    if (loadIconTexture(slot.def.iconPath, texture))
    {
        slot.icon = texture;
        slot.ownsIcon = true;
        SetTextureFilter(slot.icon, TEXTURE_FILTER_BILINEAR);
        return true;
    }

    slot.icon = createPlaceholderIcon(slot.def.name);
    slot.ownsIcon = slot.icon.id != 0;
    return slot.ownsIcon;
}

bool TakeMgr::tryTakeItem(const std::string& id, TakeableItemDef& outTaken)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].def.id != id)
            continue;

        outTaken = slots[i].def;
        if (slots[i].ownsIcon && slots[i].icon.id != 0)
            UnloadTexture(slots[i].icon);
        slots.erase(slots.begin() + (ptrdiff_t)i);
        return true;
    }

    return false;
}

std::vector<TakeableItemDef> TakeMgr::takeAll()
{
    std::vector<TakeableItemDef> taken;
    taken.reserve(slots.size());

    for (TakeSlot& slot : slots)
    {
        taken.push_back(slot.def);
        if (slot.ownsIcon && slot.icon.id != 0)
            UnloadTexture(slot.icon);
    }

    slots.clear();
    return taken;
}

Rectangle TakeMgr::getCloseButtonBounds() const
{
    const float pad = 14.0f;
    return {
        panelBounds.x + panelBounds.width - pad - kCloseButtonSize,
        panelBounds.y + pad,
        kCloseButtonSize,
        kCloseButtonSize
    };
}

Rectangle TakeMgr::getTakeAllButtonBounds() const
{
    const float pad = 14.0f;
    return {
        panelBounds.x + pad,
        panelBounds.y + panelBounds.height - pad - kTakeAllButtonHeight,
        panelBounds.width - pad * 2.0f,
        kTakeAllButtonHeight
    };
}

float TakeMgr::getContentAreaHeight() const
{
    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float footerGap = 10.0f;
    return panelBounds.height - pad * 2.0f - headerHeight - kTakeAllButtonHeight - footerGap;
}

void TakeMgr::layoutItemSlots()
{
    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float contentX = panelBounds.x + pad;
    const float contentY = panelBounds.y + pad + headerHeight;
    const float contentW = panelBounds.width - pad * 2.0f;

    const int columns = std::max(1, (int)((contentW + kItemGap) / (kItemSlotSize + kItemGap)));

    itemSlotBounds.clear();
    itemSlotBounds.reserve(slots.size());

    for (size_t i = 0; i < slots.size(); ++i)
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

void TakeMgr::handleCloseButtonInput()
{
    const Rectangle closeBounds = getCloseButtonBounds();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), closeBounds))
    {
        close();
    }
}

std::string TakeMgr::consumePendingTakeId()
{
    const std::string id = pendingTakeId;
    pendingTakeId.clear();
    return id;
}

bool TakeMgr::consumePendingTakeAll()
{
    const bool requested = pendingTakeAll;
    pendingTakeAll = false;
    return requested;
}

void TakeMgr::handleItemGridInput()
{
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;

    const Vector2 mousePos = GetMousePosition();
    for (size_t i = 0; i < itemSlotBounds.size() && i < slots.size(); ++i)
    {
        if (CheckCollisionPointRec(mousePos, itemSlotBounds[i]))
        {
            pendingTakeId = slots[i].def.id;
            return;
        }
    }
}

void TakeMgr::handleTakeAllButtonInput()
{
    if (slots.empty())
        return;

    const Rectangle buttonBounds = getTakeAllButtonBounds();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), buttonBounds))
    {
        pendingTakeAll = true;
    }
}

void TakeMgr::update()
{
    pendingTakeId.clear();
    pendingTakeAll = false;

    if (!openState)
        return;

    layoutItemSlots();
    handleCloseButtonInput();
    handleItemGridInput();
    handleTakeAllButtonInput();
}

void TakeMgr::drawCloseButton() const
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

void TakeMgr::drawItemGrid() const
{
    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float contentX = panelBounds.x + pad;
    const float contentY = panelBounds.y + pad + headerHeight;
    const float contentW = panelBounds.width - pad * 2.0f;
    const float visibleHeight = getContentAreaHeight();

    const float borderPad = 3.0f;
    BeginScissorMode(
        (int)(contentX - borderPad),
        (int)(contentY - borderPad),
        (int)(contentW + borderPad * 2.0f),
        (int)(visibleHeight + borderPad * 2.0f));

    for (size_t i = 0; i < slots.size() && i < itemSlotBounds.size(); ++i)
    {
        const Rectangle slot = itemSlotBounds[i];
        if (slot.y + kItemSlotSize < contentY || slot.y > contentY + visibleHeight)
            continue;

        const bool hovered = CheckCollisionPointRec(GetMousePosition(), slot);
        DrawRectangleRounded(slot, 0.18f, 8, hovered ? kSlotHover : kSlotFill);

        if (slots[i].icon.id != 0)
        {
            const float iconPad = 10.0f;
            const Rectangle iconArea = {
                slot.x + iconPad,
                slot.y + iconPad,
                slot.width - iconPad * 2.0f,
                slot.height - iconPad * 2.0f
            };
            DrawTexturePro(
                slots[i].icon,
                { 0.0f, 0.0f, (float)slots[i].icon.width, (float)slots[i].icon.height },
                iconArea,
                { 0.0f, 0.0f },
                0.0f,
                WHITE);
        }
    }

    EndScissorMode();

    for (size_t i = 0; i < slots.size() && i < itemSlotBounds.size(); ++i)
    {
        const Rectangle slot = itemSlotBounds[i];
        if (slot.y + kItemSlotSize < contentY || slot.y > contentY + visibleHeight)
            continue;

        DrawRoundedBorder(slot, 0.18f, 8, 2.0f, kPanelAccent);
    }

    if (slots.empty())
    {
        const char* emptyText = "Nothing left to take.";
        const float fontSize = 18.0f;
        const Vector2 textSize = MeasureTextEx(panelFont, emptyText, fontSize, 1.0f);
        DrawTextEx(
            panelFont,
            emptyText,
            {
                contentX + (contentW - textSize.x) * 0.5f,
                contentY + (visibleHeight - textSize.y) * 0.5f
            },
            fontSize,
            1.0f,
            kSectionLabel);
    }
}

void TakeMgr::drawTakeAllButton() const
{
    const Rectangle buttonBounds = getTakeAllButtonBounds();
    const bool enabled = !slots.empty();
    const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), buttonBounds);

    DrawRectangleRounded(
        buttonBounds,
        0.18f,
        8,
        enabled ? (hovered ? kActionHover : kActionFill) : (Color){40, 38, 50, 255});
    DrawRoundedBorder(
        buttonBounds,
        0.18f,
        8,
        2.0f,
        enabled ? kPanelBorder : kPanelAccent);

    const char* label = "Take All";
    const float fontSize = 18.0f;
    const Vector2 textSize = MeasureTextEx(panelFont, label, fontSize, 1.0f);
    DrawTextEx(
        panelFont,
        label,
        {
            buttonBounds.x + (buttonBounds.width - textSize.x) * 0.5f,
            buttonBounds.y + (buttonBounds.height - textSize.y) * 0.5f
        },
        fontSize,
        1.0f,
        enabled ? kActionText : kSectionLabel);
}

void TakeMgr::draw() const
{
    if (!openState)
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
    DrawTextEx(panelFont, "TAKE", { panelBounds.x + pad, panelBounds.y + pad }, 17.0f, 1, kSectionLabel);

    drawCloseButton();
    drawItemGrid();
    drawTakeAllButton();
}

}