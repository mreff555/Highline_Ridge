# Highline Ridge

**Highline Ridge** is a point-and-click mystery game built on the **Timberline** engine.

Storyboarding is still in progress; development focus is currently on Timberline as a reusable fixed-image narrative platform. A finished short game will showcase the engine. Contributions are welcome.

## Build instructions

The main third-party dependency is raylib (fetched by CMake). You need a C/C++ compiler. Builds have been tested on macOS (Intel). See [BUILD.md](BUILD.md) for platform details.

```bash
mkdir build
cd build
cmake ..
make
cd ../tools/scene-editor
mkdir build
cd build
cmake ..
make
```
Audio is enabled for some dialogs. Voices must be generated if you want TTS playback; see `--help` on the game binary.
## Timberline engine

Timberline is a rich storytelling engine that uses old-school fixed images so a small team (or one developer, with AI-assisted art) can ship scene-driven adventures without a full 3D pipeline.

- **Fixed-image scenes** with JSON configuration for exits, movement, sub-scenes, and interactions
- **Conversation system** with phases, nested dialog choices, milestones, and requirements
- **TTS** via Grok/xAI voices: default voice per line, markup to switch narrator and actor voices mid-line (`{{voice:eve}}…{{/voice}}`), SHA-based regeneration so unchanged dialog is skipped
- **Data-driven resources** under `resources/` (images, audio, conversations, scenes); binaries may be xz-compressed
- **Timberline Resource Editor** (`tools/scene-editor`) for scene maps, variables, and conversation editing (beta)
- **Player stats** that change through play:
  - **Health** — at 0%, you die; increases with sleep
  - **Energy** — stamina for hard work; increases with sleep
  - **Resolve** — grit for demanding tasks; stimulants or booze can help
  - **Lucidity** — grip on reality; sleep helps; matters for conversation and intellect
  - **Charisma** — improves odds in conversation

For voice generation, supply an xAI API key and run `./Highline\ Ridge --help`.

### Architecture
main.cpp  
   └─ SceneEditorApp          (shell: wire, fonts, selection, update/draw)  
         ├─ DocumentWorkspace   (scenes/conversations JSON, tabs, dirty)  
         ├─ EditorLayout        (panes / dividers)  
         ├─ ThumbnailCache  
         ├─ VariableEditor      (modal + TTS + text metrics)  
         ├─ ConversationTree    (tree model + view)  
         ├─ SceneGraphModel     (exits, levels, auto-layout, stack state)  
         └─ SceneMapCanvas      (list/map/chrome draw + interaction)  

## The game

The year is roughly 1891. You wake up in a cave, injured and with total memory loss, high in the mountains of Appalachia, a couple thousand feet above a small mountain town known as **Highline Ridge**. As you move around town and talk to people, you get the sense that some of them know you — but they are not giving straight answers.
