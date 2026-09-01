# Building Highline Ridge (Timberline engine)

**Highline Ridge** is the showcase game; it runs on the **Timberline** engine. Build the game from the repo root and the **Timberline Resource Editor** from `tools/scene-editor`.

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
brew install cmake xz jpeg opusfile
mkdir -p build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
./Highline\ Ridge
```

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
- Run the executable from the build folder so `resources/` and `saves/` resolve correctly, or keep `resources/` beside the `.exe`.
- Optional dev tool `XaiTtsClient` (`--key=KEY --refresh-voices`) needs `curl` on `PATH`.
- Save files live under `saves/` relative to the working directory.