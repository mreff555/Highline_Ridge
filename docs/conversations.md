# Conversations (Speak dialog)

Timberline Speak dialog lives in **`resources/conversations.json`**, keyed by **scene id** (same id as `scenes.json`). There is no separate conversation document id.

For API keys / TTS refresh after editing spoken lines: [api-keys.md](api-keys.md), [tts.md](tts.md). Map MOVE / Use leaves: [scene-map-exits.md](scene-map-exits.md).

## How Speak turns on

| Piece | Role |
|-------|------|
| `scenes.json` → `actions.speak: true` | Speak button allowed for the scene |
| `conversations.json` → `speakPhases` | Scripted / random dialog for that scene |
| `scenes.json` → `speakDetails` | Legacy one-shot line when there are **no** phases |

Runtime prefers phases when present; otherwise `speakDetails`.

## Editor: Conversations tab (flowchart shell)

The Conversations tab is being rebuilt as a **Scenes-like flowchart**. Phase 1 ships the **layout shell** (graph is editor-local; it does not yet rewrite `conversations.json`).

| Pane | Role |
|------|------|
| **Upper left** | Narrow tree: **scene → actor → conversation** (~20% width; labels ~80% of row) |
| **Upper right** | Dialog **flowchart** — immobile **Start** compass (one child port **down**) + stub nodes |
| **Lower left** | **Dialog types** palette: scalable icons + brief captions; drag onto the canvas |
| **Lower center** | **Dialog details**: selected flowchart node fields (kind, text/TTS/voice, gate/event/item ids, pointers) |
| **Lower right** | **Dialog media** preview: scene image/ambient/music; Play TTS |

Default chrome on this tab only: left ~**20%**, top ~**75%** (bottom ~**25%**). Scenes tab keeps 40% / ⅔.

### Type catalog (living — still being filled in)

> Incomplete on purpose. New kinds / fields land here as they are decided. Session plan mirrors this section.

#### Base (every palette type)

| Field | Notes |
|-------|--------|
| Icon image | Palette + canvas badge |
| Brief description | Caption / tooltip |
| Children **0–5** | **Parent** port = top center (not corners). Child ports from **bottom-center outward**: Bottom → SW → SE → Left → Right. Parent color ≠ child color |
| Dialog image / ambient / music | Per-node overrides; else **scene** media |

**Start** (not in palette): immobile compass; no parent port; **1** child port **down**.

Wires use crimson speak-graph color (not gold MOVE / silver Use).

#### Shared StatModifier

Stats: `health` \| `energy` \| `resolve` \| `lucidity` \| `charisma`.  
Entry sketch: `{ stat, mode, amount }`.

| Mode | Meaning |
|------|---------|
| `deltaValue` | Add/subtract absolute amount |
| `deltaPercent` | Add/subtract percent of **current** (vs max: confirm at implement) |
| `setPercent` | Set to percent of **max** |

#### Kinds so far

| Kind | Children | Type-specific fields | Open |
|------|----------|----------------------|------|
| **Actor Dialog** | 5 | `dialogText`, `dialogTts`, `defaultVoice` | — |
| **Player Dialog** | 1 | `playerDialogText`, `defaultVoice` (**off** by default) | — |
| **Event** | 2 (pass / fail) | `eventId` (e.g. `walletCash >= 4`, `walletCash >= actorTab:bartender`, `saloonRoomAvailable`) | Availability gates. Migrate: (1) same-label `requiresMoney` + `requiresInsufficientMoney` → one Player ask → Event → two bodies; (2) `requiresPayActorTabInFull` + `requiresInsufficientForActorTab` → Event → two player branches; (3) solo money/tab/room requires* → Player → Event (pass only) |
| **Get item** | 1 | `itemId` | — |
| **Attack** | 2 (win / lose) | combatant (sketch); `playerDeathPossible` (else **10%** HP floor); win = **StatModifier** list | Combat algo; lose fields |
| **Trigger event** | 1 | `eventId` | — |
| **Actor inventory** | 1 | `inventoryActorId` (bag key); opening `dialogText` / TTS | Migrate collapses `openActorInventory` (no nested `browse_*` trees). Runtime synthesizes priced notebook choices from `actorInventories`. |

_Phase 1 palette stubs these seven kinds. Flowchart layout remains editor-local until Phase 2 persist._

### Phase 1 interactions

1. **Select** a scene / actor / phase in the left tree — the flowchart **auto-migrates** from that scope’s `speakPhases` (Start → intro Actor Dialog → Player Dialog choices → response Actors; `grantItem` / `grantStoryFlag` / `startPhase` / `resumeChoiceId` linked when possible). All choice availability gates become **Event** nodes: room cash vs comp, tab can-pay vs later, and solo `requiresMoney` / tab / room filters (shop buys, blackjack, etc.). Layout is editor-local until Phase 2 persist.
2. Drag a type from the palette onto the flowchart to place a stub card (cartoon icons; brief caption stays in the palette / hover hint only — not on map cards).
3. Drag from a **child** (out) port to a **parent** (top) port to wire.
4. Drag stub cards to reposition; **Delete** / **Backspace** removes the selected stub (not Start).
5. **Right-click** a node for structure-based edits (text / TTS / ids / delete). **Edit text** and **Edit TTS** open the fullscreen parchment desk. Actor / Player Dialog nodes also expose **Default voice ▸** (Off + built-in Grok voices); Player defaults to **Off**. **Generate voice** parses `{{voice:…}}` into multi-segment MP3s (`ttsAudioSegments`) like game `--refresh`; **Play TTS** queues those segments.
6. **Clean up** (top-right of Dialog flow) runs layered `relayoutGraph()` after dragging — same affordance as Scenes map Clean up.
7. Media pane shows the **selected scene** image/beds for now; per-node overrides + AI generate come in Phase 2.

Icons live under `resources/ui/editor/dialog_nodes/` (speech bubbles opposing directions, fist, fedora, `?`, `!`).

Selecting a **phase** (or choice under a phase) migrates that phase only — cleaner for large scenes like the saloon. Selecting the **scene** root migrates all phases (Start wires to the first).

The older **Dialog walkthrough** remains in the binary as a fallback compile path but is no longer the primary Conversations UI.

## Actor inventories (shops) — runtime (#60)

Shops no longer need giant nested `browse_*` / `buy_*` trees. Authored stock lives on the conversation document next to `speakPhases`:

```json
"alpine_hardware": {
  "actorInventories": {
    "merchant": {
      "items": [
        { "id": "crampons", "price": 3.25, "quantity": 1 },
        { "id": "mining_pick", "price": 4.5, "quantity": 1 }
      ]
    }
  },
  "speakPhases": [ ... ]
}
```

- **`quantity`**: finite stock; use `null` or `"infinite": true` for never-depleting lines.
- A player choice with `"openActorInventory": "merchant"` plays its response/TTS, then the runtime **synthesizes** browse → buy/decline → look-again choices from that actor’s bag (labels like `Crampons - $3.25`). Buys deduct money, grant the item from `items.json`, and decrement stock; bags persist in saves.
- Alpine Hardware is the pilot. Haberdashery still uses nested trees until cut over. Authoring helpers: `tools/shop_utils.py` (`use_actor_inventory=True`) and `tools/merge_hardware_conversation.py`.

## Choice → new scene (`exitSceneId`) — runtime (existing JSON)

Until the flowchart persists, leave-to-scene is still authored on choice objects in `conversations.json`:

```json
{
  "id": "leave_shop",
  "label": "I'd better be going.",
  "response": "Safe travels.",
  "closePhase": true,
  "exitSceneId": "mountain_road_supply_row"
}
```

Rules:

1. Response / TTS / grants / overlays run first.
2. If `exitSceneId` is set, the game transitions with **Movement** semantics (clears Use return stack) — not Use.
3. `exitSceneId` **wins over** `startPhase`.

## Related docs

- [scene-editor-tutorial.md](scene-editor-tutorial.md) § Conversations
- [dialog-tokens.md](dialog-tokens.md) — `{tab_amount}` etc. in reply text
- [scene-map-exits.md](scene-map-exits.md) — gold MOVE / silver Use
