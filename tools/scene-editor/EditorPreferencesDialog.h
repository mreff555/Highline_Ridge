/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Editor Preferences: generation style filter + editor_ui_config.json metrics.
 ******************************************************************************/

#ifndef TIMBERLINE_EDITOR_PREFERENCES_DIALOG_H
#define TIMBERLINE_EDITOR_PREFERENCES_DIALOG_H

#include "EditorButton.h"

#include <string>

#include <raylib.h>

namespace timberline_editor
{

struct EditorPreferencesDialog
{
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;
    float scrollY = 0.0f;
    float lastContentHeight = 0.0f;

    std::string resourceDir;
    std::string assetRoot;

    // Editable snapshot (applied on Save).
    std::string styleFilter;
    EditorWorkingOverlayConfig working{};
    float caretBlinkHz = 1.5f;
    float textFieldPadX = 8.0f;
    float textFieldPadY = 6.0f;
    float textFieldScrollGutter = 12.0f;
    EditorButtonConfig buttons{};
    /** Map edge auto-pan while dragging scenes (px/s). 0 = off. */
    float mapDragPanSpeed = 320.0f;

    std::string status;
    std::string error;

    // Focus: 0 = style multiline; 1+ = single-line fields (see cpp).
    int focusField = -1;
    int styleCaret = 0;
    /** Draft text for the currently focused single-line / numeric field. */
    std::string focusDraft;

    void openDialog(const std::string& resourceDirIn, const std::string& assetRootIn);
    void closeDialog();
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    void snapshotFromLive();
    bool applyAndSave();
    void typeIntoFocusedField();
    void setFocus(int fieldId);
    void commitFocusDraft();
    std::string draftForField(int fieldId) const;
};

} // namespace timberline_editor

#endif
