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

#ifndef TIMBERLINE_VARIABLE_EDITOR_H
#define TIMBERLINE_VARIABLE_EDITOR_H

#include "DocumentWorkspace.h"
#include "EditorTypes.h"

#include <nlohmann/json.hpp>
#include <raylib.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace timberline_editor
{

struct VariableEditor
{
    bool open = false;
    ConversationEditDoc docTarget = ConversationEditDoc::None;
    std::string jsonPointer;
    std::string editorSceneId;
    /** When docTarget == Items, the item id being edited. */
    std::string editorItemId;
    std::string editorKey;
    std::string buffer;
    bool textTtsEnabled = false;
    bool showTts = false;
    std::string textSideBuffer;
    std::string ttsSideBuffer;
    std::string globalDefaultVoice = "leo";
    bool globalDefaultVoiceLoaded = false;

    std::string ttsHighlightCacheSource;
    std::vector<Color> ttsHighlightColors;

    // Scene Default TTS voice dropdown (OFF + builtin actors).
    bool voiceDropdownOpen = false;
    Rectangle voiceDropdownBtn{0, 0, 0, 0};
    Rectangle voiceDropdownMenu{0, 0, 0, 0};

    enum class TextTtsPairMode
    {
        None,
        StringWithSiblingTtsText,
        StringWithTtsObject,
        ObjectSplit,
        /** JSON value is a pure TTS bag (wakeTts / descriptionTts / …); sides edit ttsText. */
        TtsBagSpokenText
    };
    TextTtsPairMode textTtsMode = TextTtsPairMode::None;
    std::string ttsObjectKey;

    enum VariableValueKind
    {
        VariableKindString,
        VariableKindBool,
        VariableKindInteger,
        VariableKindFloat,
        VariableKindJson
    };
    VariableValueKind kind = VariableKindString;
    bool multiline = false;
    int cursor = 0;
    int selectAnchor = -1;
    bool mouseSelecting = false;
    double lastClickTime = -1.0;
    int lastClickPos = -1;
    float scrollY = 0.0f;
    std::string selectedVariableKey;
    std::string error;
    int ignoreInputFrames = 0;
    Rectangle fieldRect{0, 0, 0, 0};
    Rectangle saveBtn{0, 0, 0, 0};
    Rectangle cancelBtn{0, 0, 0, 0};
    Rectangle textTtsToggle{0, 0, 0, 0};
    float fontSize = 16.0f;
    float lineHeight = 20.0f;
    float pad = 8.0f;
    float preferX = 0.0f;
    float keyRepeatTimer = 0.0f;
    int keyRepeatKey = 0;

    mutable float measureCacheFontSize = -1.0f;
    mutable float measureCacheAscii[128]{};
    mutable bool measureCacheAsciiReady = false;
    mutable std::map<std::string, float> measureCacheStrings;
    mutable std::string visualLinesCacheBuffer;
    mutable float visualLinesCacheMaxW = -1.0f;
    mutable float visualLinesCacheFontSize = -1.0f;
    mutable std::vector<EditorVisualLine> visualLinesCache;

    DocumentWorkspace* docs = nullptr;
    std::string* selectionSceneId = nullptr; // app-selected scene for variables pane
    float* variablesScroll = nullptr;
    bool* stackDialogOpen = nullptr;
    Font uiFont{};
    Font uiFontBold{};
    std::function<void()> onTreeRebuild;
    /** Open Scene AI Assist for the selected scene (scenes tab). */
    std::function<void()> onAiAssist;
    /** Open Scene Inventory editor for the selected scene. */
    std::function<void()> onSceneInventory;
    /** Open Scene Effects (stat deltas) editor for the selected scene. */
    std::function<void()> onSceneEffects;



void closeVariableEditor();

static bool splitJsonPointer(
    const std::string& pointer,
    std::string& parentOut,
    std::string& leafOut);

nlohmann::json* resolveEditorParentObject(const std::string& parentPointer);

void syncActiveBufferFromSide();

void stashActiveBufferToSide();

bool variableHasSelection() const;

void variableSelectionRange(int& outStart, int& outEnd) const;

void clearVariableSelection();

bool deleteVariableSelection();

void setVariableCursor(int pos, bool extendSelection);

static bool editorIsWordChar(unsigned char ch);

void selectWordAtCursor(int pos);

void openVariableEditor(const std::string& sceneId, const std::string& key);

bool applyEditorBufferToJson(nlohmann::json& value);

bool saveVariableEditor();

void drawEditorLineText(
    const EditorVisualLine& line,
    float x,
    float y,
    float fontSize,
    bool highlightTts) const;

void clampVariableCursor();

int utf8PrevIndex(int cursor) const;

int utf8NextIndex(int cursor) const;

const std::vector<EditorVisualLine>& buildEditorVisualLines(float maxTextWidth, float fontSize) const;

int editorLineIndexForCursor(const std::vector<EditorVisualLine>& lines, int cursor) const;

float editorCaretXOnLine(
    const EditorVisualLine& line,
    int cursor,
    float fontSize) const;

int editorCursorFromClick(
    const std::vector<EditorVisualLine>& lines,
    Rectangle field,
    float pad,
    float fontSize,
    float lineHeight,
    Vector2 mouse) const;

void ensureCursorVisible(
    const std::vector<EditorVisualLine>& lines,
    float fieldHeight,
    float pad,
    float lineHeight);

bool editorNavKeyTriggered(int key);

int cursorOnLineAtPreferX(const EditorVisualLine& line, float preferX, float fontSize) const;

void moveVariableCursorVertical(int direction, const std::vector<EditorVisualLine>& lines, float fontSize);

void syncDialogLayout(int screenWidth, int screenHeight);
    void handleVariableEditorTextInput();

void drawVariableEditor(int screenWidth, int screenHeight);

void drawVariablesPane(Rectangle paneBounds);

static bool isTtsJsonKey(const std::string& key);

static nlohmann::json stripTtsKeys(const nlohmann::json& object);

static nlohmann::json onlyTtsKeys(const nlohmann::json& object);

std::string narrativeTtsObjectKey(const std::string& fieldLeaf) const;

void ensureGlobalDefaultVoiceLoaded();

void ensureTtsSyntaxThemeLoaded();

void rebuildTtsHighlightColors();

Color ttsColorAtBufferIndex(int index) const;

static bool colorsEqual(Color a, Color b);

std::string readTtsVoiceFromObject(const nlohmann::json& object) const;

std::string editorDefaultVoiceLabel() const;

/** Scene or item TTS policy label for the dropdown (OFF or voice id). */
std::string ownerTtsPolicyDropdownLabel() const;

/** Apply OFF or a builtin voice id to the scene/item ttsEnabled/ttsDefaultVoice. */
bool applyOwnerTtsPolicySelection(const std::string& selection);



void toggleTextTtsSide();

void setupTextTtsForOpenedValue(const nlohmann::json& value);

bool applyTextTtsSidesToDocument();

static std::string unescapeJsonPointerToken(const std::string& token);

float measureUiTextWidth(const std::string& text, float fontSize) const;

float measureUiCharWidth(unsigned char ch, float fontSize) const;
    static std::string truncateForTree(const std::string& text, size_t maxLen);
    std::string truncate(const std::string& text, size_t maxLen) const;
};

} // namespace timberline_editor

#endif
