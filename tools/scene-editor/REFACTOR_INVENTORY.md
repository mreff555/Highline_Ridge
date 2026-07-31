# Scene editor refactor — inventory (Step 0)

Smoke checklist (run after each structural step):

1. Launch editor; resources resolve (`scenes.json` loads).
2. Switch JSON tabs (scenes / conversations if present).
3. Select a scene in the list; thumbnail appears when available.
4. Drag a scene card on the canvas; Save (Ctrl+S) persists layout.
5. Expand conversation tree; open a node in the variable editor; cancel/save.
6. Drag vertical and horizontal dividers; resize window (splits clamp).
7. Quit with dirty flag; autosave on exit still runs.

## `SceneEditorApp` member groups

| Group | Members (approx.) | Target module |
|-------|-------------------|---------------|
| Paths / shell | `resourceDir`, `assetRoot`, `loadError`, fonts | App shell |
| Document | `scenesDoc`, `conversationsRoot`, paths, tabs, dirty | DocumentWorkspace (later) |
| Conversation tree | tree, expand set, selection, visible rows, list bounds | ConversationTree (later) |
| Text metrics cache | `measureCache*`, `visualLinesCache*` | stays on app until VariableEditor |
| Scene selection / level | `selectedSceneId`, `canvasLevel` | Scene map (later) |
| **Layout** | `leftPaneWidth`, `topAreaHeight`, resize flags, screen size, divider drag | **EditorLayout (step 2)** |
| Canvas scroll / drag | scroll, bar drag, `dragSource`, stack dialog | SceneMapCanvas (later) |
| Variable / TTS editor | all `variableEditor*` / `editor*` / TTS theme | VariableEditor (later) |
| **Thumbnails** | `thumbnails` map | **ThumbnailCache (step 1)** |

## File map (after steps 0–6)

| Module | Files | Owns |
|--------|--------|------|
| Shell | `SceneEditorApp.*`, `main.cpp` | wiring, fonts, selection, frame loop |
| Document | `DocumentWorkspace.*` | scenes/conversations JSON, tabs, dirty |
| Layout | `EditorLayout.*` | pane splits, dividers |
| Thumbs | `ThumbnailCache.*` | scene thumbnails |
| Variables + TTS | `VariableEditor.*` | modal editor, measure cache, TTS sides |
| Conversation | `ConversationTree.*` | tree model/view |
| Graph | `SceneGraphModel.*` | exits, levels, auto-layout, stack dialog state |
| Map | `SceneMapCanvas.*` | canvas/list/chrome draw + interaction |

Legacy `SceneEditorApp_*.cpp` files are superseded (not in CMake).
