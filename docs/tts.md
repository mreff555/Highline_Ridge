# TTS syntax and usage

Timberline uses **xAI Grok voices** for optional spoken dialog and scene narrative. Audio is authored in **dev** builds (disk `resources/`), then packed into the release HLAP pak.

Get an API key and credits at the [xAI console](https://console.x.ai). Cost to regenerate a short game’s voices is usually small.

## Voices

Built-in ids: **ara**, **eve**, **helios**, **leo**, **rex**, **rigel**, **sal**.

Set a default per scene / item / conversation line (`ttsDefaultVoice`, `ttsVoice`, etc.). Unknown ids fail validation.

## Markup

### Realism / timing tags

| Tag | Purpose |
|-----|---------|
| `[pause]` | Standard conversational pause |
| `[pause:Xms]` | Timed pause, e.g. `[pause:500ms]` or `[pause:1500ms]` |
| `[long-pause]` | Longer dramatic break |
| `[laugh]` / `[chuckle]` / `[giggle]` | Laughter |
| `[sigh]` / `[cry]` | Emotion |
| `[hum-tune]` | Humming |
| `[tsk]` / `[tongue-click]` / `[lip-smack]` | Mouth sounds |
| `[breath]` / `[inhale]` / `[exhale]` | Breath |

### Style wrappings

Preferred paired form: `<whisper>…</whisper>`, also `<soft>` `<loud>` `<build-intensity>` `<decrease-intensity>` `<higher-pitch>` `<lower-pitch>` `<slow>` `<fast>` `<sing-song>` `<singing>` `<emphasis>`.

Bracket form (`[emphasis]`, `[whisper]`, …) is also recognized for **editor highlighting** (and is common in authored copy). Prefer matched `<tag>…</tag>` for engine/TTS behavior.

### Voice substitution

Switch speaker mid-line (narrator vs actor):

```text
She turns away. {{voice:eve}}I can't tell you everything.{{/voice}}
```

Always close with `{{/voice}}`. Malformed markup fails `validate_tts` / refresh.

### Not the same as dialog tokens

`{tab_amount}`-style **world tokens** are runtime text substitution for on-screen copy — see [dialog-tokens.md](dialog-tokens.md). They are **not** expanded inside pre-rendered TTS clips.

## Editor highlighting

In the scene editor, TTS fields (Conversations walkthrough, Variables TTS side, Edit Scene TTS description/examine, item TTS) color:

- pauses / spoken voice spans  
- `{{voice:…}}` tags  
- style markup  
- errors (unclosed / unknown)

## Refresh CLI (dev builds only)

Release binaries **strip** these flags so players cannot hit the API by accident.

| Flag | Purpose |
|------|---------|
| `--key=API_KEY` | xAI key (not stored) |
| `--refresh-voices` | Regenerate all `ttsEnabled` owners |
| `--refresh=ID` | Limit to one conversation / scene / item / recipe id |
| `-force` / `--force` | Ignore text hashes; regenerate matching lines |

```bash
./build/Highline\ Ridge --key=YOUR_XAI_API_KEY --refresh-voices
./build/Highline\ Ridge --key=YOUR_XAI_API_KEY --refresh=saloon_balcony
./build/Highline\ Ridge --key=YOUR_XAI_API_KEY --force --refresh=stay_last_night
./build-release.sh   # bake new audio into the pak
```

Refresh writes `resources/audio/tts/**/*.mp3.xz` and JSON hashes on **disk**. It does **not** patch an already-embedded pak — rebuild release afterward.

## Packaging rule

1. Author / edit text in dev + editor  
2. Refresh voices in a **dev** binary  
3. `./build-release.sh` to embed audio  

Players never need an API key.
