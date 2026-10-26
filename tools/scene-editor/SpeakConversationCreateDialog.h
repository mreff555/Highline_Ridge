/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Phase A modal: Create Speak conversation for a scene without speakPhases.
 ******************************************************************************/

#ifndef TIMBERLINE_SPEAK_CONVERSATION_CREATE_DIALOG_H
#define TIMBERLINE_SPEAK_CONVERSATION_CREATE_DIALOG_H

#include "DocumentWorkspace.h"
#include "SpeakConversationCreate.h"

#include <functional>
#include <string>

#include <raylib.h>

namespace timberline_editor
{

struct SpeakConversationCreateDialog
{
    DocumentWorkspace* docs = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;

    SpeakConversationCreatePayload payload;
    bool openActorDialogAfter = true;

    /** Called after successful in-memory scaffold (docs already dirty). */
    std::function<void(const SpeakConversationCreatePayload& payload)> onCreated;

    std::string status;
    std::string error;

    void openDialog(const std::string& sceneId);
    void closeDialog(bool apply);
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    enum FocusField
    {
        kFocusActorId = 0,
        kFocusActorName = 1,
        kFocusPhaseId = 2,
        kFocusIntro = 3,
        kFocusChoiceLabel = 4,
        kFocusCount = 5
    };

    int focusField = kFocusActorId;
    Rectangle dialogRect{0, 0, 0, 0};
    Rectangle actorIdRect{0, 0, 0, 0};
    Rectangle actorNameRect{0, 0, 0, 0};
    Rectangle phaseIdRect{0, 0, 0, 0};
    Rectangle introRect{0, 0, 0, 0};
    Rectangle choiceLabelRect{0, 0, 0, 0};
    Rectangle openActorCheckRect{0, 0, 0, 0};
    Rectangle confirmBtnRect{0, 0, 0, 0};
    Rectangle cancelBtnRect{0, 0, 0, 0};

    void layout(int screenW, int screenH);
    void typeIntoFocused();
    std::string* focusedString();
    bool tryConfirm();
};

} // namespace timberline_editor

#endif /* TIMBERLINE_SPEAK_CONVERSATION_CREATE_DIALOG_H */
