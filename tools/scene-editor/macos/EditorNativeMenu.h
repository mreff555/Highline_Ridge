/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * macOS native menu hooks for the scene editor (File → Save ⌘S, Preferences… ⌘,).
 ******************************************************************************/

#ifndef TIMBERLINE_EDITOR_NATIVE_MENU_H
#define TIMBERLINE_EDITOR_NATIVE_MENU_H

#include <atomic>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Install native menus after InitWindow:
 * - Application menu: Preferences… (⌘,)
 * - File menu (between app menu and Window): Save (⌘S)
 */
void editorInstallNativePreferencesMenu(void (*onPreferences)(void));

/** Optional: drain any deferred menu requests (no-op when using atomics). */
void editorPollNativeMenuFlags(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/** Set true when Preferences… is chosen; SceneEditorApp clears after opening. */
extern std::atomic<bool> gEditorPreferencesMenuRequested;
/** Set true when File → Save is chosen; SceneEditorApp clears after saving. */
extern std::atomic<bool> gEditorSaveMenuRequested;
#endif

#endif
