/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "EditorAudio.h"

#include <raylib.h>

namespace timberline_editor
{
namespace
{
bool gAudioReady = false;
bool gAudioTried = false;
} // namespace

void editorEnsureAudioDevice()
{
    if (gAudioReady)
        return;
    if (IsAudioDeviceReady())
    {
        gAudioReady = true;
        gAudioTried = true;
        return;
    }
    // Only attempt once from a known-safe point (startup). Retrying every
    // preview frame during draw races CoreAudio on macOS Tahoe (#51 follow-up /
    // crash_3: InitAudioDevice → ma_on_output__coreaudio).
    if (gAudioTried)
        return;
    gAudioTried = true;
    InitAudioDevice();
    gAudioReady = IsAudioDeviceReady();
    if (!gAudioReady)
        TraceLog(LOG_WARNING, "TIMBERLINE: audio device not ready — SFX previews disabled");
}

bool editorAudioDeviceReady()
{
    if (gAudioReady)
        return true;
    if (IsAudioDeviceReady())
    {
        gAudioReady = true;
        return true;
    }
    return false;
}

void editorShutdownAudioDevice()
{
    if (IsAudioDeviceReady())
        CloseAudioDevice();
    gAudioReady = false;
    gAudioTried = false;
}

} // namespace timberline_editor
