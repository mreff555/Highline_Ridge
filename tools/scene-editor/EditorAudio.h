/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * One-shot audio device init for the scene editor (never from draw).
 ******************************************************************************/

#ifndef TIMBERLINE_EDITOR_AUDIO_H
#define TIMBERLINE_EDITOR_AUDIO_H

namespace timberline_editor
{

/** Init raylib/miniaudio once after InitWindow. Safe to call repeatedly. */
void editorEnsureAudioDevice();

/** True after a successful InitAudioDevice. */
bool editorAudioDeviceReady();

/** Close before CloseWindow. */
void editorShutdownAudioDevice();

} // namespace timberline_editor

#endif
