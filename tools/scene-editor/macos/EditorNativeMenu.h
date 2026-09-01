/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * macOS native menu hooks for the scene editor (Preferences… ⌘,).
 ******************************************************************************/

#ifndef TIMBERLINE_EDITOR_NATIVE_MENU_H
#define TIMBERLINE_EDITOR_NATIVE_MENU_H

#include <atomic>

#ifdef __cplusplus
extern "C" {
#endif

/** Install "Preferences…" (⌘,) into the application menu. Safe to call once after InitWindow. */
void editorInstallNativePreferencesMenu(void (*onPreferences)(void));

/** Optional: drain any deferred menu requests (no-op when using the callback). */
void editorPollNativeMenuFlags(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/** Set true when Preferences… is chosen; SceneEditorApp clears after opening the dialog. */
extern std::atomic<bool> gEditorPreferencesMenuRequested;
#endif

#endif
