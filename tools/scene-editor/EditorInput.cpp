/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Portable sticky mouse frame state. Platform latches live in EditorInput_macos.mm.
 ******************************************************************************/

#include "EditorInput.h"

#include <atomic>
#include <cstdint>

namespace
{

constexpr int kMaxButtons = 7;

std::atomic<uint8_t> gStickyPressed[kMaxButtons]{};
std::atomic<uint8_t> gStickyReleased[kMaxButtons]{};

bool gFramePressed[kMaxButtons]{};
bool gFrameReleased[kMaxButtons]{};
bool gFrameDown[kMaxButtons]{};
bool gFrameReady = false;

int clampButton(int button)
{
    if (button < 0 || button >= kMaxButtons)
        return -1;
    return button;
}

} // namespace

// macOS (and future platforms) write these from native callbacks.
extern "C" void editorInputStickyPress(int button)
{
    const int b = clampButton(button);
    if (b < 0)
        return;
    gStickyPressed[b].store(1, std::memory_order_relaxed);
}

extern "C" void editorInputStickyRelease(int button)
{
    const int b = clampButton(button);
    if (b < 0)
        return;
    gStickyReleased[b].store(1, std::memory_order_relaxed);
}

#if !defined(__APPLE__)
void editorInputInit(void)
{
    // No native latch on this platform; raylib edges only.
}
#endif

void editorInputBeginFrame(void)
{
    for (int i = 0; i < kMaxButtons; ++i)
    {
        const bool stickyPress =
            gStickyPressed[i].exchange(0, std::memory_order_relaxed) != 0;
        const bool stickyRelease =
            gStickyReleased[i].exchange(0, std::memory_order_relaxed) != 0;

        const bool rlPress = IsMouseButtonPressed(i);
        const bool rlRelease = IsMouseButtonReleased(i);
        const bool rlDown = IsMouseButtonDown(i);

        gFramePressed[i] = rlPress || stickyPress;
        gFrameReleased[i] = rlRelease || stickyRelease;
        // Brief tap that collapsed in one poll: still treat as down this frame
        // so waitMouseRelease / depressed skins behave.
        gFrameDown[i] = rlDown || gFramePressed[i];
    }
    gFrameReady = true;
}

bool editorMousePressed(int button)
{
    const int b = clampButton(button);
    if (b < 0)
        return false;
    if (!gFrameReady)
        return IsMouseButtonPressed(button);
    return gFramePressed[b];
}

bool editorMouseReleased(int button)
{
    const int b = clampButton(button);
    if (b < 0)
        return false;
    if (!gFrameReady)
        return IsMouseButtonReleased(button);
    return gFrameReleased[b];
}

bool editorMouseDown(int button)
{
    const int b = clampButton(button);
    if (b < 0)
        return false;
    if (!gFrameReady)
        return IsMouseButtonDown(button);
    return gFrameDown[b];
}
