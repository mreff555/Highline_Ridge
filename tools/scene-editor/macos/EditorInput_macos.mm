/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * macOS NSEvent monitors latch mouse press/release even when raylib's
 * previous/current state collapses press+release in one glfwPollEvents().
 ******************************************************************************/

#import <Cocoa/Cocoa.h>

#include <raylib.h>

extern "C" void editorInputStickyPress(int button);
extern "C" void editorInputStickyRelease(int button);

namespace
{

id gLocalMonitor = nil;
id gGlobalMonitor = nil;

int raylibButtonForNSEvent(NSEvent* event)
{
    // NSEvent buttonNumber: 0=left, 1=right, 2=middle — matches raylib/GLFW.
    const NSInteger n = event.buttonNumber;
    if (n < 0 || n > 6)
        return -1;
    return static_cast<int>(n);
}

void latchEvent(NSEvent* event)
{
    if (event == nil)
        return;
    const int button = raylibButtonForNSEvent(event);
    if (button < 0)
        return;

    switch (event.type)
    {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeRightMouseDown:
    case NSEventTypeOtherMouseDown:
        editorInputStickyPress(button);
        break;
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseUp:
        editorInputStickyRelease(button);
        break;
    default:
        break;
    }
}

} // namespace

extern "C" void editorInputInit(void)
{
    if (gLocalMonitor != nil)
        return;

    @autoreleasepool
    {
        const NSEventMask mask =
            NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp
            | NSEventMaskRightMouseDown | NSEventMaskRightMouseUp
            | NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp;

        // Local: events delivered to this app (normal path through GLFW/Cocoa).
        gLocalMonitor = [NSEvent
            addLocalMonitorForEventsMatchingMask:mask
            handler:^NSEvent*(NSEvent* event) {
                latchEvent(event);
                return event; // do not swallow — GLFW still needs the event
            }];

        // Global (this app only via accessibility-free addGlobal... requires trust).
        // Skip global — local is enough when the editor is focused.
        (void)gGlobalMonitor;

        TraceLog(LOG_INFO, "TIMBERLINE: macOS sticky mouse input monitors installed");
    }
}
