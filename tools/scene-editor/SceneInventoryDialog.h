/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Explicit scene loot editor (takeables + inventory dual-write).
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_INVENTORY_DIALOG_H
#define TIMBERLINE_SCENE_INVENTORY_DIALOG_H

#include "DocumentWorkspace.h"

#include <functional>
#include <string>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct SceneInventoryEntry
{
    std::string id;
    std::string name;
    std::string iconPath;
    std::string examineText;
    bool requiresExamine = true;
    std::string requiresStoryFlag;
    int quantity = 1;
};

struct SceneInventoryDialog
{
    DocumentWorkspace* docs = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;

    std::string sceneId;
    std::vector<SceneInventoryEntry> entries;
    std::string status;
    std::string error;

    bool addPickerOpen = false;
    float addPickerScroll = 0.0f;
    float listScroll = 0.0f;
    std::string addFilter;

    std::function<void()> onSaved;

    void openForScene(const std::string& id);
    void closeDialog();
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    void loadFromScene();
    bool saveToScene();
    void removeAt(size_t index);
    void addItemId(const std::string& itemId);
    std::string resolveItemName(const std::string& itemId) const;
    std::string resolveItemDescription(const std::string& itemId) const;
    std::string resolveItemIcon(const std::string& itemId) const;
    void typeIntoFilter();
};

} // namespace timberline_editor

#endif
