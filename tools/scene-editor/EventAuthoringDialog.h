/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Author a Conversations Trigger Event flowchart node (story flag + status).
 * Dual-writes grantStoryFlag and status onto the parent choice/line.
 ******************************************************************************/

#ifndef TIMBERLINE_EVENT_AUTHORING_DIALOG_H
#define TIMBERLINE_EVENT_AUTHORING_DIALOG_H

#include "DialogFlowCanvas.h"
#include "DocumentWorkspace.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

/** Copy grantStoryFlag + status from a choice/line object onto a TriggerEvent node. */
void fillEventNodeFromChoiceJson(DialogFlowNode& node, const nlohmann::json& choice);

struct EventAuthoringDialog
{
    DocumentWorkspace* docs = nullptr;
    Font uiFont{};
    Font uiFontBold{};
    bool open = false;
    std::function<void()> onSaved;

    void openForEvent(const std::string& sceneId, DialogFlowNode* node);
    void closeDialog();
    bool blocksInput() const { return open; }
    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

    EventAuthoringDialog() = default;
    EventAuthoringDialog(const EventAuthoringDialog&) = delete;
    EventAuthoringDialog& operator=(const EventAuthoringDialog&) = delete;

private:
    std::string sceneId;
    DialogFlowNode* node = nullptr;
    std::string loadedJsonPointer;

    std::string flagId;
    float statusHealth = 0.0f;
    float statusEnergy = 0.0f;
    float statusResolve = 0.0f;
    float statusLucidity = 0.0f;
    float statusCharisma = 0.0f;
    float statusMoney = 0.0f;
    std::string statusKey;
    std::string statusOnZeroLucidity;
    std::string tabActor;
    float tabDelta = 0.0f;
    std::vector<EventOpinionRow> opinions;

    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;
    int focusField = 0;
    std::string focusDraft;
    float scrollY = 0.0f;
    float lastContentH = 0.0f;
    Rectangle lastScrollClip{0, 0, 0, 0};

    std::string status;
    std::string error;

    void resetWorkingState();
    void loadFromNodeAndJson();
    void copyNodeToDialog(const DialogFlowNode& n);
    void copyDialogToNode(DialogFlowNode& n) const;
    bool applyChanges();
    void typeIntoFocusedField();
    std::string* focusedString();
    bool focusedIsNumeric() const;
    void beginNumericFocus(int field, const std::string& initial);
    void commitNumericFocus();
    nlohmann::json* resolveChoiceObject();
    const nlohmann::json* resolveChoiceObject() const;
    void addOpinionRow();
    void removeOpinionRow(int index);
};

} // namespace timberline_editor

#endif
