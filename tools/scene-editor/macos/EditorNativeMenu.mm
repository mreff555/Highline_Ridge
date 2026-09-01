/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Minimal Cocoa Preferences menu for the raylib scene editor.
 ******************************************************************************/

#import "EditorNativeMenu.h"

#import <Cocoa/Cocoa.h>

#include <raylib.h>

std::atomic<bool> gEditorPreferencesMenuRequested{false};

namespace
{

void (*gOnPreferences)(void) = nullptr;

} // namespace

@interface EditorPreferencesMenuTarget : NSObject
- (void)openPreferences:(id)sender;
@end

@implementation EditorPreferencesMenuTarget
- (void)openPreferences:(id)sender
{
    (void)sender;
    gEditorPreferencesMenuRequested.store(true);
    if (gOnPreferences)
        gOnPreferences();
}
@end

extern "C" void editorInstallNativePreferencesMenu(void (*onPreferences)(void))
{
    gOnPreferences = onPreferences;

    @autoreleasepool
    {
        NSApplication* app = NSApp;
        if (app == nil)
            app = [NSApplication sharedApplication];
        if (app == nil)
            return;

        // Ensure raylib's Cocoa window is up so NSApp has a main menu.
        (void)GetWindowHandle();

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
            [mainMenu addItem:appMenuItem];
        }

        NSMenu* appMenu = [appMenuItem submenu];
        if (appMenu == nil)
        {
            appMenu = [[NSMenu alloc] initWithTitle:@"Timberline Resource Editor"];
            [appMenuItem setSubmenu:appMenu];
        }

        // Avoid duplicate Preferences items on re-install.
        for (NSMenuItem* existing in [appMenu itemArray])
        {
            if ([[existing title] isEqualToString:@"Preferences…"]
                || [[existing title] isEqualToString:@"Preferences..."])
                return;
        }

        static EditorPreferencesMenuTarget* target = nil;
        if (target == nil)
            target = [[EditorPreferencesMenuTarget alloc] init];

        NSMenuItem* prefs = [[NSMenuItem alloc]
            initWithTitle:@"Preferences…"
                   action:@selector(openPreferences:)
            keyEquivalent:@","];
        [prefs setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        [prefs setTarget:target];

        // Insert after About / before first separator when possible.
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
        if (insertAt == 0 || ![[appMenu itemAtIndex:0] isSeparatorItem])
        {
            // Keep a separator after Preferences when the menu was empty-ish.
            const NSInteger after = [appMenu indexOfItem:prefs] + 1;
            if (after >= [appMenu numberOfItems]
                || ![[appMenu itemAtIndex:after] isSeparatorItem])
                [appMenu insertItem:[NSMenuItem separatorItem] atIndex:after];
        }
    }
}

extern "C" void editorPollNativeMenuFlags(void)
{
    // Flag is read directly via gEditorPreferencesMenuRequested.
}
