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

#include "SceneEditorApp.h"
#include "EditorPaths.h"

#include <raylib.h>

#include <string>

using timberline_editor::SceneEditorApp;
using timberline_editor::ensureValidResourcePaths;

int main(int argc, char** argv)
{
    const int screenWidth = 1440;
    const int screenHeight = 900;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Timberline Resource Editor");
    SetTargetFPS(60);

    SceneEditorApp app;

    if (argc >= 2)
    {
        app.document.resourceDir = argv[1];
        app.document.assetRoot = (argc >= 3) ? argv[2] : "";
    }

    if (!ensureValidResourcePaths(app.document.resourceDir, app.document.assetRoot))
    {
        TraceLog(
            LOG_WARNING,
            "TIMBERLINE: scenes.json not found under resources (%s)",
            app.document.resourceDir.c_str());
    }
    else
    {
        TraceLog(
            LOG_INFO,
            "TIMBERLINE: using resources at %s",
            app.document.resourceDir.c_str());
    }

    app.loadUiFont();
    app.layout.init(GetScreenWidth(), GetScreenHeight());
    app.document.refreshTabs();
    app.loadActiveDocument();

    while (!WindowShouldClose())
    {
        app.update();
        app.draw();
    }

    app.unloadThumbnails();
    app.unloadUiFont();
    if (app.document.dirty)
        app.saveDocument();
    CloseWindow();
    return 0;
}