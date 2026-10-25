# API keys (xAI + ElevenLabs)

Authoring tools call **xAI** and **ElevenLabs**. Players never need keys. Dev builds, the scene editor, and CLI refresh/generate jobs do.

Get keys at:

| Provider | Console |
|----------|---------|
| **xAI** | [console.x.ai](https://console.x.ai) |
| **ElevenLabs** | [elevenlabs.io](https://elevenlabs.io) → API keys |

---

## Preferred: Options → Configure API keys…

In **`scene-editor`** (macOS native menu bar):

1. **Options → Configure API keys…** (between **File** and **Window**).
2. Paste the **xAI** and/or **ElevenLabs** key into the fields.
3. Click **Confirm**.

**Confirm** does two things:

1. Loads the keys into **editor session memory** (Generate buttons enable once validation succeeds).
2. **Writes (or clears) on-disk files** under your user config directory so the next editor launch and CLI runners can pick them up without re-pasting:

| File | Purpose |
|------|---------|
| `$HOME/.config/highline-ridge/xai_api_key` | xAI / Grok (images, chat, TTS) |
| `$HOME/.config/highline-ridge/elevenlabs_api_key` | ElevenLabs (music, ambient, SFX) |

- Clearing a field and Confirming **deletes** that file.
- Files are written **atomically** and set to mode **`0600`** (owner read/write only).
- Do **not** put real keys under `resources/` or commit them.

Status glyphs next to Generate rows use ASCII **`OK` / `..` / `X`** (CourierPrime lacks Unicode checkmarks). Generate stays enabled while a key is present and not known-Invalid (including “still checking”).

---

## Saving keys manually (all operating systems)

The on-disk location is always:

```text
$HOME/.config/highline-ridge/xai_api_key
$HOME/.config/highline-ridge/elevenlabs_api_key
```

That is the **same relative path** on macOS, Linux, and Windows. Expand `$HOME` as follows:

| OS | Typical `$HOME` | Full key directory |
|----|-----------------|--------------------|
| **macOS / Linux** | `/Users/<you>` or `/home/<you>` | `~/.config/highline-ridge/` |
| **Windows (Git Bash / WSL / MSYS)** | `/c/Users/<you>` or `/home/<you>` | `$HOME/.config/highline-ridge/` |
| **Windows (PowerShell / cmd)** | often unset — use `%USERPROFILE%` | `%USERPROFILE%\.config\highline-ridge\` |

Notes:

- The **scene editor** (C++) reads/writes keys via the **`HOME`** environment variable. On native Windows shells, set `HOME` to your profile (or use Git Bash), or rely on env / CLI overrides below.
- The **Python CLI** (`tools/run_item_authoring_ai.py`) uses `Path.home()`, which resolves to the user profile on Windows even when `HOME` is unset — so `%USERPROFILE%\.config\highline-ridge\` works for CLI jobs.

### Shell examples

**macOS / Linux / Git Bash:**

```bash
mkdir -p ~/.config/highline-ridge

# One line each — no quotes, no trailing spaces
printf '%s\n' 'xai-your-key-here' > ~/.config/highline-ridge/xai_api_key
printf '%s\n' 'sk_your-elevenlabs-key-here' > ~/.config/highline-ridge/elevenlabs_api_key

chmod 700 ~/.config/highline-ridge
chmod 600 ~/.config/highline-ridge/xai_api_key
chmod 600 ~/.config/highline-ridge/elevenlabs_api_key
```

**Windows PowerShell:**

```powershell
$dir = Join-Path $env:USERPROFILE ".config\highline-ridge"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Set-Content -NoNewline -Path (Join-Path $dir "xai_api_key") -Value "xai-your-key-here"
Set-Content -NoNewline -Path (Join-Path $dir "elevenlabs_api_key") -Value "sk_your-elevenlabs-key-here"
# Optional: so the scene editor finds the same files
$env:HOME = $env:USERPROFILE
```

| Path | Mode | Notes |
|------|------|--------|
| `…/highline-ridge/` | `700` recommended (Unix) | Directory only you can list/enter |
| `…/xai_api_key` | **`600` required** (Unix) | Owner read/write; editor Confirm also forces `0600` |
| `…/elevenlabs_api_key` | **`600` required** (Unix) | Same |

Example stubs (no secrets): `resources/xai_api_key.example`, `resources/elevenlabs_api_key.example`.

On next `./build/scene-editor` launch, empty session slots are **bootstrapped** from these files (and from env — see below).

---

## Environment variables (optional)

Useful for CI, one-off shells, or game TTS refresh:

| Variable | Provider |
|----------|----------|
| `XAI_API_KEY` (also `xAI_API_KEY`, `GROK_API_KEY`) | xAI |
| `ELEVENLABS_API_KEY` (also `ELEVEN_API_KEY`, `XI_API_KEY`) | ElevenLabs |

Priority when bootstrapping an empty editor slot: **session / Configure Confirm** → **env** → **`$HOME/.config/highline-ridge/*_api_key`**. Legacy `resources/xai_api_key` may still be **read** if present; it is gitignored and must not be the preferred path.

---

## Command-line overrides

Pass keys on the command line when you do not want (or cannot use) the home-dir files:

```bash
# Authoring jobs (images, ambient, music, SFX, TTS text) — see job→key table below
python3 tools/run_item_authoring_ai.py \
  --jobs-file resources/.authoring/foo_ai_jobs.json \
  --key=YOUR_XAI_KEY \
  --elevenlabs-key=YOUR_ELEVENLABS_KEY

# Dev game TTS refresh (xAI only; not stored on disk by the game)
./build/Highline\ Ridge --key=YOUR_XAI_API_KEY --refresh-voices
```

| Flag | Key | Used for |
|------|-----|----------|
| `--key=` | **xAI** | Images, chat/TTS text markup, legacy `xai_video` ambient, game `--refresh-voices` |
| `--elevenlabs-key=` | **ElevenLabs** | Music, ambient beds, enter/exit SFX, item examine/use SFX |

Resolution order for each key (CLI runner): **flag** → **env** → **`$HOME/.config/highline-ridge/*_api_key`** → legacy paths.

See [tts.md](tts.md) for refresh flags.

---

## Which actions need which key

### xAI key (`--key` / `XAI_API_KEY` / `xai_api_key`)

| Action | Where |
|--------|--------|
| **Generate image** (scene plates) | Edit Scene / AI Assist |
| **Generate TTS dialog** text (scene description/examine bags) | Edit Scene |
| **Generate voice** (bake TTS markup → MP3) | Edit Scene / parchment / Exit Requirements |
| **Blocked / Exit Requirements TTS** generate + voice preview | Exit Requirements dialog |
| **Item AI Assist** — examine / icon **images**, assemble narrative text, TTS text jobs | Items → AI Assist |
| **Game TTS refresh** (`--refresh-voices` / `--refresh=…`) | Dev `Highline Ridge` binary |
| Ambient **legacy** Imagine Video path only (`TIMBERLINE_AUDIO_BACKEND=xai_video`) | CLI / ambient jobs |

### ElevenLabs key (`--elevenlabs-key` / `ELEVENLABS_API_KEY` / `elevenlabs_api_key`)

Prefer a key with **Music** and **Sound Effects** scopes (Music-only is enough for music; ambient/SFX need Sound Effects). Restricted music keys may 401 on `/v1/user` — the editor still treats them as Valid when compose works.

| Action | Where |
|--------|--------|
| **Generate ambient** (looping room bed) | Edit Scene / AI Assist — default backend |
| **Generate music** (period underscore / title beds) | Edit Scene / AI Assist |
| **Enter / exit** transition SFX | Edit Scene (Generate all / transition audio) |
| **Item examine / use SFX** | Items → AI Assist |

### No key required

Playing the game, editing prose/JSON in the scene editor, map layout, inventory/interactions wiring, and packing a release **do not** call these APIs. Ship baked `resources/audio/**` (and the HLAP pak) so players never need credentials.

---

## Related

Index: [README.md](README.md). Ambient/music backends: [scene-audio.md](scene-audio.md). TTS markup/refresh: [tts.md](tts.md). Editor tour: [scene-editor-tutorial.md](scene-editor-tutorial.md).
