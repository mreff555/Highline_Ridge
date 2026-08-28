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

#ifndef TIMBERLINE_DIALOG_WALKTHROUGH_H
#define TIMBERLINE_DIALOG_WALKTHROUGH_H

#include "DocumentWorkspace.h"
#include "EditorTypes.h"

#include <functional>
#include <string>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

/** One editable spoken line in a scene's conversation tree (in walk order). */
struct DialogWalkStep
{
    enum class Field
    {
        Intro,
        ResumeIntro,
        Response,
        LineText
    };

    std::string sceneId;
    std::string objectPointer; // JSON pointer to phase / choice / line object
    Field field = Field::Response;
    std::string treeKey;       // matches ConversationTreeNode.key when available
    std::string breadcrumb;    // phase / player labels path
    std::string stepLabel;     // short label for list (Intro, response id, …)
    std::string playerLabel;   // player-facing choice text (if any)
    std::string objectId;      // phase or choice id
};

/**
 * Conversations-tab main pane: walk every dialog line for a scene and edit
 * on-screen text + TTS (enable, voice, audio path, spoken ttsText).
 */
struct DialogWalkthrough
{
    DocumentWorkspace* docs = nullptr;
    std::string* selectionSceneId = nullptr;
    std::string* conversationSelectedKey = nullptr;
    std::function<void()> onDirty;
    std::function<void()> onTreeRebuild;
    /** Called after the active conversation scene id changes. */
    std::function<void()> onSceneChanged;
    Font uiFont{};
    Font uiFontBold{};

    std::vector<DialogWalkStep> steps;
    int index = 0;
    bool dirtyStep = false;
    std::string status;
    std::string error;
    std::string lastBuiltScene;

    // Editable buffers for the current step.
    std::string textBuffer;
    std::string ttsTextBuffer;
    std::string ttsVoice;
    std::string ttsAudio;
    bool ttsEnabled = false;
    /** false = edit player-facing dialog; true = edit TTS spoken script. */
    bool editTtsText = false;
    bool textFieldFocused = true;
    int cursor = 0;
    /** Horizontal goal for up/down caret motion (-1 = recompute from current). */
    float preferredCaretX = -1.0f;
    float textScroll = 0.0f;
    float listScroll = 0.0f;
    bool voiceMenuOpen = false;
    int ignoreInputFrames = 0;

    Rectangle lastPane{0, 0, 0, 0};
    Rectangle textField{0, 0, 0, 0};
    Rectangle listPanel{0, 0, 0, 0};
    Rectangle voiceMenuRect{0, 0, 0, 0};
    Rectangle voiceBtnRect{0, 0, 0, 0};

    void rebuildSteps();
    void loadCurrentStep();
    bool applyCurrentStep();
    void goPrev();
    void goNext();
    void selectIndex(int i);
    /** Prefer a scene that actually has speakPhases when switching to this tab. */
    void ensureConversationSceneSelected();
    bool selectConversationScene(const std::string& sceneId);
    /** Jump to a tree key (choice:/narrative-conv:…). Returns true if found. */
    bool selectTreeKey(const std::string& treeKey);
    bool selectObjectField(const std::string& objectPointer, DialogWalkStep::Field field);

    void handleInput(Rectangle pane);
    void draw(Rectangle pane);

private:
    nlohmann::json* currentObject();
    const nlohmann::json* currentObject() const;
    std::vector<std::string> conversationSceneIds() const;
    static const char* fieldKey(DialogWalkStep::Field field);
    static const char* ttsFlagKey(DialogWalkStep::Field field);
    static const char* ttsVoiceKey(DialogWalkStep::Field field);
    static const char* ttsAudioKey(DialogWalkStep::Field field);
    static const char* ttsTextKey(DialogWalkStep::Field field);
    static const char* ttsShaKey(DialogWalkStep::Field field);
    void appendChoiceSteps(
        const nlohmann::json& choice,
        const std::string& objectPointer,
        const std::string& breadcrumb,
        const std::string& sceneId,
        int depth);
    void handleTextTyping();
    void ensureDefaultAudioPath();
    void drawVoiceMenu(Font font);
    bool handleVoiceMenuClick(Vector2 mouse);
    static int utf8Prev(const std::string& buffer, int cursor);
    static int utf8Next(const std::string& buffer, int cursor);
    void ensureCaretVisible(const std::vector<EditorVisualLine>& lines, float lineHeight);
};

} // namespace timberline_editor

#endif /* TIMBERLINE_DIALOG_WALKTHROUGH_H */
