# Scene map exits, floors, and gates

The Timberline Resource Editor map shows connectors between scene cards. This guide covers **compass exits**, **floor links**, **Use transitions**, and **gated exits** (#42). It also answers the old “special case” transition ask (#16).

## Compass exits (gold)

Mid-edge ports (**F / B / L / R**) write `exits` + `movement` on the scene. In game these are the directional movement buttons.

Right-click a **gold** same-floor wire:

| Menu item | Purpose |
|-----------|---------|
| **Delete** | Remove the exit (optionally reciprocal) |
| **Edit Transition Audio…** | Enter/exit SFX for this neighbor pair |
| **Exit Requirements…** | Gates, block badge, blocked notebook/TTS |

## Floor transitions (up / down)

Floor links are still compass exits — **`exits.up` / `exits.down`** — but the destination card lives on **another map level**, so the editor **does not draw a gold wire** across floors.

### How to connect floors

1. Select a scene card → right-click → **Connect to floor…**, **or**  
2. Drag one card onto another and choose **Up** / **Down** in the stack dialog.

That authors reciprocal `up`/`down` (and `movement`) when the link is created as a floor connection. Prefer **`down`/`up`** for true floor changes (not `backward`/`forward` across levels), so stair badges appear on both cards.

### Stair badges

On each end of a floor link the card shows a badge:

| Badge | Meaning |
|-------|---------|
| **`^N`** | Exit **up** to a scene on floor **N** |
| **`vN`** | Exit **down** to a scene on floor **N** |

Gold-tinted badges mean that direction has **exit requirements**.

**Right-click the badge** (not the card body) for the same menu as a gold wire: Delete / Edit Transition Audio… / Exit Requirements….

Switch floor chrome (**−** / **+** or floor label) to see the other card.

### Example

`high_alpine_trail` (floor 0) → **up** → `alpine_summit` (floor 1): badge **`^1`** on the trail, **`v0`** on the summit.

## Use transitions (silver)

Corner ports (**NW / NE / SW / SE**) author **Use-driven scene changes**:

| Binding | JSON | In-game Use behavior |
|---------|------|----------------------|
| **Direct Use** | Scene `useExit` (+ optional `useDetails`, `useRepeatStatus`, map corner fields) | If the scene has **no** available `interactions[]`, Use runs immediately (no picker). |
| **Interaction Use** | `interactions[]` entry with `exitSceneId` (map stubs are often `use_map_N`) | Use opens the interaction picker when more than one Use option exists. |

Drag corner → corner (or **Manage…** on the silver wire) to create/edit these. The dialog’s **Use description** is the narrative when the player clicks Use (`useDetails`).

Right-click a **silver** wire → **Manage…** or **Edit Transition Audio…**.

**Note:** Edit Scene does **not** author Use description / repeat — those live on Manage Use Transition (and Effects for repeat status). Saving Edit Scene preserves them.

### What gets a silver Use wire

**Only Use actions that leave for another scene** are drawn:

- Scene `useExit` → another scene / `scene#sub`
- An interaction with non-empty `exitSceneId`

**Same-room Use** (narrative only, no `exitSceneId`) has **no** map wire but still appears in the Use picker.

## Special-case transitions (#16 fold)

| Need | Author with | Player UI |
|------|-------------|-----------|
| Walk F/B/L/R | Gold compass wire | Movement buttons |
| Climb/descend floors | Connect to floor → stair badge | Up/down movement |
| “Use the door / rug / desk” to another scene | Silver Use wire | Use (direct or picker) |
| Locked / dark / story-gated MOVE | Exit Requirements on gold wire or stair badge | Blocked notebook + optional TTS; no move |
| “Search the drawer” for an item | Variables → **Interactions** (grantItem) | Examine → Use |
| Item on the ground after examine | Variables → **Inventory** (takeable) | Examine → Take |

There is no separate “special transition” schema beyond these tools.

## Exit requirements (gated compass / floor exits)

Open **Exit Requirements…** from a gold wire or stair badge.

Requirements are **one-way** (`fromScene.exitRequirements[direction]`). The dialog **direction slider** flips outbound vs return (e.g. `snow_cave_exterior -> snow_cave_interior` vs the reverse). Each side has its own gates, badge, blocked details, and TTS.

### Gates

- **Exit requires a light source** — any inventory item with `lightSource: true` (lantern, future candle, …)
- **Exit requires a room purchased today** — saloon lodging day flag
- **Inventory item id(s)** — specific id(s); comma-separated if *all* are required (autocomplete; Tab accepts)
- **Story flag** — unlock-once pattern (e.g. after a Use “unlock” sets the flag)

### Block badge

`auto` / `light` / `lock` / `gear` — padlock keys should use **lock**.

### Blocked VO

- **Blocked details** — notebook `Blocked:` text (right-click → Edit full screen / parchment)
- **Blocked TTS** — Voice · **Generate TTS dialog** · **Generate Voice** · **Preview voice**
- **Blocked variants** — `{ when, details, tts }` bags; **first matching `when` wins**; empty `when` = default (**place last**)

Runtime: clicking a gated MOVE control shows/plays the blocked copy **without moving**.

### `when` condition grammar (P3)

Bare form (preferred in JSON / variant `when` field):

| Example | Meaning |
|---------|---------|
| `item:padlock_key:in_inventory` | Player has item |
| `item:padlock_key:discovered` | Taken/discovered (or in inventory) |
| `not_item:padlock_key:in_inventory` | Negation (`not_` on object or state) |
| `scene:saloon_service_hall:examined` | Scene examined this playthrough |
| `scene:x:visited` | Entered / examined (best-effort) |
| `flag:some_flag:set` | Story flag present |
| `milestone:quest_id:set` | Milestone started or complete |
| `actor:bartender:observed` | Actor known (`spoken_to` / `attacked` via `actor:<id>:<state>` flags) |

Brace sugar `{{condition:item:padlock_key:in_inventory}}` is accepted as the same clause. **Do not** put `{{condition}}` inside TTS bake text — use separate variant TTS bags. The editor highlights condition tags (yellow) and body (gray).

## Alternate views vs Use wires (#40)

Keep **alternate / sub-scene cards** (`parent#sub`) for focus plates and per-view TTS (e.g. snow cave `chamber`). **Use wires** are for going to another authored scene (door, nightstand focus scene). Prefer Use when the player changes scene; prefer an alternate when it is the same scene with a different image/description/TTS bag.

## Place item vs Inventory

| Goal | Where | Player path |
|------|--------|-------------|
| Search furniture / grant on Use | Variables → **Interactions** → **Add place item…** | Examine → **Use** |
| Ground loot after examine | Variables → **Inventory** → **+** | Examine → **Take** |

Interactions defaults: requires examine, one-shot `useFlag` / `hideWhenStoryFlag` = `sceneId:itemId_taken`, `grantItem`. Inventory rows use a **Must examine scene first** slider (#50).

**These buttons are on the Scene Variables pane** (not inside Edit Scene).

### Worked example (key + locked door)

1. Bedroom → Use wire to nightstand focus scene (silver).  
2. On the nightstand scene → Variables → **Inventory** → **+** → `key_ring` (or **Interactions** → place item if you want Use instead of Take).  
3. `saloon_service_hall` → right-click gold wire to the supply closet → **Exit Requirements…** → inventory `key_ring`, badge **lock**, blocked details/TTS → Save.

## Quick checklist

| Authoring goal | Map affordance | Player UI |
|----------------|----------------|-----------|
| Walk F/B/L/R | Gold wire | Movement |
| Floor up/down | Stair badge `^N` / `vN` | Up/down |
| Use into another scene | Silver wire | Use |
| Same-room Use narrative | *(none)* | Use picker |
| Gate a MOVE | Exit Requirements on wire/badge | Blocked + TTS |
| Find item via Use | Interactions | Examine → Use |
| Find item via Take | Inventory | Examine → Take |

See also Manage Use Transition help text in the editor for **Create new / Clear / Accept / Cancel**.
