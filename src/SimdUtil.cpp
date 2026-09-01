/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Bulk pixel helpers for JobSystem workers.
 ******************************************************************************/

#include "SimdUtil.h"

#include <algorithm>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define TIMBERLINE_HAVE_NEON 1
#elif defined(__SSE2__)
#include <emmintrin.h>
#define TIMBERLINE_HAVE_SSE2 1
#endif

namespace timberline_engine
{
namespace
{

int bytesPerPixel(int format)
{
    switch (format)
    {
    case PIXELFORMAT_UNCOMPRESSED_R8G8B8:
        return 3;
    case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
        return 4;
    default:
        return 0;
    }
}

} // namespace

void simdFillBytes(void* dst, std::uint8_t value, std::size_t count)
{
    if (dst == nullptr || count == 0)
        return;

    auto* out = static_cast<std::uint8_t*>(dst);

#if defined(TIMBERLINE_HAVE_NEON)
    const uint8x16_t v = vdupq_n_u8(value);
    std::size_t i = 0;
    for (; i + 16 <= count; i += 16)
        vst1q_u8(out + i, v);
    for (; i < count; ++i)
        out[i] = value;
#elif defined(TIMBERLINE_HAVE_SSE2)
    const __m128i v = _mm_set1_epi8(static_cast<char>(value));
    std::size_t i = 0;
    for (; i + 16 <= count; i += 16)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i), v);
    for (; i < count; ++i)
        out[i] = value;
#else
    // Prefer libc — highly tuned; clang also auto-vectorizes simple loops.
    std::memset(dst, value, count);
#endif
}

bool downscaleImageToFit(Image& image, int maxW, int maxH)
{
    if (image.data == nullptr || image.width <= 0 || image.height <= 0)
        return false;
    if (maxW < 1 || maxH < 1)
        return false;

    const int bpp = bytesPerPixel(image.format);
    if (bpp == 0)
        return false;

    if (image.width <= maxW && image.height <= maxH)
        return true;

    const float scale = std::min(
        static_cast<float>(maxW) / static_cast<float>(image.width),
        static_cast<float>(maxH) / static_cast<float>(image.height));
    int newW = std::max(1, static_cast<int>(image.width * scale + 0.5f));
    int newH = std::max(1, static_cast<int>(image.height * scale + 0.5f));
    if (newW > maxW)
        newW = maxW;
    if (newH > maxH)
        newH = maxH;
    if (newW == image.width && newH == image.height)
        return true;

    const int srcW = image.width;
    const int srcH = image.height;
    auto* src = static_cast<const std::uint8_t*>(image.data);
    const std::size_t dstBytes = static_cast<std::size_t>(newW) * static_cast<std::size_t>(newH)
        * static_cast<std::size_t>(bpp);
    auto* dst = static_cast<std::uint8_t*>(RL_MALLOC(dstBytes));
    if (dst == nullptr)
        return false;

    // Box filter: average source pixels mapping into each destination pixel.
    for (int y = 0; y < newH; ++y)
    {
        const int y0 = y * srcH / newH;
        const int y1 = std::max(y0 + 1, (y + 1) * srcH / newH);
        for (int x = 0; x < newW; ++x)
        {
            const int x0 = x * srcW / newW;
            const int x1 = std::max(x0 + 1, (x + 1) * srcW / newW);

            std::uint32_t sum[4] = {0, 0, 0, 0};
            int count = 0;
            for (int sy = y0; sy < y1; ++sy)
            {
                const std::uint8_t* row = src + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(srcW) * bpp);
                for (int sx = x0; sx < x1; ++sx)
                {
                    const std::uint8_t* p = row + static_cast<std::size_t>(sx) * bpp;
                    // Inner channel loop is tiny (3–4); outer pixel loops vectorize poorly
                    // for box filter, but the tight sx scan still benefits from auto-SIMD
                    // on wide horizontal runs when count is large.
                    for (int c = 0; c < bpp; ++c)
                        sum[c] += p[c];
                    ++count;
                }
            }
            if (count < 1)
                count = 1;

            std::uint8_t* out = dst
                + (static_cast<std::size_t>(y) * static_cast<std::size_t>(newW)
                   + static_cast<std::size_t>(x))
                    * static_cast<std::size_t>(bpp);
            for (int c = 0; c < bpp; ++c)
                out[c] = static_cast<std::uint8_t>(sum[c] / static_cast<std::uint32_t>(count));
        }
    }

    RL_FREE(image.data);
    image.data = dst;
    image.width = newW;
    image.height = newH;
    image.mipmaps = 1;
    return true;
}

} // namespace timberline_engine
