# Scene ambient & music authoring

Edit Scene can generate **ambient beds** (room tone) and **music undersores** for each scene. They write under `resources/audio/ambient/` and `resources/audio/music/`, then compress sibling `.xz` files for release packing.

## Ambient (Generate ambient)

**Preferred backend:** xAI Grok Imagine Video (`grok-imagine-video-1.5`) with `generate_audio`, then ffmpeg extract → MP3.

**Fallback:** local procedural layers (`local_layers_v1`) planned by chat. This is thinner than a good video extract — the run log prints `[ambient-backend] local_layers_v1` when it happens.

### Env

| Variable | Meaning |
|----------|---------|
| `TIMBERLINE_AUDIO_BACKEND=auto` | Try video extract, else local (default) |
| `TIMBERLINE_AUDIO_BACKEND=xai_video` | Require video extract |
| `TIMBERLINE_AUDIO_BACKEND=local` | Procedural only |
| `XAI_API_KEY` | Imagine Video + layer planner |

Needs a working `ffmpeg` (Homebrew installs are often dyld-broken; `pip install imageio-ffmpeg` is preferred).

### Prompt hygiene

Ambient jobs use the **scene overview only** (not examine notes). Examine prose that mentions humming, speech, or music tends to produce strange non-ambient audio from the video model.

### Restoring a good bed

Before overwrite, Generate rotates:

- `name.mp3` → `name_1.mp3`
- prior `name_1` → `name_2`

Copy `_1` back over the live file if a regen goes wrong.

## Music (Generate music)

**Backend:** ElevenLabs Music (`music_v2_5`), instrumental-only. The period prompt is honored — there is **no** procedural sine-pad fallback.

### Keys (scene editor)

**Preferred:** **Options → Configure API keys…** (native menu between File and Window). Paste xAI + ElevenLabs keys; Confirm keeps them **in memory for this editor session only** (never written to disk from the dialog). See issue #56.

Empty AI path fields show a short hint when the required key is missing (e.g. `XAI key required — go to console.x.ai`). A red **X** / green **✓** icon sits left of each Generate row; Generate is accent-enabled only when the check is green and the row has content.

### Key (CLI / temporary disk fallback)

| Source | Path / env |
|--------|------------|
| Env | `ELEVENLABS_API_KEY` (also `ELEVEN_API_KEY` / `XI_API_KEY`) |
| File | `~/.config/highline-ridge/elevenlabs_api_key` (gitignored; temporary) |
| CLI | `python3 tools/run_item_authoring_ai.py --elevenlabs-key=...` |

Do **not** put the key in `resources/`. Confirm your ElevenLabs plan allows commercial game use before shipping release beds.

### Style presets (Edit Scene)

Click **Music style (1890s)** under the music path to cycle:

| Id | Intent |
|----|--------|
| `saloon_piano` | Parlor upright / early ragtime |
| `trail_folk` | Fiddle + acoustic, open country |
| `cabin_hearth` | Sparse intimate piano |
| `mining_camp` | Harmonica / spare guitar |
| `tension` | Low period underscore |
| `title_hymn` | Hopeful menu / title acoustic |
| `scarlet_whispers` | Tragic violin + chamber orchestra (“Scarlet Whispers at Dawn”) — strong title / mournful scene bed |

Default length ~24s loopable bed. Title screen (`resources/audio/music/title_theme.mp3`) prefers `scarlet_whispers`.

## Runtime

`AudioManager` crossfades `audio.music` and loops `audio.ambient[]` on scene enter. Dialog ducks room beds. See `src/AudioManager.cpp` and `docs/platform-parallelism.md` (async bed experiments are parked).

## Known content notes

- `saloon_noir_jazz` is anachronistic for 1891 — regenerate with `saloon_piano` when ready.
- `mountain_wind` under `music/` is wind ambience misfiled; prefer `resources/audio/ambient/wind.mp3` for outdoor beds.
