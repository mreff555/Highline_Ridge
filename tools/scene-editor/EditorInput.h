/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Sticky mouse edges for the scene editor.
 *
 * raylib's previous/current button state collapses when press+release both
 * arrive in one glfwPollEvents() flush (common with macOS trackpad taps).
 * IsMouseButtonPressed then never fires unless the button is held across a
 * poll. This layer latches native edges and exposes stable per-frame queries.
 ******************************************************************************/

#ifndef TIMBERLINE_EDITOR_INPUT_H
#define TIMBERLINE_EDITOR_INPUT_H

#include <raylib.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Install platform hooks (macOS NSEvent monitors). Call once after InitWindow. */
void editorInputInit(void);

/**
 * Snapshot sticky edges + raylib state for this frame.
 * Call once at the start of each main-loop iteration, before update/draw.
 */
void editorInputBeginFrame(void);

/** True if the button was pressed this frame (raylib edge or sticky latch). */
bool editorMousePressed(int button);

/** True if the button was released this frame (raylib edge or sticky latch). */
bool editorMouseReleased(int button);

/**
 * True while held, or on the press frame even if press+release collapsed in
 * one poll (so brief taps still light depressed skins / clear wait-release).
 */
bool editorMouseDown(int button);

#ifdef __cplusplus
}
#endif

#endif /* TIMBERLINE_EDITOR_INPUT_H */
