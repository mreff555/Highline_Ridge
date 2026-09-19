# Contributing to The Timberline Engine and/or Highline Ridge.

Human and AI contributions are welcome. This project is largely AI co-developed (**c0d3B0t555** with his human Dan) and is built to work with **xAI** / Grok for TTS and authoring workflows.

## Contact

- **Email:** feerstd@gmail.com  
- **Issues:** [github.com/mreff555/Highline_Ridge/issues](https://github.com/mreff555/Highline_Ridge/issues)

Open an issue before large design swings (conversation file layout, global story-event graphs, packaging). Small fixes and docs PRs are always fine without a long design thread.

## What we need

- **Cross-platform support** — especially **Windows** (MSVC/vcpkg polish, packaging, CI, “it just runs” player builds). macOS and Linux are the daily drivers; Windows is documented but under-exercised.
- **Storyboarding** — mystery structure, character beats, endings that stay dark without painting us into a corner
- **Scene and TTS generation** — art, ambient/music, Grok voice refresh, markup quality
- **UI improvements** — game HUD and scene-editor usability
- **Documentation** — tutorials, screenshots, platform notes, diagrams

## Workflow

1. Prefer a **unique branch** off the **latest release-candidate branch** (currently `v0.3.0.0_RC`), not a long-lived personal fork of `main` unless agreed.
2. Keep changes focused; match existing code and doc style.
3. When the work is ready, open a **pull request** into the RC branch (or the branch maintainers specify).
4. If the PR addresses a GitHub issue, put the issue in the **title**:

   ```text
   Issue: 22 - Brief description
   ```

   Example: `Issue: 35 - Find Homebrew deps under /opt/homebrew on Apple Silicon`

5. Pull requests with windows CRLF file endings will be rejected.  Update your global config to automate this.
   ```
   git config --global core.autocrlf input
   ```

## Dev quick start

See [README.md](README.md) and [BUILD.md](BUILD.md). Typical loop:

```bash
git fetch origin
git checkout -b issue-22-docs origin/v0.3.0.0_RC
cmake -S . -B build && cmake --build build -j
./build/Highline\ Ridge
./build/scene-editor
```

Thanks for helping with Timberline and Highline Ridge.
