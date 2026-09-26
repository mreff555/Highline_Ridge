# Scene map exits (compass vs Use)

The Timberline Resource Editor map shows two kinds of connectors between scene cards. They look similar but mean different things in play.

## Compass exits (gold)

Mid-edge ports (**F / B / L / R**, plus floor **up / down** via Connect) write `exits` + `movement` on the scene. In game these are the directional movement buttons.

## Use transitions (silver)

Corner ports (**NW / NE / SW / SE**) author **Use-driven scene changes**:

| Binding | JSON | In-game Use behavior |
|---------|------|----------------------|
| **Direct Use** | Scene `useExit` (+ optional `useDetails`, `useRepeatStatus`, map corner fields) | If the scene has **no** available `interactions[]`, Use runs immediately (no picker). |
| **Interaction Use** | `interactions[]` entry with `exitSceneId` (map stubs are often `use_map_N`) | Use opens the interaction picker when any such interactions are available. |

Drag corner → corner (or **Manage Use Transition**) to create/edit these. The dialog’s **Use description** box is the narrative shown when the player clicks Use (`useDetails`). **Accept** saves that description and the destination; **Cancel** closes without changing the destination.

**Note:** Edit Scene does **not** author Use description / repeat — those live on the Use Transition dialog (and Effects for repeat status). Saving Edit Scene preserves them.

### Exit requirements (gated compass exits)

Right-click a **gold** same-floor exit wire → **Edit Transition Audio...** (SFX) or **Exit Requirements...** (gates / blocked VO).

**Floor (up/down) links** do not draw a wire — the destination card lives on another map level. Use the stair badge on the card corner instead (`^N` / `vN`, gold-tinted when gated). **Right-click the badge** for the same menu: Delete / Edit Transition Audio… / Exit Requirements…. The direction slider still flips outbound vs return.

Right-click a **silver** Use wire → **Manage...** (Use description / destination) or **Edit Transition Audio...** (same enter/exit SFX dialog).

Requirements are **one-way** (`fromScene.exitRequirements[direction]`). The Exit Requirements dialog has a **direction slider**: left shows e.g. `snow_cave_exterior -> snow_cave_interior`, right shows the return path. Gates, badge, blocked details, and blocked TTS are unique per side. Switching sides auto-saves the side you leave.

- **Exit requires a light source** / **room purchased today** — row toggles under the direction slider  
- **Inventory item id(s):** full-width field with **autocomplete** (Tab accepts); comma-separated if *all* are required  
- **Story flag:** separate full-width field (unlock-once pattern)  

- **Block badge:** `auto` / `light` / `lock` / `gear`  
- **Blocked TTS:** Voice · **Generate TTS dialog** · **Generate Voice** · **Preview voice**
- **Blocked variants:** optional list of `{ when, details, tts }` bags. **First matching `when` wins**; leave `when` empty for the default branch and place it **last**.

### `when` condition grammar (P3)

Bare form (preferred in JSON / variant `when` field):

| Example | Meaning |
|---------|---------|
| `item:padlock_key:in_inventory` | Player has item |
| `item:padlock_key:discovered` | Taken/discovered (or in inventory) |
| `not_item:padlock_key:in_inventory` | Negation (`not_` prefix works on object or state) |
| `scene:saloon_service_hall:examined` | Scene examined this playthrough |
| `scene:x:visited` | Entered / examined (best-effort) |
| `flag:some_flag:set` | Story flag present |
| `milestone:quest_id:set` | Milestone started or complete |
| `actor:bartender:observed` | Actor known (`spoken_to` / `attacked` via `actor:<id>:<state>` flags) |

Brace sugar `{{condition:item:padlock_key:in_inventory}}` is accepted as the same clause. **Do not** put `{{condition}}` inside TTS bake text — use separate variant TTS bags. Editor highlights condition tags (yellow) and body (gray).

Runtime: clicking a gated MOVE button shows/plays the blocked copy without moving (see #42).

### What gets a silver Use wire on the map

**Only Use actions that leave for another scene** are drawn as map wires:

- Scene `useExit` pointing at another scene / `scene#sub`
- An interaction with a non-empty `exitSceneId`

**Same-room Use actions do not get a map wire.** They still appear in the in-game Use list when authored as `interactions[]` with narrative (and no exit), for example cabin **Sit in the luxurious chair** (`sit_chair`): it has `useDetails` but no `exitSceneId`, so there is nothing to connect on the map. Cabin **Lift the rug** (`lift_rug`) has `exitSceneId: cabin_under_rug`, so it **does** show a Use line.

Missing art is unrelated: a transition Use still draws a wire to the destination card even if that scene still uses a placeholder image.

### Alternate views vs Use wires (#40)

Keep **alternate / sub-scene cards** (`parent#sub`) for focus plates and per-view TTS (e.g. snow cave `chamber`). **Use wires** are for room-to-room Use destinations (doors, nightstand focus scene). Prefer Use when the player moves to another authored scene; prefer an alternate card when it is the same scene with a different image/description/TTS bag.

### Place item on Use (Variables → Interactions)

Same-room “search the nightstand” style finds use **`interactions[]`** with `grantItem` (not Inventory takeables). Variables pane → **Interactions** → **Add place item…** pick from `items.json`. Defaults: requires examine, unlock-once `useFlag` / `hideWhenStoryFlag` (`sceneId:itemId_taken`). Player path: **Examine → Use** (not Take). Map Use stubs with `exitSceneId` also appear in this list.

Inventory (Variables → **Inventory**) remains for ground **Take** loot (`takeables[]`).

### Quick checklist

| Authoring goal | Map wire? | Player UI |
|----------------|-----------|-----------|
| Walk F/B/L/R (or floor) to another room | Gold compass / floor link | Movement buttons |
| Use to enter another scene (door, rug, sit-at-desk focus, etc.) | Silver Use corner wire | Use (direct or picker) |
| Use that only plays narrative / status in the same room | **No** wire | Use picker (interaction list) |
| Find item via Use (nightstand / search) | **No** wire | Variables → Interactions → grantItem |
| Find item via Take after examine | **No** wire | Variables → Inventory |

See also Manage Use Transition help text in the editor for **Create new / Clear / Accept / Cancel**.
