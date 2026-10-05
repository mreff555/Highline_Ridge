# Scene ambient & music authoring

Edit Scene can generate **ambient beds** (room tone) and **music underscores** for each scene. They write under `resources/audio/ambient/` and `resources/audio/music/`, then compress sibling `.xz` files for release packing.

**Keys:** ambient and music need **ElevenLabs** by default — setup is only in **[api-keys.md](api-keys.md)** (do not duplicate key paths here).

## Ambient (Generate ambient)

**Preferred backend:** ElevenLabs Sound Effects (`eleven_text_to_sound_v2`, looping bed). Edit Scene labels this **Ambient path (ElevenLabs loop)**.

**Other backends** (`TIMBERLINE_AUDIO_BACKEND`):

| Value | Meaning |
|-------|---------|
| `auto` (default) | ElevenLabs when a key is present; otherwise legacy paths |
| `elevenlabs` | Require ElevenLabs looping SFX |
| `xai_video` | Legacy Imagine Video extract (needs xAI key + working ffmpeg) |
| `local` | Procedural layered synth only (thin; avoid when EL is configured) |

With an ElevenLabs key present, auto/elevenlabs **does not** silently fall back to procedural local layers.

### Prompt hygiene

Ambient jobs use the **scene overview only** (not examine notes). Examine prose that mentions humming, speech, or music tends to produce strange non-ambient audio.

### Restoring a good bed

Before overwrite, Generate rotates:

- `name.mp3` → `name_1.mp3`
- prior `name_1` → `name_2`

Copy `_1` back over the live file if a regen goes wrong.

## Music (Generate music)

**Backend:** ElevenLabs Music (`music_v2_5`), instrumental-only. The period prompt is honored — there is **no** procedural sine-pad fallback. Key setup: [api-keys.md](api-keys.md). Confirm your ElevenLabs plan allows commercial game use before shipping release beds.

### Style presets (Edit Scene)

**Music style** dropdown under the music path (ids match `MUSIC_STYLE_PRESETS` / `MusicStylePresets.h`). **Preview** plays a ~10s cached clip (`resources/.authoring/music_preview/<id>.mp3`); Shift+Preview regenerates.

| Id | Label |
|----|--------|
| `saloon_piano` | Saloon piano |
| `parlor_waltz` | Parlor waltz |
| `dance_hall` | Dance hall |
| `trail_folk` | Trail fiddle |
| `cabin_hearth` | Cabin piano |
| `hotel_parlor` | Hotel parlor |
| `mining_camp` | Camp harmonica |
| `depot_guitar` | Depot guitar |
| `river_guitar` | River guitar |
| `night_watch` | Night watch |
| `snowbound` | Snowbound |
| `tension` | Tension |
| `quiet_inquiry` | Quiet inquiry |
| `vespers` | Vespers |
| `title_hymn` | Title hymn |
| `scarlet_whispers` | Scarlet violin |

Default Generate length ~24s loopable bed. Title screen (`resources/audio/music/title_theme.mp3`) prefers `scarlet_whispers`.

## Runtime

`AudioManager` crossfades `audio.music` and loops `audio.ambient[]` on scene enter. Dialog ducks room beds. See `src/AudioManager.cpp` and `docs/platform-parallelism.md` (async bed experiments are parked).

## Known content notes

- `saloon_noir_jazz` is anachronistic for 1891 — regenerate with `saloon_piano` when ready.
- `mountain_wind` under `music/` is wind ambience misfiled; prefer `resources/audio/ambient/wind.mp3` for outdoor beds.
