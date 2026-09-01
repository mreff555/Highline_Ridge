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

#ifndef TIMBERLINE_EDITOR_BUTTON_H
#define TIMBERLINE_EDITOR_BUTTON_H

#include <raylib.h>

#include <string>
#include <vector>

namespace timberline_editor
{

/** Metrics/skins from resources/editor_ui_config.json (with safe defaults). */
struct EditorButtonConfig
{
    float minWidth = 72.0f;
    float maxWidth = 280.0f;
    float minHeight = 28.0f;
    float maxHeight = 64.0f;
    float padX = 12.0f;
    float padY = 8.0f;
    float fontSize = 16.0f;
    float lineSpacing = 3.0f;
    bool wordWrap = true;
    int sliceLeft = 10;
    int sliceTop = 10;
    int sliceRight = 10;
    int sliceBottom = 10;
    std::string raisedSkinPath = "resources/ui/editor/button_raised.png";
    std::string depressedSkinPath = "resources/ui/editor/button_depressed.png";
    float labelOffsetPressedX = 1.0f;
    float labelOffsetPressedY = 1.0f;
};

/**
 * Editor UI button: sizes within min/max, optional wrap, raised/depressed skins.
 * Layout guide = preferred rect (slot). Final bounds are computed from label.
 */
struct EditorButton
{
    Rectangle preferred{0, 0, 100, 32};
    std::string label;
    bool enabled = true;
    bool accent = true;
    /** When true, layout width is fixed to preferred.width (clamped). */
    bool expandWidth = false;

    // Interaction (updated each frame while visible).
    bool hovered = false;
    bool pressed = false;
    /** True once on mouse release over the button after a press that started on it. */
    bool clicked = false;

    Rectangle bounds{0, 0, 0, 0};

    void layout(Font font, const EditorButtonConfig& config);
    /**
     * Hit-test / press state. Returns true the frame the mouse is pressed
     * over the button (uses editorMousePressed sticky edges — safe for brief
     * trackpad taps and stack-local buttons recreated each frame).
     * Call after layout(); uses preferred for hit if bounds empty.
     */
    bool update(const EditorButtonConfig& config);
    void draw(Font font, const EditorButtonConfig& config) const;
};

/** Working-overlay spinner (from editor_ui_config.json workingOverlay). */
struct EditorWorkingOverlayConfig
{
    /** Rigid: texture width/height must equal sizePx (see config comments). */
    int sizePx = 64;
    float revolutionsPerSecond = 1.0f;
    std::string spinnerPath = "resources/ui/editor_working_spinner.png";
    std::string title = "Working - Please wait";
};

/** Global button config + skins for the editor session. */
struct EditorButtonResources
{
    EditorButtonConfig config{};
    /** Multiline / caret fields (from editor_ui_config.json textFields). */
    float caretBlinkHz = 1.5f;
    float textFieldPadX = 8.0f;
    float textFieldPadY = 6.0f;
    float textFieldScrollGutter = 12.0f;
    EditorWorkingOverlayConfig working{};
    Texture2D raised{};
    Texture2D depressed{};
    Texture2D workingSpinner{};
    bool raisedLoaded = false;
    bool depressedLoaded = false;
    bool workingSpinnerLoaded = false;
    bool configLoaded = false;

    void load(const std::string& resourceDir, const std::string& assetRoot);
    void unload();
};

/** Shared instance wired by SceneEditorApp. */
EditorButtonResources& editorButtons();

/**
 * Persist current button/textField/workingOverlay metrics to
 * resources/editor_ui_config.json (under resourceDir).
 */
bool saveEditorUiConfig(const std::string& resourceDir, const EditorButtonResources& res);

/**
 * Drop-in skinned button draw into fixed bounds. Press visual follows the mouse;
 * does not consume clicks — callers keep their own hit-testing on `bounds`.
 * Primary free-function entry point for all fixed-slot editor action buttons.
 */
void drawEditorButton(
    Font font,
    Rectangle bounds,
    const char* label,
    bool accent,
    bool enabled);

} // namespace timberline_editor

#endif /* TIMBERLINE_EDITOR_BUTTON_H */
