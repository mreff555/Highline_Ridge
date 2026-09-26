/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Cocoa application + File menus for the raylib scene editor.
 ******************************************************************************/

#import "EditorNativeMenu.h"

#import <Cocoa/Cocoa.h>

#include <raylib.h>

std::atomic<bool> gEditorPreferencesMenuRequested{false};
std::atomic<bool> gEditorSaveMenuRequested{false};

namespace
{
void (*gOnPreferences)(void) = nullptr;
} // namespace

@interface EditorNativeMenuTarget : NSObject
- (void)openPreferences:(id)sender;
- (void)saveDocument:(id)sender;
@end

@implementation EditorNativeMenuTarget
- (void)openPreferences:(id)sender
{
    (void)sender;
    gEditorPreferencesMenuRequested.store(true);
    if (gOnPreferences)
        gOnPreferences();
}
- (void)saveDocument:(id)sender
{
    (void)sender;
    gEditorSaveMenuRequested.store(true);
}
@end

namespace
{

/**
 * macOS Tahoe (26) can SIGSEGV in CoreUI while AppKit injects SF Symbol icons
 * into the Windows menu ("Move & Resize by Halves") during key-equivalent
 * routing from glfwPollEvents. Rebuild a plain Window menu we own so AppKit
 * does not run the broken sidecar updater against a nil CUICatalog.
 */
void ensurePlainWindowsMenu(NSApplication* app, NSMenu* mainMenu)
{
    if (app == nil || mainMenu == nil)
        return;

    NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu addItemWithTitle:@"Minimize"
                          action:@selector(performMiniaturize:)
                   keyEquivalent:@"m"];
    [windowMenu addItemWithTitle:@"Zoom"
                          action:@selector(performZoom:)
                   keyEquivalent:@""];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    [windowMenu addItemWithTitle:@"Bring All to Front"
                          action:@selector(arrangeInFront:)
                   keyEquivalent:@""];

    for (NSInteger i = [mainMenu numberOfItems] - 1; i >= 0; --i)
    {
        NSMenuItem* item = [mainMenu itemAtIndex:i];
        if ([[item title] isEqualToString:@"Window"] || [app windowsMenu] == [item submenu])
            [mainMenu removeItemAtIndex:i];
    }

    NSMenuItem* windowMenuItem =
        [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    [windowMenuItem setSubmenu:windowMenu];
    [mainMenu addItem:windowMenuItem];
    [app setWindowsMenu:windowMenu];
}

void installMenusNow(void)
{
    @autoreleasepool
    {
        NSApplication* app = NSApp;
        if (app == nil)
            app = [NSApplication sharedApplication];
        if (app == nil)
            return;

        (void)GetWindowHandle();

        // Follow system light/dark (do NOT force Aqua — that made the title bar
        // light while the OS was in dark mode, #49). nil appearance = system.
        [app setAppearance:nil];

        NSMenu* mainMenu = [app mainMenu];
        if (mainMenu == nil)
        {
            mainMenu = [[NSMenu alloc] initWithTitle:@""];
            [app setMainMenu:mainMenu];
        }

        NSMenuItem* appMenuItem = nil;
        if ([mainMenu numberOfItems] > 0)
            appMenuItem = [mainMenu itemAtIndex:0];
        if (appMenuItem == nil)
        {
            appMenuItem = [[NSMenuItem alloc] init];
            [mainMenu insertItem:appMenuItem atIndex:0];
        }

        NSMenu* appMenu = [appMenuItem submenu];
        if (appMenu == nil)
        {
            appMenu = [[NSMenu alloc] initWithTitle:@"Timberline Resource Editor"];
            [appMenuItem setSubmenu:appMenu];
        }

        static EditorNativeMenuTarget* target = nil;
        if (target == nil)
            target = [[EditorNativeMenuTarget alloc] init];

        bool hasPrefs = false;
        for (NSMenuItem* existing in [appMenu itemArray])
        {
            if ([[existing title] isEqualToString:@"Preferences…"]
                || [[existing title] isEqualToString:@"Preferences..."])
            {
                hasPrefs = true;
                break;
            }
        }
        if (!hasPrefs)
        {
            NSMenuItem* prefs = [[NSMenuItem alloc]
                initWithTitle:@"Preferences…"
                       action:@selector(openPreferences:)
                keyEquivalent:@","];
            [prefs setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
            [prefs setTarget:target];

            NSInteger insertAt = 0;
            for (NSInteger i = 0; i < [appMenu numberOfItems]; ++i)
            {
                if ([[appMenu itemAtIndex:i] isSeparatorItem])
                {
                    insertAt = i;
                    break;
                }
                insertAt = i + 1;
            }
            [appMenu insertItem:prefs atIndex:insertAt];
            const NSInteger after = [appMenu indexOfItem:prefs] + 1;
            if (after >= [appMenu numberOfItems]
                || ![[appMenu itemAtIndex:after] isSeparatorItem])
                [appMenu insertItem:[NSMenuItem separatorItem] atIndex:after];
        }

        bool hasFileMenu = false;
        for (NSMenuItem* item in [mainMenu itemArray])
        {
            if ([[item title] isEqualToString:@"File"])
            {
                hasFileMenu = true;
                NSMenu* fileMenu = [item submenu];
                bool hasSave = false;
                for (NSMenuItem* sub in [fileMenu itemArray])
                {
                    if ([[sub title] isEqualToString:@"Save"])
                    {
                        hasSave = true;
                        break;
                    }
                }
                if (!hasSave && fileMenu != nil)
                {
                    NSMenuItem* save = [[NSMenuItem alloc]
                        initWithTitle:@"Save"
                               action:@selector(saveDocument:)
                        keyEquivalent:@"s"];
                    [save setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
                    [save setTarget:target];
                    [fileMenu addItem:save];
                }
                break;
            }
        }

        if (!hasFileMenu)
        {
            NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
            NSMenuItem* fileMenuItem =
                [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
            [fileMenuItem setSubmenu:fileMenu];

            NSMenuItem* save = [[NSMenuItem alloc]
                initWithTitle:@"Save"
                       action:@selector(saveDocument:)
                keyEquivalent:@"s"];
            [save setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
            [save setTarget:target];
            [fileMenu addItem:save];

            NSInteger insertAt = 1;
            for (NSInteger i = 1; i < [mainMenu numberOfItems]; ++i)
            {
                NSString* title = [[mainMenu itemAtIndex:i] title];
                if ([title isEqualToString:@"Window"] || [title isEqualToString:@"Help"]
                    || [title isEqualToString:@"Edit"] || [title isEqualToString:@"View"])
                {
                    insertAt = i;
                    break;
                }
                insertAt = i + 1;
            }
            if (insertAt > [mainMenu numberOfItems])
                insertAt = [mainMenu numberOfItems];
            [mainMenu insertItem:fileMenuItem atIndex:insertAt];
        }

        ensurePlainWindowsMenu(app, mainMenu);
    }
}

} // namespace

extern "C" void editorInstallNativePreferencesMenu(void (*onPreferences)(void))
{
    gOnPreferences = onPreferences;

    // Defer until after GLFW finishes its first Cocoa menu/window setup. Installing
    // synchronously can leave AppKit's Windows-menu sidecar updater holding a bad
    // CUICatalog on macOS Tahoe (SIGSEGV in EndDrawing → glfwPollEvents → NSMenu).
    dispatch_async(dispatch_get_main_queue(), ^{
        installMenusNow();
    });
}

extern "C" void editorPollNativeMenuFlags(void)
{
    // Flags are read directly via the atomics.
}
