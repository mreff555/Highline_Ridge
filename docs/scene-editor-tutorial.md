# Scene editor tutorial

The **Timberline Resource Editor** (`scene-editor`) authors Highline Ridge content on disk under `resources/`.

Build it with a normal dev configure (`HIGHLINE_BUILD_EDITOR` defaults **ON**):

```bash
cmake -S . -B build && cmake --build build --target scene-editor -j
./build/scene-editor
```

> **Screenshots:** drop PNGs into `docs/images/` using the filenames below. Until then, each step lists what to capture.

---

## 1. Launch and layout

You get a **left list** (scenes / conversations / items depending on tab), a **main pane** (map or conversation walkthrough), and a **Variables** pane when a scene is selected.

**Screenshot:** `docs/images/editor-overview.png` — full window with Scenes tab, map, Variables.

---

## 2. Scenes tab — map basics

1. Select a scene in the list (or click its card on the map).  
2. Drag unplaced scenes from the list onto the map to assign layout.  
3. Use floor chrome to change level; connect Above/Below with the floor-connect flow.  
4. Right-click a card for Edit, Remove from map, Delete, etc.

**Gold wires** (mid-edge) = compass exits. **Silver wires** (corners) = Use transitions to another scene.

**Screenshot:** `docs/images/editor-map-wires.png` — map showing both wire types.

See also [scene-map-exits.md](scene-map-exits.md).

---

## 3. Edit Scene

Right-click → **Edit…** (or equivalent) opens **Scene Authoring**:

- Id / title fields as applicable  
- Description & Examine (multiline)  
- TTS enable + TTS Description / Examine when TTS is on  
- Default `useExit` string (target scene or `scene#sub`)  
- Image / audio path fields  

**Accept** writes `scenes.json`.  

**Important:** Use **narrative** (`useDetails`) and **repeat** for map Use exits are **not** edited here — use **Manage Use Transition** (and Effects for status). Edit Scene **preserves** those fields so they are not wiped on save.

**Alternate / focus views** (`parent#sub`, e.g. snow cave `chamber` / `toward_exit`): TTS description and examine bags live on the **sub-scene**. Edit the alternate map node (not only the parent room) to see TTS on and the spoken text. Saving writes `subScenes[sub].descriptionTts` / `examineTts` and keeps existing audio paths.

**Fullscreen writing desk:** right-click a description / examine / TTS multiline field (Edit Scene) or the Conversations text pane → **Edit full screen**. A parchment-on-desk overlay fills the editor; **Confirm** writes back, **Cancel** discards. TTS fields keep syntax coloring.

**Screenshot:** `docs/images/editor-edit-scene.png` — Edit Scene with TTS section visible.

---

## 4. Variables pane

With a scene selected:

| Button | Opens |
|--------|--------|
| **Effects** | Stat deltas / repeat for scene Use |
| **Events** | `storyEvents[]` (enter / exit / examine beats) |
| **Inventory** | Starting inventory for the scene |
| **AI Assist** | Generation assist (when configured) |

Tree rows under the scene expose JSON fields (including TTS bags) for raw edit when needed.

**Screenshot:** `docs/images/editor-variables-events.png` — Variables pane with Events dialog open.

### Story Events (quick)

1. **Events** → Add  
2. Set `when` (`enter` / `exit` / `examine`), direction if exit  
3. Flag CSVs (`requiresFlags`, `unlessFlags`, `setsFlags`, …)  
4. Narrative (+ optional block movement / refresh takeables)  
5. **Save**

Used for ice-house badge glint, vestry greeting, cottonwood departure, etc.

---

## 5. Use transitions

1. Drag a **Use corner** on the source card to a corner on the destination (or open **Manage Use Transition** from the wire menu).  
2. **Create new** if no binding exists (Direct `useExit`, or a `use_map_*` interaction if Direct is taken).  
3. Edit **Use description** (player-facing “Using:” text).  
4. Confirm **Repeatable** in the meta line (map exits should stay repeatable).  
5. **Accept** → save the document (Ctrl/Cmd+S).

**Screenshot:** `docs/images/editor-use-transition.png` — Manage Use Transition with description focused.

---

## 6. Conversations tab

1. Switch to **Conversations**.  
2. Tree: scene → speak phases / actors → choices.  
3. Main pane: **Dialog walkthrough** — edit response / TTS text, shift-select, copy/paste.  
4. TTS highlighting shows pauses, voices, and errors.  

**Delete** while editing text deletes characters — it does **not** delete the scene.

**Screenshot:** `docs/images/editor-conversations.png` — walkthrough with TTS-colored text.

After changing spoken lines, refresh audio with a **dev** game binary ([tts.md](tts.md)), then rebuild release to embed.

---

## 7. Items tab

Edit item definitions, combinations, and TTS where enabled. Prefer the item authoring dialogs over hand-editing `items.json` unless you know the schema.

**Screenshot:** `docs/images/editor-items.png` — optional.

---

## 8. Save and playtest

1. **Ctrl/Cmd+S** (or Save control) so `resources/*.json` are on disk.  
2. Run `./build/Highline\ Ridge` from the build tree (resources are synced beside the binary).  
3. For release packaging after TTS/art changes: `./build-release.sh`.

---

## Screenshot checklist

| File | Capture |
|------|---------|
| `docs/images/editor-overview.png` | Full editor, Scenes + map |
| `docs/images/editor-map-wires.png` | Gold + silver wires |
| `docs/images/editor-edit-scene.png` | Edit Scene + TTS |
| `docs/images/editor-variables-events.png` | Story Events dialog |
| `docs/images/editor-use-transition.png` | Use description field |
| `docs/images/editor-conversations.png` | Dialog walkthrough |
| `docs/images/editor-items.png` | Items (optional) |

Once images exist, add them under each section as:

```markdown
![Editor overview](images/editor-overview.png)
```
