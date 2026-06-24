#include "NarrativeNotebook.h"

#include <ConversationManager.h>
#include <ImageCompression.h>
#include <ProgressionService.h>
#include <RaylibCompat.h>
#include <SceneLoader.h>
#include <algorithm>
#include <cstdio>
#include <sstream>

namespace testgame
{

const float NarrativeNotebook::kScrollbarWidth = 16.0f;
const int NarrativeNotebook::kMaxNarrativeLines = 500;

namespace
{
    const float kDialogHeightShareWhenSidePanelOpen = 2.0f / 3.0f;
    const Color kPaperShadow = {118, 98, 68, 255};
    const Color kPaperEdge = {108, 88, 58, 255};
    const Color kRuleLine = {132, 148, 168, 85};
    const Color kMarginLine = {168, 78, 68, 150};
    const Color kBindingRing = {98, 82, 62, 255};
    const Color kBindingHole = {58, 48, 38, 255};
    const Color kChoiceText = {92, 52, 22, 255};
    const Color kChoiceHover = {148, 88, 28, 255};
    const char kNarrativeSketchPrefix[] = "@sketch:";
    const Color kNotebookHeader = {78, 54, 34, 255};
    const Color kNotebookNavEnabled = {78, 54, 34, 255};
    const Color kNotebookNavDisabled = {148, 132, 112, 255};
    const Color kQuestComplete = {52, 92, 58, 255};
    const Color kQuestFailed = {108, 72, 72, 255};

    Rectangle getNotebookHeaderBand(const Rectangle& bounds)
    {
        return {
            bounds.x + 58.0f,
            bounds.y,
            bounds.width - 58.0f - 16.0f,
            46.0f
        };
    }

    void drawCenteredUnderlinedHeader(
        Font font,
        const char* title,
        const Rectangle& band,
        float fontSize,
        Color color)
    {
        const Vector2 textSize = MeasureTextEx(font, title, fontSize, 1.0f);
        const float textX = band.x + (band.width - textSize.x) / 2.0f;
        const float textY = band.y + (band.height - textSize.y) / 2.0f - 2.0f;
        DrawTextEx(font, title, { textX, textY }, fontSize, 1.0f, color);
        DrawLineEx(
            { textX, textY + textSize.y + 2.0f },
            { textX + textSize.x, textY + textSize.y + 2.0f },
            1.5f,
            color);
    }

    void drawNotebookArrowButton(
        Font font,
        const char* label,
        Rectangle bounds,
        bool enabled,
        bool hovered)
    {
        const float fontSize = bounds.height - 4.0f;
        const Vector2 textSize = MeasureTextEx(font, label, fontSize, 1.0f);
        const Vector2 textPos = {
            bounds.x + (bounds.width - textSize.x) / 2.0f,
            bounds.y + (bounds.height - textSize.y) / 2.0f
        };
        Color color = enabled ? kNotebookNavEnabled : kNotebookNavDisabled;
        if (enabled && hovered)
            color = kChoiceHover;
        DrawTextEx(font, label, textPos, fontSize, 1.0f, color);
    }
}

NarrativeNotebook::NarrativeNotebook()
{
}

NarrativeNotebook::~NarrativeNotebook()
{
    unloadSketchTextures();
    if (notebookPaperTextureReady)
    {
        UnloadTexture(notebookPaperTexture);
        notebookPaperTextureReady = false;
    }
}

void NarrativeNotebook::unloadSketchTextures()
{
    for (std::map<std::string, Texture2D>::iterator it = narrativeSketchTextures.begin();
         it != narrativeSketchTextures.end(); ++it)
    {
        if (it->second.id != 0)
            UnloadTexture(it->second);
    }
    narrativeSketchTextures.clear();
}

void NarrativeNotebook::setFonts(Font descriptionFont_, Font boldFont_)
{
    descriptionFont = descriptionFont_;
    boldFont = boldFont_;
}

void NarrativeNotebook::setLayout(
    Rectangle textBox_,
    float fullDialogHeight_,
    bool sidePanelOpen_)
{
    textBox = textBox_;
    fullDialogHeight = fullDialogHeight_;
    sidePanelOpen = sidePanelOpen_;
}

void NarrativeNotebook::setAssetRoot(const std::string& root)
{
    primaryAssetRoot = root;
}

void NarrativeNotebook::setContext(
    const ConversationManager* conversation,
    const ProgressionService* progression,
    const std::set<std::string>* committedLines,
    float wallet,
    bool navEnabled,
    const std::function<void(const std::string&)>& onChoice)
{
    conversationMgr = conversation;
    progressionService = progression;
    committedPlayerDialogLines = committedLines;
    walletCash = wallet;
    notebookNavEnabled = navEnabled;
    onChoiceSelected = onChoice;
}

Rectangle NarrativeNotebook::getDialogBounds() const
{
    const float height = sidePanelOpen
        ? fullDialogHeight * kDialogHeightShareWhenSidePanelOpen
        : fullDialogHeight;
    return { textBox.x, textBox.y, textBox.width, height };
}

std::vector<ConversationChoiceDef> NarrativeNotebook::filterChoices(
    const std::vector<ConversationChoiceDef>& choices) const
{
    std::vector<ConversationChoiceDef> available;
    available.reserve(choices.size());
    for (const ConversationChoiceDef& choice : choices)
    {
        if (choice.isAvailable(walletCash))
            available.push_back(choice);
    }
    return available;
}

    float NarrativeNotebook::getNarrativeLineHeight() const
    {
        const float scaleFactor = fontSize / (float)descriptionFont.baseSize;
        return (descriptionFont.baseSize + descriptionFont.baseSize / 2.0f) * scaleFactor;
    }
    void NarrativeNotebook::ensureNotebookPaperTexture() const
    {
        if (notebookPaperTextureReady)
            return;

        const int width = std::max(1, (int)textBox.width);
        const int height = std::max(1, (int)fullDialogHeight);
        Image paper = GenImageColor(width, height, {214, 188, 136, 255});

        for (int y = 0; y < height; ++y)
        {
            const float rowFade = (float)y / (float)height;
            for (int x = 0; x < width; ++x)
            {
                const float colFade = (float)x / (float)width;
                const unsigned int hash = (unsigned int)(x * 92837111u + y * 689287499u);

                int red = 208 - (int)(rowFade * 28.0f) - (int)(colFade * 8.0f);
                int green = 178 - (int)(rowFade * 24.0f) - (int)(colFade * 6.0f);
                int blue = 118 - (int)(rowFade * 18.0f) - (int)(colFade * 4.0f);

                if ((hash % 19u) == 0u)
                {
                    red -= 16;
                    green -= 18;
                    blue -= 12;
                }
                else if ((hash % 29u) == 0u)
                {
                    red += 10;
                    green += 8;
                    blue += 4;
                }

                if ((hash % 41u) == 0u)
                {
                    red -= 8;
                    green -= 10;
                    blue -= 6;
                }

                const int edgeX = std::min(x, width - 1 - x);
                const int edgeY = std::min(y, height - 1 - y);
                const int edge = std::min(edgeX, edgeY);
                if (edge < 36)
                {
                    const int edgeWeight = 36 - edge;
                    red -= edgeWeight / 3;
                    green -= edgeWeight / 2;
                    blue -= edgeWeight / 4;
                }

                red = std::max(0, std::min(255, red));
                green = std::max(0, std::min(255, green));
                blue = std::max(0, std::min(255, blue));
                ImageDrawPixel(&paper, x, y, { (unsigned char)red, (unsigned char)green, (unsigned char)blue, 255 });
            }
        }

        for (int y = 0; y < height; y += 3)
        {
            const unsigned int rowHash = (unsigned int)(y * 2654435761u);
            const int shift = (int)(rowHash % 7u) - 3;
            for (int x = 0; x < width; x += 2)
            {
                const unsigned int hash = rowHash ^ (unsigned int)(x * 1597334677u);
                if ((hash % 11u) > 1u)
                    continue;

                const int tone = 18 + (int)(hash % 24u);
                ImageDrawPixel(
                    &paper,
                    x,
                    y,
                    { (unsigned char)(118 + shift), (unsigned char)(98 + shift), (unsigned char)(62 + shift), (unsigned char)tone });
            }
        }

        notebookPaperTexture = LoadTextureFromImage(paper);
        UnloadImage(paper);
        notebookPaperTextureReady = notebookPaperTexture.id != 0;
    }
    void NarrativeNotebook::drawNotebookBackdrop(const Rectangle& bounds) const
    {
        ensureNotebookPaperTexture();

        const Rectangle shadow = {
            bounds.x + 6.0f,
            bounds.y + 8.0f,
            bounds.width,
            bounds.height
        };
        DrawRectangleRec(shadow, kPaperShadow);

        if (notebookPaperTextureReady)
        {
            DrawTexturePro(
                notebookPaperTexture,
                { 0.0f, 0.0f, (float)notebookPaperTexture.width, bounds.height },
                bounds,
                { 0.0f, 0.0f },
                0.0f,
                WHITE);
        }
        else
        {
            DrawRectangleRec(bounds, {208, 182, 132, 255});
        }

        const float lineStep = getNarrativeLineHeight();
        const float firstLineY = bounds.y + notebookHeaderReserve + lineStep * 0.15f;
        for (float ruleY = firstLineY; ruleY < bounds.y + bounds.height - 18.0f; ruleY += lineStep)
        {
            DrawLineEx(
                { bounds.x + 18.0f, ruleY },
                { bounds.x + bounds.width - 24.0f, ruleY },
                1.0f,
                kRuleLine);
        }

        const float marginX = bounds.x + 58.0f;
        DrawLineEx(
            { marginX, bounds.y + 16.0f },
            { marginX, bounds.y + bounds.height - 16.0f },
            2.0f,
            kMarginLine);

        const Rectangle bindingStrip = {
            bounds.x,
            bounds.y,
            18.0f,
            bounds.height
        };
        DrawRectangleRec(bindingStrip, {176, 152, 108, 255});

        const float holeRadius = 5.5f;
        const float holeSpacing = 42.0f;
        for (float holeY = bounds.y + 34.0f; holeY < bounds.y + bounds.height - 34.0f; holeY += holeSpacing)
        {
            const Vector2 holeCenter = { bounds.x + 9.0f, holeY };
            DrawCircleV(holeCenter, holeRadius + 1.5f, kBindingRing);
            DrawCircleV(holeCenter, holeRadius, kBindingHole);
            DrawCircleV({ holeCenter.x - 1.0f, holeCenter.y - 1.0f }, holeRadius - 2.0f, {78, 66, 52, 255});
        }

        const float foldSize = 28.0f;
        DrawTriangle(
            { bounds.x + bounds.width, bounds.y },
            { bounds.x + bounds.width - foldSize, bounds.y },
            { bounds.x + bounds.width, bounds.y + foldSize },
            {184, 158, 112, 255});
        DrawLineEx(
            { bounds.x + bounds.width - foldSize, bounds.y },
            { bounds.x + bounds.width, bounds.y + foldSize },
            1.0f,
            kPaperEdge);

        DrawRectangleLinesEx(bounds, 2.0f, kPaperEdge);

        const Rectangle scrollbarGutter = {
            bounds.x + bounds.width - kScrollbarWidth,
            bounds.y,
            kScrollbarWidth,
            bounds.height
        };
        DrawRectangleRec(scrollbarGutter, {196, 174, 132, 255});
        DrawLineEx(
            { scrollbarGutter.x, scrollbarGutter.y + 8.0f },
            { scrollbarGutter.x, scrollbarGutter.y + scrollbarGutter.height - 8.0f },
            1.0f,
            Fade(kPaperEdge, 0.8f));
    }
    void NarrativeNotebook::drawNotebookHeader(const Rectangle& bounds) const
    {
        const Rectangle headerBand = getNotebookHeaderBand(bounds);
        const char* title = notebookPage == NotebookPage::CaseNotes ? "CASE NOTES" : "TO DO";
        drawCenteredUnderlinedHeader(
            boldFont,
            title,
            headerBand,
            notebookHeaderFontSize,
            kNotebookHeader);
    }
    void NarrativeNotebook::drawNotebookNavButtons(const Rectangle& bounds) const
    {
        const bool prevEnabled = notebookPage != NotebookPage::CaseNotes;
        const bool nextEnabled = notebookPage != NotebookPage::Todo;

        const float arrowFontSize = notebookHeaderFontSize;
        const Vector2 arrowSize = MeasureTextEx(boldFont, "<", arrowFontSize, 1.0f);
        const float buttonWidth = arrowSize.x + 6.0f;
        const float buttonHeight = arrowSize.y + 4.0f;
        const float gap = 4.0f;
        const float topPad = 10.0f;
        const float rightPad = 8.0f;

        const float buttonsRight = bounds.x + bounds.width - kScrollbarWidth - rightPad;
        const Rectangle nextBounds = {
            buttonsRight - buttonWidth,
            bounds.y + topPad,
            buttonWidth,
            buttonHeight
        };
        const Rectangle prevBounds = {
            nextBounds.x - gap - buttonWidth,
            bounds.y + topPad,
            buttonWidth,
            buttonHeight
        };

        const Vector2 mousePos = GetMousePosition();
        const bool prevHovered = prevEnabled && CheckCollisionPointRec(mousePos, prevBounds);
        const bool nextHovered = nextEnabled && CheckCollisionPointRec(mousePos, nextBounds);

        drawNotebookArrowButton(boldFont, "<", prevBounds, prevEnabled, prevHovered);
        drawNotebookArrowButton(boldFont, ">", nextBounds, nextEnabled, nextHovered);
    }
    bool NarrativeNotebook::canUseNotebookNav() const
    {
        return notebookNavEnabled;
    }
    void NarrativeNotebook::handleNotebookNavInput()
    {
        if (!canUseNotebookNav())
            return;

        const Rectangle dialog = getDialogBounds();
        const bool prevEnabled = notebookPage != NotebookPage::CaseNotes;
        const bool nextEnabled = notebookPage != NotebookPage::Todo;

        const float arrowFontSize = notebookHeaderFontSize;
        const Vector2 arrowSize = MeasureTextEx(boldFont, "<", arrowFontSize, 1.0f);
        const float buttonWidth = arrowSize.x + 6.0f;
        const float buttonHeight = arrowSize.y + 4.0f;
        const float gap = 4.0f;
        const float topPad = 10.0f;
        const float rightPad = 8.0f;

        const float buttonsRight = dialog.x + dialog.width - kScrollbarWidth - rightPad;
        const Rectangle nextBounds = {
            buttonsRight - buttonWidth,
            dialog.y + topPad,
            buttonWidth,
            buttonHeight
        };
        const Rectangle prevBounds = {
            nextBounds.x - gap - buttonWidth,
            dialog.y + topPad,
            buttonWidth,
            buttonHeight
        };

        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            return;

        const Vector2 mousePos = GetMousePosition();
        if (prevEnabled && CheckCollisionPointRec(mousePos, prevBounds))
        {
            notebookPage = NotebookPage::CaseNotes;
            todoScroll.resetScroll();
        }
        else if (nextEnabled && CheckCollisionPointRec(mousePos, nextBounds))
        {
            notebookPage = NotebookPage::Todo;
            todoScroll.resetScroll();
        }
    }
    void NarrativeNotebook::drawTodoPage() const
    {
        const Rectangle dialog = getDialogBounds();
        const Rectangle clipArea = getNotebookContentBounds();

        BeginScissorMode(
            (int)clipArea.x,
            (int)clipArea.y,
            (int)clipArea.width,
            (int)clipArea.height);

        const float lineHeight = getNarrativeLineHeight();
        float contentY = 0.0f;
        const std::vector<const MilestoneDef*> quests = progressionService->getVisibleQuests();

        const Rectangle content = getNotebookContentBounds();

        if (quests.empty())
        {
            const char* emptyText = "No quests on record yet.";
            DrawTextEx(
                descriptionFont,
                emptyText,
                { dialog.x + xOffset, content.y + contentY - todoScroll.getScrollY() },
                fontSize,
                spacing,
                Fade(textColor, 0.72f));
            contentY += lineHeight * 1.5f;
        }
        else
        {
            for (const MilestoneDef* quest : quests)
            {
                if (!quest)
                    continue;

                const MilestoneStatus status = progressionService->getMilestoneStatus(quest->id);
                std::string questBody;
                if (status == MilestoneStatus::Complete && !quest->completeSummary.empty())
                    questBody = quest->completeSummary;
                else if (!quest->title.empty())
                    questBody = quest->title;
                else
                    questBody = quest->summary;

                std::string displayLine;
                Color lineColor = textColor;
                if (status == MilestoneStatus::Complete)
                {
                    displayLine = std::string("+ ") + questBody;
                    lineColor = kQuestComplete;
                }
                else
                {
                    displayLine = std::string("- ") + questBody;
                    if (status == MilestoneStatus::Failed)
                        lineColor = kQuestFailed;
                }

                const float blockStartY = contentY;
                layoutWrappedParagraph(
                    displayLine.c_str(),
                    status == MilestoneStatus::Complete ? boldFont : descriptionFont,
                    fontSize,
                    contentY,
                    true,
                    todoScroll.getScrollY(),
                    lineColor);

                if (status == MilestoneStatus::Failed)
                {
                    const float drawY = content.y + blockStartY - todoScroll.getScrollY();
                    const float blockHeight = std::max(lineHeight, contentY - blockStartY);
                    const float strikeY = drawY + blockHeight * 0.55f;
                    DrawLineEx(
                        { dialog.x + xOffset, strikeY },
                        { dialog.x + xOffset + getNarrativeWrapWidth(), strikeY },
                        1.5f,
                        kQuestFailed);
                }

                contentY += lineHeight * 0.35f;
            }
        }

        todoContentHeight = contentY;
        EndScissorMode();
    }
    void NarrativeNotebook::drawTodoScrollbar() const
    {
        const Rectangle dialog = getDialogBounds();
        const Rectangle content = getNotebookContentBounds();
        todoScroll.setContentHeight(todoContentHeight);
        todoScroll.setScrollTrack({
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
            kScrollbarWidth,
            content.height
        });
        todoScroll.drawScrollbar();
    }
    void NarrativeNotebook::handleTodoScrollInput()
    {
        const Rectangle dialog = getDialogBounds();
        const Rectangle content = getNotebookContentBounds();

        todoScroll.setContentHeight(todoContentHeight);
        todoScroll.setVisibleArea(content);
        todoScroll.setScrollTrack({
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
            kScrollbarWidth,
            content.height
        });
        todoScroll.setWheelStep(getNarrativeLineHeight());
        todoScroll.setWheelMultiplier(3.0f);
        todoScroll.handleInput();
    }
    Rectangle NarrativeNotebook::getNotebookContentBounds() const
    {
        const Rectangle dialog = getDialogBounds();
        const float contentTop = dialog.y + notebookHeaderReserve;
        const float contentHeight = dialog.height - notebookHeaderReserve - notebookContentBottomPad;
        return {
            dialog.x,
            contentTop,
            dialog.width - kScrollbarWidth,
            std::max(0.0f, contentHeight)
        };
    }
    float NarrativeNotebook::getNarrativeVisibleHeight() const
    {
        return getNotebookContentBounds().height;
    }
    float NarrativeNotebook::getNarrativeWrapWidth() const
    {
        return getDialogBounds().width - xOffset - kScrollbarWidth - 8.0f;
    }
    void NarrativeNotebook::trimBuffer()
    {
        std::vector<std::string> lines;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
            lines.push_back(line);

        if ((int)lines.size() <= NarrativeNotebook::kMaxNarrativeLines)
            return;

        lines.erase(lines.begin(), lines.begin() + (lines.size() - NarrativeNotebook::kMaxNarrativeLines));

        std::ostringstream rebuilt;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i > 0)
                rebuilt << '\n';
            rebuilt << lines[i];
        }

        narrativeText = rebuilt.str();
        narrativeLayoutDirty = true;
    }
    bool NarrativeNotebook::isBoldNarrativeHeader(const std::string& line) const
    {
        return line == "Examining:" || line == "Speaking:" || line == "Using:";
    }
    bool NarrativeNotebook::isBoldNarrativeLine(const std::string& line) const
    {
        return isBoldNarrativeHeader(line)
            || (conversationMgr->isAwaitingChoice() && isDialogChoiceLine(line))
            || (committedPlayerDialogLines != nullptr
                && committedPlayerDialogLines->count(line) > 0);
    }
    std::string NarrativeNotebook::normalizeNarrativeLine(std::string line) const
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return line;
    }
    bool NarrativeNotebook::isDialogChoiceLine(const std::string& line) const
    {
        return line.size() > 2 && line.compare(0, 2, "> ") == 0;
    }
    bool NarrativeNotebook::isNarrativeSketchLine(const std::string& line) const
    {
        const size_t prefixLen = sizeof(kNarrativeSketchPrefix) - 1;
        return line.size() > prefixLen && line.compare(0, prefixLen, kNarrativeSketchPrefix) == 0;
    }
    std::string NarrativeNotebook::narrativeSketchPathFromLine(const std::string& line) const
    {
        if (!isNarrativeSketchLine(line))
            return "";

        const size_t prefixLen = sizeof(kNarrativeSketchPrefix) - 1;
        return line.substr(prefixLen);
    }
    float NarrativeNotebook::getNarrativeSketchDisplaySize(
        const Texture2D& texture,
        float& outWidth,
        float& outHeight) const
    {
        if (texture.id == 0 || texture.width <= 0 || texture.height <= 0)
        {
            outWidth = 0.0f;
            outHeight = 0.0f;
            return 0.0f;
        }

        const float maxWidth = getNarrativeWrapWidth();
        const float maxHeight = std::max(getNarrativeLineHeight() * 5.0f, 220.0f);
        const float widthScale = maxWidth / (float)texture.width;
        const float heightScale = maxHeight / (float)texture.height;
        const float scale = std::min(widthScale, heightScale);

        outWidth = (float)texture.width * scale;
        outHeight = (float)texture.height * scale;
        return outHeight + getNarrativeLineHeight() * 0.5f;
    }
    Texture2D NarrativeNotebook::getOrLoadNarrativeSketchTexture(const std::string& sketchPath) const
    {
        if (sketchPath.empty())
            return {};

        const auto cached = narrativeSketchTextures.find(sketchPath);
        if (cached != narrativeSketchTextures.end())
            return cached->second;

        Texture2D texture{};
        if (!loadResourceTexture(primaryAssetRoot, sketchPath, texture))
        {
            const std::vector<std::string> paths = buildAssetSearchPaths(
                primaryAssetRoot,
                sketchPath);
            for (const std::string& path : paths)
            {
                if (!FileExists(path.c_str()))
                    continue;

                texture = LoadTexture(path.c_str());
                if (texture.id != 0)
                    break;

                if (loadTextureFromAssetFile(path, texture))
                    break;
            }
        }

        if (texture.id != 0)
        {
            SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
            narrativeSketchTextures[sketchPath] = texture;
            TraceLog(LOG_INFO, "Loaded narrative sketch: %s (%dx%d)", sketchPath.c_str(), texture.width, texture.height);
            return texture;
        }

        TraceLog(LOG_WARNING, "Failed to load narrative sketch: %s", sketchPath.c_str());
        return {};
    }
    float NarrativeNotebook::getNarrativeSketchHeight(const std::string& sketchPath) const
    {
        const Texture2D texture = getOrLoadNarrativeSketchTexture(sketchPath);
        float drawWidth = 0.0f;
        float drawHeight = 0.0f;
        return getNarrativeSketchDisplaySize(texture, drawWidth, drawHeight);
    }
    void NarrativeNotebook::layoutNarrativeSketch(const std::string& sketchPath, float& textOffsetY) const
    {
        const Texture2D texture = getOrLoadNarrativeSketchTexture(sketchPath);
        float drawWidth = 0.0f;
        float drawHeight = 0.0f;
        const float blockHeight = getNarrativeSketchDisplaySize(texture, drawWidth, drawHeight);
        if (texture.id == 0 || blockHeight <= 0.0f)
            return;

        NarrativeSketchPlacement placement;
        placement.path = sketchPath;
        placement.yOffset = textOffsetY;
        placement.width = drawWidth;
        placement.height = drawHeight;
        narrativeSketchPlacements.push_back(placement);
        textOffsetY += blockHeight;
    }
    void NarrativeNotebook::drawNarrativeSketches() const
    {
        const Rectangle content = getNotebookContentBounds();
        const Rectangle dialog = getDialogBounds();
        const float visibleHeight = content.height;

        for (const NarrativeSketchPlacement& placement : narrativeSketchPlacements)
        {
            const Texture2D texture = getOrLoadNarrativeSketchTexture(placement.path);
            if (texture.id == 0)
                continue;

            const float drawY = content.y + placement.yOffset - narrativeScroll.getScrollY();
            const bool visible = (placement.yOffset + placement.height > narrativeScroll.getScrollY()) &&
                                 (placement.yOffset < narrativeScroll.getScrollY() + visibleHeight);
            if (!visible)
                continue;

            const Rectangle source = { 0.0f, 0.0f, (float)texture.width, (float)texture.height };
            const Rectangle dest = {
                dialog.x + xOffset,
                drawY,
                placement.width,
                placement.height
            };
            DrawTexturePro(texture, source, dest, { 0.0f, 0.0f }, 0.0f, WHITE);
        }
    }
    void NarrativeNotebook::appendSketch(const std::string& sketchPath)
    {
        if (sketchPath.empty())
            return;

        getOrLoadNarrativeSketchTexture(sketchPath);
        narrativeText += "\n";
        narrativeText += kNarrativeSketchPrefix;
        narrativeText += sketchPath;
        trimBuffer();
        narrativeLayoutDirty = true;
    }
    Color NarrativeNotebook::narrativeLineColor(const std::string& line) const
    {
        if (!conversationMgr->isAwaitingChoice() || !isDialogChoiceLine(line))
            return textColor;

        for (const ConversationChoiceDef& choice : conversationMgr->getPendingChoices())
        {
            if (line == choice.label)
            {
                const Vector2 mousePos = GetMousePosition();
                for (const NarrativeChoiceHitArea& hitArea : narrativeChoiceHitAreas)
                {
                    if (hitArea.id == choice.id && CheckCollisionPointRec(mousePos, hitArea.bounds))
                        return kChoiceHover;
                }
                return kChoiceText;
            }
        }

        return textColor;
    }
    void NarrativeNotebook::appendSection(const char* header, const std::string& details)
    {
        if (details.empty())
            return;

        narrativeText += "\n\n";
        narrativeText += header;
        narrativeText += "\n";
        narrativeText += details;
        trimBuffer();
        narrativeLayoutDirty = true;
        scrollToHeader(header);
    }
    void NarrativeNotebook::appendChoiceLines(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& scrollAnchorLine)
    {
        const std::vector<ConversationChoiceDef> available = filterChoices(choices);
        for (const ConversationChoiceDef& choice : available)
        {
            narrativeText += "\n";
            narrativeText += choice.label;
        }

        trimBuffer();
        narrativeLayoutDirty = true;

        if (!available.empty())
            scrollToPendingChoices(scrollAnchorLine);
    }
    void NarrativeNotebook::scrollToPendingChoices(const std::string& preferredLine)
    {
        const std::vector<ConversationChoiceDef>& choices = conversationMgr->getPendingChoices();
        if (choices.empty())
            return;

        rebuildNarrativeLayout();

        if (!preferredLine.empty())
        {
            scrollToLine(preferredLine, true);
            return;
        }

        static const char* kEventHeaders[] = { "Speaking:", "Examining:", "Using:" };
        for (const char* header : kEventHeaders)
        {
            const float headerOffset = getNarrativeLineOffsetY(header, true);
            if (headerOffset >= 0.0f)
            {
                const float maxScroll = std::max(0.0f, narrativeContentHeight - getNarrativeVisibleHeight());
                narrativeScroll.setScrollY(std::max(0.0f, std::min(headerOffset, maxScroll)));
                return;
            }
        }

        scrollToLine(choices.front().label, true);
    }

void NarrativeNotebook::resetNarrativeScroll()
{
    narrativeScroll.resetScroll();
}

void NarrativeNotebook::resetInventoryExamineScroll()
{
    inventoryExamineScroll.resetScroll();
}

    void NarrativeNotebook::stripChoiceLines(
        const std::vector<ConversationChoiceDef>& choices,
        const std::string& keepLineText)
    {
        if (choices.empty())
            return;

        std::vector<std::string> lines;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            bool isChoiceLine = false;
            for (const ConversationChoiceDef& choice : choices)
            {
                if (line == choice.label)
                {
                    isChoiceLine = true;
                    break;
                }
            }

            if (isChoiceLine && line == keepLineText)
            {
                lines.push_back(line);
                continue;
            }

            if (!isChoiceLine)
                lines.push_back(line);
        }

        std::ostringstream rebuilt;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i > 0)
                rebuilt << '\n';
            rebuilt << lines[i];
        }

        narrativeText = rebuilt.str();
        narrativeLayoutDirty = true;
    }
    void NarrativeNotebook::handleNarrativeChoiceInput()
    {
        if (!conversationMgr->isAwaitingChoice())
            return;

        if (narrativeLayoutDirty)
            rebuildNarrativeLayout();

        rebuildNarrativeChoiceHitAreas();

        if (narrativeChoiceHitAreas.empty())
            return;

        const Rectangle textArea = getNotebookContentBounds();
        const Vector2 mousePos = GetMousePosition();
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || !CheckCollisionPointRec(mousePos, textArea))
            return;

        for (const NarrativeChoiceHitArea& hitArea : narrativeChoiceHitAreas)
        {
            if (CheckCollisionPointRec(mousePos, hitArea.bounds))
            {
                onChoiceSelected(hitArea.id);
                return;
            }
        }
    }
    float NarrativeNotebook::getNarrativeLineOffsetY(const std::string& lineText, bool lastOccurrence) const
    {
        float textOffsetY = 0.0f;
        float foundOffset = -1.0f;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            line = normalizeNarrativeLine(line);
            if (line.empty())
            {
                textOffsetY += getNarrativeLineHeight() * 0.5f;
                continue;
            }

            if (line == lineText)
                foundOffset = textOffsetY;
            else if (!lastOccurrence && foundOffset >= 0.0f)
            {
                narrativeContentHeight = textOffsetY;
                narrativeLayoutDirty = false;
                return foundOffset;
            }

            if (isNarrativeSketchLine(line))
            {
                textOffsetY += getNarrativeSketchHeight(narrativeSketchPathFromLine(line));
                continue;
            }

            const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
            layoutWrappedParagraph(line.c_str(), lineFont, fontSize, textOffsetY, false, 0.0f, textColor);
        }

        narrativeContentHeight = textOffsetY;
        narrativeLayoutDirty = false;
        return foundOffset;
    }
    void NarrativeNotebook::scrollToHeader(const char* header)
    {
        scrollToLine(header, true);
    }
    void NarrativeNotebook::scrollToLine(const std::string& lineText, bool lastOccurrence)
    {
        const float lineOffset = getNarrativeLineOffsetY(lineText, lastOccurrence);
        if (lineOffset < 0.0f)
            return;

        const float maxScroll = std::max(0.0f, narrativeContentHeight - getNarrativeVisibleHeight());
        narrativeScroll.setScrollY(std::max(0.0f, std::min(lineOffset, maxScroll)));
    }
    void NarrativeNotebook::rebuildNarrativeChoiceHitAreas() const
    {
        narrativeChoiceHitAreas.clear();

        if (!conversationMgr->isAwaitingChoice() || conversationMgr->getPendingChoices().empty())
            return;

        const Rectangle dialog = getDialogBounds();

        float textOffsetY = 0.0f;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            line = normalizeNarrativeLine(line);
            if (line.empty())
            {
                textOffsetY += getNarrativeLineHeight() * 0.5f;
                continue;
            }

            if (isNarrativeSketchLine(line))
            {
                textOffsetY += getNarrativeSketchHeight(narrativeSketchPathFromLine(line));
                continue;
            }

            if (isDialogChoiceLine(line))
            {
                for (const ConversationChoiceDef& choice :
                    filterChoices(conversationMgr->getPendingChoices()))
                {
                    if (line != choice.label)
                        continue;

                    const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
                    float measureY = textOffsetY;
                    layoutWrappedParagraph(line.c_str(), lineFont, fontSize, measureY, false, 0.0f, textColor);
                    const float choiceHeight = std::max(getNarrativeLineHeight(), measureY - textOffsetY);
                    const float drawY = getNotebookContentBounds().y + textOffsetY - narrativeScroll.getScrollY();

                    narrativeChoiceHitAreas.push_back({
                        choice.id,
                        { dialog.x + xOffset, drawY, getNarrativeWrapWidth(), choiceHeight }
                    });
                    break;
                }
            }

            const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
            layoutWrappedParagraph(line.c_str(), lineFont, fontSize, textOffsetY, false, 0.0f, textColor);
        }
    }
    void NarrativeNotebook::rebuildNarrativeLayout() const
    {
        narrativeContentHeight = 0.0f;
        narrativeSketchPlacements.clear();

        float textOffsetY = 0.0f;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            line = normalizeNarrativeLine(line);
            if (line.empty())
            {
                textOffsetY += getNarrativeLineHeight() * 0.5f;
                continue;
            }

            if (isNarrativeSketchLine(line))
            {
                layoutNarrativeSketch(narrativeSketchPathFromLine(line), textOffsetY);
                continue;
            }

            const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
            layoutWrappedParagraph(line.c_str(), lineFont, fontSize, textOffsetY, false, 0.0f, textColor);
        }

        narrativeContentHeight = textOffsetY;
        narrativeLayoutDirty = false;
    }
    void NarrativeNotebook::handleNarrativeScrollInput()
    {
        if (narrativeLayoutDirty)
            rebuildNarrativeLayout();

        const Rectangle content = getNotebookContentBounds();
        const Rectangle dialog = getDialogBounds();

        narrativeScroll.setContentHeight(narrativeContentHeight);
        narrativeScroll.setVisibleArea(content);
        narrativeScroll.setScrollTrack({
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
            kScrollbarWidth,
            content.height
        });
        narrativeScroll.setWheelStep(getNarrativeLineHeight());
        narrativeScroll.setWheelMultiplier(3.0f);
        narrativeScroll.setClampToBottom(true);
        narrativeScroll.handleInput();
    }
    void NarrativeNotebook::drawNarrativeScrollbar() const
    {
        if (narrativeLayoutDirty)
            rebuildNarrativeLayout();

        const Rectangle dialog = getDialogBounds();
        const Rectangle content = getNotebookContentBounds();
        narrativeScroll.setContentHeight(narrativeContentHeight);
        narrativeScroll.setScrollTrack({
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
            kScrollbarWidth,
            content.height
        });
        narrativeScroll.drawScrollbar();
    }
    void NarrativeNotebook::drawNarrativeText() const
    {
        if (narrativeLayoutDirty)
            rebuildNarrativeLayout();

        rebuildNarrativeChoiceHitAreas();

        const Rectangle clipArea = getNotebookContentBounds();

        BeginScissorMode(
            (int)clipArea.x,
            (int)clipArea.y,
            (int)clipArea.width,
            (int)clipArea.height);

        float textOffsetY = 0.0f;
        std::istringstream stream(narrativeText);
        std::string line;

        while (std::getline(stream, line))
        {
            line = normalizeNarrativeLine(line);
            if (line.empty())
            {
                textOffsetY += getNarrativeLineHeight() * 0.5f;
                continue;
            }

            if (isNarrativeSketchLine(line))
            {
                textOffsetY += getNarrativeSketchHeight(narrativeSketchPathFromLine(line));
                continue;
            }

            const Font lineFont = isBoldNarrativeLine(line) ? boldFont : descriptionFont;
            const Color lineColor = narrativeLineColor(line);
            layoutWrappedParagraph(line.c_str(), lineFont, fontSize, textOffsetY, true, narrativeScroll.getScrollY(), lineColor);
        }

        drawNarrativeSketches();

        EndScissorMode();
    }
    void NarrativeNotebook::drawInventoryExamineView(const InventoryItem* item) const
    {
        if (item == nullptr)
            return;

        const Rectangle dialog = getDialogBounds();
        const Rectangle clipArea = getNotebookContentBounds();

        BeginScissorMode(
            (int)clipArea.x,
            (int)clipArea.y,
            (int)clipArea.width,
            (int)clipArea.height);

        float contentY = 0.0f;
        const float lineHeight = getNarrativeLineHeight();

        const char* header = "Examining:";
        DrawTextEx(
            boldFont,
            header,
            { dialog.x + xOffset, clipArea.y + contentY - inventoryExamineScroll.getScrollY() },
            fontSize,
            spacing,
            textColor);
        contentY += lineHeight * 1.2f;

        layoutWrappedParagraph(
            item->examineText.c_str(),
            descriptionFont,
            fontSize,
            contentY,
            true,
            inventoryExamineScroll.getScrollY(),
            textColor);

        if (item->weightLb > 0.0f)
        {
            char weightLine[64];
            std::snprintf(weightLine, sizeof(weightLine), "Weight: %.2f lb", item->weightLb);
            contentY += lineHeight * 0.4f;
            DrawTextEx(
                descriptionFont,
                weightLine,
                { dialog.x + xOffset, clipArea.y + contentY - inventoryExamineScroll.getScrollY() },
                fontSize * 0.85f,
                spacing,
                textColor);
            contentY += lineHeight;
        }

        inventoryExamineContentHeight = contentY;
        EndScissorMode();
    }
    void NarrativeNotebook::drawInventoryExamineScrollbar() const
    {
        const Rectangle dialog = getDialogBounds();
        const Rectangle content = getNotebookContentBounds();
        inventoryExamineScroll.setContentHeight(inventoryExamineContentHeight);
        inventoryExamineScroll.setScrollTrack({
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
            kScrollbarWidth,
            content.height
        });
        inventoryExamineScroll.drawScrollbar();
    }
    void NarrativeNotebook::handleInventoryExamineScrollInput()
    {
        const Rectangle dialog = getDialogBounds();
        const Rectangle content = getNotebookContentBounds();

        inventoryExamineScroll.setContentHeight(inventoryExamineContentHeight);
        inventoryExamineScroll.setVisibleArea(content);
        inventoryExamineScroll.setScrollTrack({
            dialog.x + dialog.width - kScrollbarWidth,
            content.y,
            kScrollbarWidth,
            content.height
        });
        inventoryExamineScroll.setWheelStep(getNarrativeLineHeight());
        inventoryExamineScroll.setWheelMultiplier(3.0f);
        inventoryExamineScroll.handleInput();
    }
    void NarrativeNotebook::layoutWrappedParagraph(const char* text, Font font, float paragraphFontSize, float& textOffsetY, bool draw, float scrollY, Color lineColor) const
    {
        int length = TextLength(text);

        float textOffsetX = 0.0f;
        float scaleFactor = paragraphFontSize / (float)font.baseSize;
        float lineHeight = (font.baseSize + font.baseSize / 2.0f) * scaleFactor;
        const float wrapWidth = getNarrativeWrapWidth();
        const Rectangle content = getNotebookContentBounds();
        const float visibleHeight = content.height;
        const Rectangle dialog = getDialogBounds();

        enum { MEASURE_STATE = 0, DRAW_STATE = 1 };
        int state = wordWrap ? MEASURE_STATE : DRAW_STATE;

        int startLine = -1;
        int endLine = -1;
        int lastk = -1;

        for (int i = 0, k = 0; i < length; i++, k++)
        {
            int codepointByteCount = 0;
            int codepoint = GetCodepoint(&text[i], &codepointByteCount);
            int index = GetGlyphIndex(font, codepoint);

            if (codepoint == 0x3f) codepointByteCount = 1;
            i += (codepointByteCount - 1);

            float glyphWidth = 0;
            if (codepoint != '\n')
            {
                glyphWidth = (font.glyphs[index].advanceX == 0)
                    ? font.recs[index].width * scaleFactor
                    : font.glyphs[index].advanceX * scaleFactor;

                if (i + 1 < length) glyphWidth = glyphWidth + spacing;
            }

            if (state == MEASURE_STATE)
            {
                if ((codepoint == ' ') || (codepoint == '\t') || (codepoint == '\n')) endLine = i;

                if ((textOffsetX + glyphWidth) > wrapWidth)
                {
                    endLine = (endLine < 1) ? i : endLine;
                    if (i == endLine) endLine -= codepointByteCount;
                    if ((startLine + codepointByteCount) == endLine) endLine = (i - codepointByteCount);

                    state = !state;
                }
                else if ((i + 1) == length)
                {
                    endLine = i;
                    state = !state;
                }
                else if (codepoint == '\n') state = !state;

                if (state == DRAW_STATE)
                {
                    textOffsetX = 0;
                    i = startLine;
                    glyphWidth = 0;

                    int tmp = lastk;
                    lastk = k - 1;
                    k = tmp;
                }
            }
            else
            {
                if (codepoint == '\n')
                {
                    if (!wordWrap)
                    {
                        textOffsetY += lineHeight;
                        textOffsetX = 0;
                    }
                }
                else
                {
                    if (!wordWrap && ((textOffsetX + glyphWidth) > wrapWidth))
                    {
                        textOffsetY += lineHeight;
                        textOffsetX = 0;
                    }

                    const float drawY = content.y + textOffsetY - scrollY;
                    const bool visible = (textOffsetY + lineHeight > scrollY) &&
                                         (textOffsetY < scrollY + visibleHeight);

                    if (draw && visible && (codepoint != ' ') && (codepoint != '\t'))
                    {
                        DrawTextCodepoint(
                            font,
                            codepoint,
                            Vector2{ dialog.x + xOffset + textOffsetX, drawY },
                            paragraphFontSize,
                            lineColor);
                    }
                }

                if (wordWrap && (i == endLine))
                {
                    textOffsetY += lineHeight;
                    textOffsetX = 0;
                    startLine = endLine;
                    endLine = -1;
                    glyphWidth = 0;
                    k = lastk;

                    state = !state;
                }
            }

            if ((textOffsetX != 0) || (codepoint != ' ')) textOffsetX += glyphWidth;
        }
    }
}
