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

Drag corner → corner (or Manage Use Transition) to create/edit these. **Accept** saves the Use description and destination; **Cancel** closes without changing the destination.

### What gets a silver Use wire on the map

**Only Use actions that leave for another scene** are drawn as map wires:

- Scene `useExit` pointing at another scene / `scene#sub`
- An interaction with a non-empty `exitSceneId`

**Same-room Use actions do not get a map wire.** They still appear in the in-game Use list when authored as `interactions[]` with narrative (and no exit), for example cabin **Sit in the luxurious chair** (`sit_chair`): it has `useDetails` but no `exitSceneId`, so there is nothing to connect on the map. Cabin **Lift the rug** (`lift_rug`) has `exitSceneId: cabin_under_rug`, so it **does** show a Use line.

Missing art is unrelated: a transition Use still draws a wire to the destination card even if that scene still uses a placeholder image.

### Quick checklist

| Authoring goal | Map wire? | Player UI |
|----------------|-----------|-----------|
| Walk F/B/L/R (or floor) to another room | Gold compass / floor link | Movement buttons |
| Use to enter another scene (door, rug, sit-at-desk focus, etc.) | Silver Use corner wire | Use (direct or picker) |
| Use that only plays narrative / status in the same room | **No** wire | Use picker (interaction list) |

See also Manage Use Transition help text in the editor for **Create new / Clear / Accept / Cancel**.
