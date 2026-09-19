# Dev build vs release package

| | **Dev** (`cmake ..`) | **Release** (`./build-release.sh`) |
|--|----------------------|-------------------------------------|
| Resources | Loose `resources/` on disk (synced next to the binary) | Embedded **HLAP** pak inside the executable |
| Scene editor | Built by default (`./scene-editor`) | Off unless `--with-scene-editor` |
| TTS refresh CLI | `--key`, `--refresh-voices`, … | **Compiled out** |
| In-game dev tools | Ctrl+Shift+S, `~` console (default ON) | Off unless `--with-dev-tools` |
| Logs | Raylib / game logs on stdout | Game `TraceLog` → **stderr** |
| Player needs | Build tree + deps | Just the binary (+ optional sidecar pak fallback) |
| Saves / settings | User data dir (same as release) | User data dir — **not** next to the binary |

## Authoring loop

1. Edit JSON / art / dialog in **dev** + **scene-editor**  
2. Playtest `./build/Highline\ Ridge`  
3. Refresh TTS if spoken text changed ([tts.md](tts.md))  
4. `./build-release.sh` to pack and embed  
5. Ship the release binary — players do **not** get a `resources/` folder  

## What release does *not* include

- Writable `resources/` tree  
- Ability to regenerate voices without rebuilding  
- Scene editor (unless you explicitly built it into that tree)  
- Default in-game cheat/overlay tools  

## Flags cheat sheet

| CMake / script | Effect |
|----------------|--------|
| `HIGHLINE_RELEASE=ON` | Player-oriented defaults (embed ON, editor OFF, tools OFF) |
| `HIGHLINE_EMBED_RESOURCES` | Pack `resources/` into the binary |
| `HIGHLINE_BUILD_EDITOR` | Build `scene-editor` |
| `HIGHLINE_DEV_TOOLS` | Overlay + console |
| `./build-release.sh --with-scene-editor` | Release + editor |
| `./build-release.sh --with-dev-tools` | Release + tools |

See [BUILD.md](../BUILD.md) for platform install details.
