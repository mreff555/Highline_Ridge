# Building Highline Ridge (Timberline engine)

**Highline Ridge** is the showcase game; it runs on the **Timberline** engine. Project overview, contributors, and architecture diagrams live in **[README.md](README.md)**.

Build the game from the repo root; the **Timberline Resource Editor** is built as `scene-editor` (dev default ON).

The game uses CMake, bundled raylib 5.5, and three native libraries for assets/audio:

- **liblzma** — `.xz` compressed images and audio
- **libjpeg** — JPEG examine art
- **libopusfile** + **libopus** — dialog TTS playback

Multi-core / SIMD / GPU compute work is documented in [docs/platform-parallelism.md](docs/platform-parallelism.md) (`JobSystem` is in-tree; async asset decode and Metal/Vulkan compute follow).

Resources are copied into the build directory automatically (`sync_resources`).

Preferred workflow is the classic out-of-source Makefile build (**dev default**):

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
./Highline\ Ridge          # game (disk resources/)
./scene-editor             # resource editor (built by default in dev)
```

| Mode | Configure | Embed resources | Scene editor |
|------|-----------|-----------------|--------------|
| **Dev (default)** | `cmake ..` | OFF | **ON** |
| **Release (player)** | `cmake .. -DHIGHLINE_RELEASE=ON` | **ON** | OFF |
| **Release + editor** | `cmake .. -DHIGHLINE_RELEASE=ON -DHIGHLINE_BUILD_EDITOR=ON` | **ON** | **ON** |

### User data (saves / settings)

Writable data does **not** live next to the binary in release builds:

| Platform | Directory |
|----------|-----------|
| Linux | `~/.highline_ridge/` |
| macOS | `~/Library/Application Support/Highline Ridge/` |
| Windows | `%AppData%\\Highline Ridge\\` |

Override with `HIGHLINE_DATA_DIR`. Saves go in `saves/` under that root.

### Release options (in progress)

Preferred: wipe + rebuild with the helper script (avoids stale CMake cache):

```bash
./build-release.sh                      # game only (embedded resources)
./build-release.sh --with-scene-editor  # game + ./scene-editor
./build-release.sh --with-dev-tools     # keep Ctrl+Shift+S / ~ console
```

Manual equivalent:

```bash
mkdir -p build-release && cd build-release
cmake .. -DHIGHLINE_RELEASE=ON -DCMAKE_INSTALL_PREFIX=/opt/highline_ridge
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
./Highline\ Ridge
```

`HIGHLINE_RELEASE=ON` turns **embed ON** and routes game `TraceLog` output to **stderr** (dev builds keep stdout). The editor defaults to **OFF** in that mode, but you can keep it with `-DHIGHLINE_BUILD_EDITOR=ON` (or `./build-release.sh --with-scene-editor`). In-game developer tools (Ctrl+Shift+S overlay, `~` console) default **ON** for normal builds and **OFF** for release unless `-DHIGHLINE_DEV_TOOLS=ON` / `--with-dev-tools`. TTS refresh CLI (`--key`, `--refresh-voices`, …) is **compiled out** of release binaries — refresh with a dev build, then rebuild release to repack.

- `HIGHLINE_EMBED_RESOURCES` — pack assets into the binary (default **OFF** / dev).
- `HIGHLINE_BUILD_EDITOR` — build `scene-editor` (default **ON** / dev; default OFF when using `HIGHLINE_RELEASE` alone).
- `HIGHLINE_DEV_TOOLS` — in-game developer tools (default **ON** / dev; default OFF for release).
- `HIGHLINE_INSTALL_LINKS` — install `/usr/local/bin` symlinks (default **ON**); use `-DHIGHLINE_INSTALL_LINKS=OFF` / `--without-links` to skip.

```bash
# Release game + scene editor (manual)
mkdir -p build-release && cd build-release
cmake .. -DHIGHLINE_RELEASE=ON -DHIGHLINE_BUILD_EDITOR=ON
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
./Highline\ Ridge
./scene-editor
```

The nested build writes `scene-editor` next to `Highline Ridge` in `build-release/`, not under `build-release/tools/scene-editor/`.

Pack tooling: `python3 tools/pack_assets.py --resources resources --out-dir build/generated`.

With embed ON, game content is linked into the executable (`.incbin` of `highline_assets.pak`). A `resources/` folder is **not** required to play. Saves go under the platform user-data directory (e.g. `~/.highline_ridge/saves` on Linux).

## macOS

```bash
brew install cmake xz jpeg opusfile pkg-config
mkdir -p build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
./Highline\ Ridge
```

### Apple Silicon / Homebrew under `/opt/homebrew`

On Apple Silicon, Homebrew installs to **`/opt/homebrew`** (Intel Macs use `/usr/local`). Some shells, IDE CMake kits, and GUI-launched tools never put `/opt/homebrew/bin` on `PATH` or `PKG_CONFIG_PATH`, so `libjpeg` / `liblzma` / `opusfile` look “missing” even after `brew install`.

CMake auto-detects the Homebrew prefix (`brew --prefix`, then `/opt/homebrew`, then `/usr/local`) via `cmake/HomebrewPrefix.cmake` and prepends it for `pkg-config` and `find_library` (#35). You usually do **not** need to export PATH just to configure.

If detection fails:

```bash
cmake .. -DHIGHLINE_HOMEBREW_PREFIX=/opt/homebrew
```

Confirm the configure log shows: `Homebrew prefix for deps: /opt/homebrew`.

## Linux

Install build tools and libraries (Debian/Ubuntu example):

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config \
  liblzma-dev libjpeg-dev libopusfile-dev libopus-dev
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./Highline\ Ridge
```

Fedora/RHEL variants: `liblzma-devel`, `libjpeg-turbo-devel`, `opusfile-devel`, `opus-devel`.

## Windows (MSVC)

Use **x64 Native Tools Command Prompt for VS** (or PowerShell with MSVC on `PATH`).

### Dependencies via vcpkg (recommended)

```powershell
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\vcpkg install liblzma libjpeg-turbo opus opusfile
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
.\build\Release\Highline Ridge.exe
```

### Notes

- The project file is `CMakeLists.txt` (standard casing; required on Linux).
- **Windows help wanted** — builds via vcpkg are documented but less battle-tested than macOS/Linux. See [CONTRIBUTING.md](CONTRIBUTING.md) (contact **feerstd@gmail.com**).
- Dev: run from the build folder so synced `resources/` resolve.
- Release: content is embedded; saves/settings use `%AppData%\Highline Ridge\` (or `HIGHLINE_DATA_DIR`).
- TTS refresh (`--key`, `--refresh-voices`) is a **dev** binary feature and needs `curl` on `PATH` for the xAI client.
- See [docs/dev-vs-release.md](docs/dev-vs-release.md) for the authoring vs player package split.