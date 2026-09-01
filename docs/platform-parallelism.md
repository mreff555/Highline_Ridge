# Timberline platform parallelism

Shared foundation for **multi-core CPU jobs**, later **SIMD**, and **GPU compute**
(Metal on macOS, Vulkan on Win/Linux). See the multi-processor plan for phases.

## JobSystem (Phase A — landed)

- Headers: `src/JobSystem.h` / `src/JobSystem.cpp`
- Workers run CPU work only — **never** call raylib GL (`LoadTexture*`, `BeginDrawing`, …).
- Optional `onComplete` callbacks are drained on the main thread via `pollCompletions()`.

```cpp
auto& jobs = timberline_engine::JobSystem::global();
jobs.enqueue(
    [] { /* decompress / decode off-main */ },
    [] { /* LoadTextureFromImage on main */ });
// each frame:
jobs.pollCompletions();
```

Smoke test: `./test_job_system` (built with the game).

CMake: `HIGHLINE_JOB_WORKERS` (reserved for callers that pass an explicit count; `0` = auto).

## Scene image decode (Phase A slice — landed)

- `loadResourceImage` / `SceneDatabase::decodeSceneImage` — CPU xz+PNG/JPEG (serialized; stb-safe)
- `GameSession::refreshSceneImage` enqueues decode on `JobSystem`; main thread uploads via `LoadTextureFromImage`
- Previous room texture is held until upload (`ActiveScene::loadFromStruct` preserve + `adoptOwnedTexture`)
- Movement blocked while `sceneImageLoadPending`
- Force sync: `HIGHLINE_SYNC_SCENE_LOAD=1`

## Room audio beds

- **Current (stable):** sync `resolveMusicAssetFile` → `/tmp` extract when needed → `LoadMusicStream` / `LoadSound` (same as pre-JobSystem audio).
- JobSystem async / FromMemory experiments are parked — they raced with repeated `onRoomEnter` and could cancel every in-flight bed (silence).
- TTS dialog still uses `resolveMusicAssetFile` / `LoadSound`.

## Editor thumbnails (landed)

- `ThumbnailCache::getOrLoad` enqueues CPU decode on `JobSystem` (never blocks the UI thread on xz/PNG)
- `ThumbnailCache::poll()` each editor frame uploads completed `Image`s via `LoadTextureFromImage`
- List/map cards show empty thumbs for a frame or two, then fill in as jobs complete
- `clear()` / generation token ignores stale jobs after regen/invalidate

## Neighbor-scene image prefetch (landed)

- After each `refreshSceneImage`, enqueue decode+upload for **forward/backward/left/right** exit targets
- Cache keyed by resolved image path; pruned when leaving a room’s neighbor set
- Entering a prefetched room **adopts** the cached texture (no hitch / no duplicate job)
- If a prefetch is still loading when you enter, the pending transition waits on that job

## SIMD helpers (landed)

- `src/SimdUtil.h` / `.cpp`: `simdFillBytes`, `downscaleImageToFit` (box filter for R8G8B8 / RGBA)
- Editor `ThumbnailCache` downscales decoded thumbs to ≤320×136 on the worker before GPU upload
- CMake: `HIGHLINE_SIMD` (default ON) — reserved for toggling NEON/SSE fill paths

## Next

1. Revisit async audio with hardened in-flight rules (optional)
2. `GpuDevice` with Metal + Vulkan compute backends
