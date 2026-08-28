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

#ifndef TIMBERLINE_ITEM_EDITOR_H
#define TIMBERLINE_ITEM_EDITOR_H

#include "DocumentWorkspace.h"
#include "EditorTypes.h"
#include "ItemAuthoring.h"
#include "VariableEditor.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

/**
 * Left-pane item browser for the items.json tab.
 * Tree of items → editable fields, plus New/Modify Item authoring dialog.
 */
struct ItemEditor
{
    std::vector<ConversationTreeNode> roots;
    std::set<std::string> expanded;
    std::string selectedKey;
    std::string selectedItemId;
    mutable std::vector<ConversationVisibleRow> visibleRowsCache;
    mutable bool visibleRowsDirty = true;
    float listScroll = 0.0f;

    DocumentWorkspace* docs = nullptr;
    VariableEditor* text = nullptr;
    bool* stackDialogOpen = nullptr;
    std::function<bool()> draggingDivider;
    Font uiFont{};
    Font uiFontBold{};

    // --- Authoring dialog (create + modify) ---
    bool authoringDialogOpen = false;
    bool authoringIsModify = false;
    ItemAuthoringPayload authoringPayload{};
    /** 0=name, 1=weight, 2=AI API key (description is dialog-only) */
    int authoringFocusField = 0;
    std::string authoringWeightBuffer = "0.1";
    /**
     * Session-only xAI API key for AI Assist image generation.
     * Never written to items.json or resources/xai_api_key.
     */
    std::string authoringAiApiKey;
    std::string authoringError;
    std::string lastAuthoringStatus;
    int authoringIgnoreInputFrames = 0;
    bool authoringWaitMouseRelease = false;
    float authoringScrollY = 0.0f;
    bool authoringDraggingScroll = false;
    bool authoringRecipeAdvanced = false;

    int authoringDropdown = 0;
    float authoringDropdownScroll = 0.0f;
    Rectangle authoringDropdown1Rect{0, 0, 0, 0};
    Rectangle authoringDropdown2Rect{0, 0, 0, 0};

    // Right-side asset preview (examine image, icon, sound controls).
    Texture2D authoringPreviewExamine{};
    Texture2D authoringPreviewIcon{};
    bool authoringPreviewExamineLoaded = false;
    bool authoringPreviewIconLoaded = false;
    std::string authoringPreviewExaminePath;
    std::string authoringPreviewIconPath;
    Sound authoringPreviewExamineSound{};
    Sound authoringPreviewUseSound{};
    bool authoringPreviewExamineSoundLoaded = false;
    bool authoringPreviewUseSoundLoaded = false;
    std::string authoringPreviewExamineSoundPath;
    std::string authoringPreviewUseSoundPath;
    bool authoringAudioReady = false;
    /** 0 = none, 1 = examine, 2 = use */
    int authoringPlayingSound = 0;

    /**
     * Background AI asset generation. system() is blocking; we run it off the
     * UI thread so the dialog can show a pulsing "Working" label.
     * 0=none, 1=examine image, 2=icon, 3=examine sfx, 4=use sfx, 5=all
     */
    std::atomic<bool> authoringGenerateBusy{false};
    std::atomic<int> authoringGenerateTarget{0};
    std::mutex authoringGenerateMutex;
    std::string authoringGenerateResultStatus;
    bool authoringGenerateResultPending = false;
    std::thread authoringGenerateThread;

    enum class SubEditKind
    {
        None,
        Description,
        TtsDescription,
        ConstructionDescription,
        TtsConstructionDescription,
        ImagePath,
        IconPath,
        ExamineSound,
        UseSound,
        RecipeAdvancedJson
    };
    bool subEditOpen = false;
    SubEditKind subEditKind = SubEditKind::None;
    std::string subEditBuffer;
    std::string subEditTitle;
    int subEditCursor = 0;
    int subEditSelectAnchor = -1;
    float subEditScrollY = 0.0f;
    float subEditPreferX = 0.0f;
    int subEditIgnoreFrames = 0;
    float subEditKeyRepeatTimer = 0.0f;
    int subEditKeyRepeatKey = 0;
    bool subEditMouseSelecting = false;
    bool subEditSyntaxHighlight = false;
    Rectangle subEditFieldRect{0, 0, 0, 0};

    void invalidateVisibleRows();
    void rebuildTree();
    void handleInput(Rectangle listBounds);
    void draw(Rectangle listBounds);
    void openNodeEditor(const ConversationTreeNode& node);

    bool isExpanded(const std::string& key) const;
    void toggleExpanded(const std::string& key);
    bool allRootsExpanded() const;
    void expandAllRoots();
    void collapseAllRoots();
    void toggleExpandAllRoots();

    void collectVisibleRows(
        const ConversationTreeNode& node,
        int depth,
        bool isLastChild,
        std::vector<bool> ancestorContinues,
        std::vector<ConversationVisibleRow>& out) const;

    const std::vector<ConversationVisibleRow>& visibleRows() const;

    static std::string truncateForTree(const std::string& text, size_t maxLen);
    static std::string truncateToWidth(
        Font font,
        const std::string& text,
        float fontSize,
        float maxWidth);

    bool blocksInput() const { return authoringDialogOpen; }

    void openNewItemDialog();
    void openModifyItemDialog(const std::string& itemId);
    void closeAuthoringDialog();
    void handleAuthoringDialogInput(int screenWidth, int screenHeight);
    void drawAuthoringDialog(int screenWidth, int screenHeight);
    bool commitAuthoringDialog();
    /**
     * Write AI jobs for current image/icon/sound flags and run the generator.
     * target: 0=all flagged, 1=image, 2=icon, 3=examine sfx, 4=use sfx, 5=all four.
     * Runs on a worker thread; UI shows pulsing "Working" until done.
     */
    bool generateAuthoringAssetsNow(int target = 0);
    /** Call each frame while the authoring dialog is open. */
    void pollAuthoringGenerateResult();
    void joinAuthoringGenerateThread();

    void handleNewItemDialogInput(int screenWidth, int screenHeight)
    {
        handleAuthoringDialogInput(screenWidth, screenHeight);
    }
    void drawNewItemDialog(int screenWidth, int screenHeight)
    {
        drawAuthoringDialog(screenWidth, screenHeight);
    }

    private:
    Rectangle newItemBtnBounds(Rectangle listBounds) const;
    Rectangle editItemBtnBounds(Rectangle listBounds) const;

    void openSubEdit(SubEditKind kind);
    void closeSubEdit(bool apply);
    void handleSubEditInput();
    void drawSubEditDialog(int screenWidth, int screenHeight);
    bool subEditHasSelection() const;
    void subEditSelectionRange(int& start, int& end) const;
    void subEditDeleteSelection();
    void subEditSetCursor(int pos, bool extend);
    bool subEditNavKeyTriggered(int key);
    std::vector<EditorVisualLine> subEditBuildLines(float maxWidth, float fontSize) const;
    void subEditDrawHighlightedText(
        Font font,
        const std::vector<EditorVisualLine>& lines,
        Rectangle field,
        float fontSize,
        float lineHeight) const;

    std::vector<std::string> componentCandidateIds() const;
    void drawOnOffSwitch(
        Font font,
        Rectangle track,
        bool on,
        const char* label,
        bool canClick,
        bool& outToggled);

    void unloadAuthoringPreviews();
    void syncAuthoringPreviews();
    void ensureAuthoringAudio();
    bool loadAuthoringTexture(const std::string& relPath, Texture2D& outTex);
    bool loadAuthoringSound(const std::string& relPath, Sound& outSound);
    void stopAuthoringSounds();
    void drawAuthoringPreviewPane(
        Font font,
        Rectangle pane,
        bool canClick);
};

} // namespace timberline_editor

#endif /* TIMBERLINE_ITEM_EDITOR_H */
