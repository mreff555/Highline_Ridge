#include <SpeakTargetMgr.h>
#include <SidePanel.h>

#include <RaylibCompat.h>
#include <algorithm>

namespace highline_ridge
{

namespace
{
    const Color kPanelFill = {28, 26, 34, 255};
    const Color kPanelBorder = {168, 138, 72, 255};
    const Color kPanelAccent = {96, 78, 48, 255};
    const Color kSectionLabel = {132, 122, 104, 255};
    const Color kOptionFill = {40, 38, 50, 255};
    const Color kOptionHover = {62, 52, 34, 255};
    const Color kOptionText = {228, 220, 198, 255};
    const Color kCloseHover = {210, 178, 108, 255};
}

const float SpeakTargetMgr::kCloseButtonSize = 28.0f;
const float SpeakTargetMgr::kRowGap = 10.0f;
const float SpeakTargetMgr::kScrollbarWidth = ScrollPanel::kDefaultScrollbarWidth;

SpeakTargetMgr::SpeakTargetMgr() = default;

void SpeakTargetMgr::setPanelBounds(Rectangle bounds)
{
    panelBounds = bounds;
}

void SpeakTargetMgr::setFont(Font font)
{
    panelFont = font;
}

void SpeakTargetMgr::setUiBackdrop(const UiBackdrop* backdrop)
{
    uiBackdrop = backdrop;
}

void SpeakTargetMgr::open()
{
    openState = true;
    optionScroll.resetScroll();
}

void SpeakTargetMgr::close()
{
    openState = false;
}

void SpeakTargetMgr::setAvailableTargets(const std::vector<SpeakTargetDef>& targets)
{
    options = targets;
    optionBounds.clear();
    optionScroll.resetScroll();
}

SpeakTargetDef SpeakTargetMgr::consumePendingTarget()
{
    const SpeakTargetDef target = pendingTarget;
    pendingTarget = SpeakTargetDef();
    return target;
}

float SpeakTargetMgr::getRowHeight() const
{
    return 42.0f;
}

Rectangle SpeakTargetMgr::getCloseButtonBounds() const
{
    return SidePanelChrome::getCloseButtonBounds(panelBounds, kCloseButtonSize);
}

Rectangle SpeakTargetMgr::getContentBounds() const
{
    const float pad = 14.0f;
    const float headerHeight = 28.0f;
    const float contentHeight = std::max(
        0.0f,
        panelBounds.height - pad * 2.0f - headerHeight);

    return {
        panelBounds.x + pad,
        panelBounds.y + pad + headerHeight,
        std::max(0.0f, panelBounds.width - pad * 2.0f - kScrollbarWidth - 4.0f),
        contentHeight
    };
}

float SpeakTargetMgr::getContentHeight() const
{
    if (options.empty())
        return 0.0f;

    const float rowHeight = getRowHeight();
    return (float)options.size() * rowHeight + ((float)options.size() - 1.0f) * kRowGap;
}

void SpeakTargetMgr::layoutOptionBounds()
{
    const Rectangle content = getContentBounds();
    const float rowHeight = getRowHeight();

    optionBounds.clear();
    optionBounds.reserve(options.size());

    for (size_t i = 0; i < options.size(); ++i)
    {
        optionBounds.push_back({
            content.x,
            content.y + (float)i * (rowHeight + kRowGap),
            content.width,
            rowHeight
        });
    }
}

void SpeakTargetMgr::handleScrollInput()
{
    const Rectangle content = getContentBounds();
    optionScroll.setContentHeight(getContentHeight());
    optionScroll.setVisibleArea(content);
    optionScroll.setScrollTrack({
        panelBounds.x + panelBounds.width - kScrollbarWidth - 14.0f,
        content.y,
        kScrollbarWidth,
        content.height
    });
    optionScroll.setWheelStep(getRowHeight() + kRowGap);
    optionScroll.setWheelMultiplier(1.0f);
    optionScroll.handleInput();
}

void SpeakTargetMgr::handleCloseButtonInput()
{
    const Rectangle closeBounds = getCloseButtonBounds();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), closeBounds))
    {
        close();
    }
}

void SpeakTargetMgr::handleOptionInput()
{
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;

    const Rectangle content = getContentBounds();
    const Vector2 mousePos = GetMousePosition();
    const float scrollY = optionScroll.getScrollY();

    for (size_t i = 0; i < optionBounds.size() && i < options.size(); ++i)
    {
        Rectangle row = optionBounds[i];
        row.y -= scrollY;

        if (row.y + row.height < content.y || row.y > content.y + content.height)
            continue;

        if (CheckCollisionPointRec(mousePos, row))
        {
            pendingTarget = options[i];
            return;
        }
    }
}

void SpeakTargetMgr::update()
{
    pendingTarget = SpeakTargetDef();

    if (!openState)
        return;

    layoutOptionBounds();
    handleScrollInput();
    handleCloseButtonInput();
    handleOptionInput();
}

void SpeakTargetMgr::drawCloseButton() const
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

void SpeakTargetMgr::drawOptionList() const
{
    const Rectangle content = getContentBounds();
    const float textPad = 12.0f;
    const float fontSize = 18.0f;
    const float scrollY = optionScroll.getScrollY();

    BeginScissorMode(
        (int)content.x,
        (int)content.y,
        (int)content.width,
        (int)content.height);

    for (size_t i = 0; i < options.size() && i < optionBounds.size(); ++i)
    {
        Rectangle row = optionBounds[i];
        row.y -= scrollY;

        if (row.y + row.height < content.y || row.y > content.y + content.height)
            continue;

        const bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
        DrawRectangleRounded(row, 0.16f, 8, hovered ? kOptionHover : kOptionFill);
        DrawTextEx(
            panelFont,
            options[i].label.c_str(),
            { row.x + textPad, row.y + (row.height - fontSize) * 0.5f },
            fontSize,
            1.0f,
            kOptionText);
    }

    EndScissorMode();
}

void SpeakTargetMgr::draw() const
{
    if (!openState)
        return;

    const Color panelBorder = (uiBackdrop != nullptr) ? uiBackdrop->panelBorderColor() : kPanelBorder;
    const Color sectionLabel = (uiBackdrop != nullptr) ? uiBackdrop->sectionLabelColor() : kSectionLabel;

    if (uiBackdrop != nullptr)
        uiBackdrop->drawPanel(panelBounds, 0.04f, 10);
    else
        DrawRectangleRounded(panelBounds, 0.04f, 10, kPanelFill);

    DrawRectangleLinesEx(panelBounds, 3.0f, panelBorder);

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
    DrawTextEx(
        panelFont,
        "SPEAK",
        { panelBounds.x + pad, panelBounds.y + pad },
        17.0f,
        1.0f,
        sectionLabel);

    drawCloseButton();
    drawOptionList();
    optionScroll.drawScrollbar();
}

}