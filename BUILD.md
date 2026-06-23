# Building Highline Ridge

The game uses CMake, bundled raylib 5.5, and three native libraries for assets/audio:

- **liblzma** — `.xz` compressed images and audio
- **libjpeg** — JPEG examine art
- **libopusfile** + **libopus** — dialog TTS playback

Resources are copied into the build directory automatically (`sync_resources`).

## macOS

```bash
brew install cmake xz jpeg opusfile
cmake -S . -B build
cmake --build build
./build/Highline\ Ridge
```

## Linux

Install build tools and libraries (Debian/Ubuntu example):

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config \
  liblzma-dev libjpeg-dev libopusfile-dev libopus-dev
cmake -S . -B build
cmake --build build
./build/Highline\ Ridge
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
- Optional dev tool `XaiTtsClient` (`--refresh-voices=KEY`) needs `curl` on `PATH`.
- Save files live under `saves/` relative to the working directory.