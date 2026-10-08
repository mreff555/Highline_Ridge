# Documentation index

Project overview, abstract, and architecture live in the root **[README.md](../README.md)**. This folder holds topic guides. Prefer linking here instead of restating the same how-to in multiple places.

## Start here

| If you want to… | Read |
|-----------------|------|
| Build on macOS / Linux / Windows | [BUILD.md](../BUILD.md) |
| Contribute / open a PR | [CONTRIBUTING.md](../CONTRIBUTING.md) |
| Learn the scene editor | [scene-editor-tutorial.md](scene-editor-tutorial.md) |
| Set up xAI / ElevenLabs keys | [api-keys.md](api-keys.md) |
| Understand dev vs player builds | [dev-vs-release.md](dev-vs-release.md) |

## Topic guides (`docs/`)

| Doc | Canonical for |
|-----|----------------|
| [api-keys.md](api-keys.md) | Where keys live (`Options → Configure API keys…`, `~/.config/highline-ridge/`, mode `600`), env vars, which Generate actions need xAI vs ElevenLabs |
| [scene-editor-tutorial.md](scene-editor-tutorial.md) | Editor walkthrough (map, Variables, Conversations, Items, screenshots) |
| [scene-map-exits.md](scene-map-exits.md) | Compass / floors / Use wires / Exit Requirements / place-item vs Inventory |
| [scene-audio.md](scene-audio.md) | Ambient + music backends, style presets, backup rotation (not key setup) |
| [tts.md](tts.md) | Voice ids, markup, editor highlighting, `--refresh-voices` packaging loop |
| [conversations.md](conversations.md) | Speak / `speakPhases`, choice flow fields, `exitSceneId` leave rules |
| [dialog-tokens.md](dialog-tokens.md) | Runtime `{tab_amount}`-style world tokens (not TTS markup) |
| [display-aspect.md](display-aspect.md) | Display aspect preferences and scene plate ratios |
| [dev-vs-release.md](dev-vs-release.md) | Dev vs release matrix and authoring → ship loop |
| [platform-parallelism.md](platform-parallelism.md) | JobSystem, decode/prefetch, audio bed notes |

## Root docs (not under `docs/`)

| Doc | Canonical for |
|-----|----------------|
| [README.md](../README.md) | Product abstract, play loop / stats, architecture diagrams |
| [BUILD.md](../BUILD.md) | Dependencies, CMake flags, Homebrew `/opt/homebrew`, release script, **save/user-data paths** |
| [CONTRIBUTING.md](../CONTRIBUTING.md) | Contact, needs list, branch/PR workflow |

## Authorship rule of thumb

- **Keys** → [api-keys.md](api-keys.md) only  
- **Ambient/music how-to** → [scene-audio.md](scene-audio.md)  
- **TTS markup / refresh** → [tts.md](tts.md)  
- **Speak dialog / choice leaves** → [conversations.md](conversations.md)  
- **Map exits / gates** → [scene-map-exits.md](scene-map-exits.md)  
- **Editor UI tour** → [scene-editor-tutorial.md](scene-editor-tutorial.md)  

Other pages should **link**, not copy, those sections.
