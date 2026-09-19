/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Author scene-local storyEvents[] (enter / exit / examine beats).
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_STORY_EVENTS_DIALOG_H
#define TIMBERLINE_SCENE_STORY_EVENTS_DIALOG_H

#include "DocumentWorkspace.h"

#include <functional>
#include <string>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

struct StoryEventEdit
{
    std::string id;
    std::string when = "exit"; // enter | exit | examine
    std::string direction;     // required when when==exit
    bool requiresExamined = false;
    std::string requiresFlagsCsv;
    std::string unlessFlagsCsv;
    std::string setsFlagsCsv;
    std::string clearsFlagsCsv;
    std::string requiresConsumedStatusCsv;
    std::string narrativeHeader = "Examining:";
    std::string narrative;
    bool blockMovement = false;
    bool refreshTakeables = false;
    bool once = true;
};

struct SceneStoryEventsDialog
{
    DocumentWorkspace* docs = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;

    std::string sceneId;
    std::vector<StoryEventEdit> entries;
    int selectedIndex = -1;
    float listScroll = 0.0f;
    float detailScroll = 0.0f;

    /** Focused detail field: -1 none, else enum below. */
    int focusField = -1;
    int narrativeCursor = 0;

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
        FocusDirection,
        FocusRequiresFlags,
        FocusUnlessFlags,
        FocusSetsFlags,
        FocusClearsFlags,
        FocusConsumedStatus,
        FocusNarrativeHeader,
        FocusNarrative,
        FocusCount
    };

    void loadFromScene();
    bool saveToScene();
    void addEvent();
    void removeSelected();
    StoryEventEdit* selected();
    const StoryEventEdit* selected() const;
    std::string* focusString();
    void typeIntoFocusedField();
    static std::vector<std::string> splitCsv(const std::string& csv);
    static std::string joinCsv(const std::vector<std::string>& values);
};

} // namespace timberline_editor

#endif
