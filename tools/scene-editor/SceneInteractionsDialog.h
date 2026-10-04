/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Author scene interactions[] — place-item (grantItem) and Use actions (#42 P4).
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_INTERACTIONS_DIALOG_H
#define TIMBERLINE_SCENE_INTERACTIONS_DIALOG_H

#include "DocumentWorkspace.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct SceneInteractionEdit
{
    std::string id;
    std::string label;
    std::string useDetails;
    std::string grantItemId;
    bool requiresExamine = true;
    bool repeat = false;
    std::string useFlag;
    std::string hideWhenStoryFlag;
    std::string requiresAnyInventoryItemsCsv;
    std::string exitSceneId; // read-only hint when map Use stub
    /** Original JSON row so Save merges without clobbering TTS/overlays/deltas. */
    nlohmann::json original = nlohmann::json::object();
};

struct SceneInteractionsDialog
{
    DocumentWorkspace* docs = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;

    std::string sceneId;
    std::vector<SceneInteractionEdit> entries;
    int selectedIndex = -1;
    float listScroll = 0.0f;
    float detailScroll = 0.0f;

    int focusField = -1;

    bool addPickerOpen = false;
    float addPickerScroll = 0.0f;
    std::string addFilter;

    std::string status;
    std::string error;

    std::function<void()> onSaved;

    void openForScene(const std::string& id);
    void closeDialog();
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    enum FocusField
    {
        FocusNone = -1,
        FocusId = 0,
        FocusLabel,
        FocusUseDetails,
        FocusGrantItem,
        FocusUseFlag,
        FocusHideFlag,
        FocusRequiresItems,
        FocusCount
    };

    void loadFromScene();
    bool saveToScene();
    void addPlaceItem(const std::string& itemId);
    void removeSelected();
    SceneInteractionEdit* selected();
    const SceneInteractionEdit* selected() const;
    std::string* focusString();
    void typeIntoFocusedField();
    void typeIntoFilter();
    std::string resolveItemName(const std::string& itemId) const;
    std::string uniqueInteractionId(const std::string& base) const;
    static std::vector<std::string> splitCsv(const std::string& csv);
    static std::string joinCsv(const std::vector<std::string>& values);
};

} // namespace timberline_editor

#endif
