/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Portable bulk pixel helpers (auto-vectorized + optional NEON/SSE).
 * Safe for JobSystem workers — no raylib GL.
 ******************************************************************************/

#ifndef TIMBERLINE_SIMD_UTIL_H
#define TIMBERLINE_SIMD_UTIL_H

#include <raylib.h>

#include <cstddef>
#include <cstdint>

namespace timberline_engine
{

/** Fill bytes (memset-like; vectorizes well). */
void simdFillBytes(void* dst, std::uint8_t value, std::size_t count);

/**
 * Box-filter downscale of a raylib Image in place to fit within maxW×maxH
 * (aspect preserved). Supports R8G8B8 and R8G8B8A8. No-op if already smaller
 * or format unsupported. Intended for editor thumbnails on worker threads.
 */
bool downscaleImageToFit(Image& image, int maxW, int maxH);

/** Default editor card thumb budget (2× display ~148×68). */
inline constexpr int kEditorThumbMaxW = 320;
inline constexpr int kEditorThumbMaxH = 136;

} // namespace timberline_engine

#endif /* TIMBERLINE_SIMD_UTIL_H */
