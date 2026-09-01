# Display aspect ratios & scene plates

## Player-facing

- **Windowed / fullscreen** already live in settings (`display.fullscreen`, resolution).
- Scene art is drawn with **cover-crop** (uniform scale, center crop) — never stretched.
- `display.aspectPreference` in `user_config.json`: `"auto"` (default).  
  Code also accepts `"16x9"`, `"16x10"`, `"21x9"` for forced variant picking; v1 UI stays on Auto.

## Art / data

Canonical target: **16:9** masters (recommend **3840×2160** when regenerating).

Optional per-scene variants in `scenes.json`:

```json
"image": "resources/images/mining_camp.png",
"imageVariants": {
  "16x9": "resources/images/mining_camp_16x9.png",
  "16x10": "resources/images/mining_camp_16x10.png",
  "21x9": "resources/images/mining_camp_21x9.png"
}
```

- Runtime picks exact bucket → else `16x9` → else `image`.
- Story **alternateImages** / focus subscene plates still override; aspect variants apply to the canonical main plate only (v1).
- **Pak format unchanged** — more/larger files only.

## Migrating images

Engine works with **existing** plates via cover-crop. Regenerating or upscaling to 4K 16:9 is a **manual art** pass (or future tooling), not automatic.
