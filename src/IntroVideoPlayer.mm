/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * macOS intro video via AVFoundation (video track only — muted).
 * Compiled with -fobjc-arc (see CMakeLists.txt).
 ******************************************************************************/

#ifdef __APPLE__

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>

#include "IntroVideoPlayer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace timberline_engine
{
namespace
{

struct IntroVideoImpl
{
    __strong AVPlayer* player = nil;
    __strong AVPlayerItem* item = nil;
    __strong AVPlayerItemVideoOutput* output = nil;
    bool itemFailed = false;
};

void destroyImpl(IntroVideoImpl*& impl)
{
    if (impl == nullptr)
        return;
    if (impl->player != nil)
        [impl->player pause];
    impl->player = nil;
    impl->item = nil;
    impl->output = nil;
    delete impl;
    impl = nullptr;
}

} // namespace

bool IntroVideoPlayer::open(const std::string& absoluteOrRelativePath)
{
    close();
    if (absoluteOrRelativePath.empty())
        return false;

    @autoreleasepool
    {
        NSString* path = [NSString stringWithUTF8String:absoluteOrRelativePath.c_str()];
        if (path == nil || path.length == 0)
            return false;
        if (![path isAbsolutePath])
        {
            NSString* cwd = [[NSFileManager defaultManager] currentDirectoryPath];
            path = [cwd stringByAppendingPathComponent:path];
        }
        path = [path stringByStandardizingPath];
        NSURL* url = [NSURL fileURLWithPath:path isDirectory:NO];
        if (url == nil || ![[NSFileManager defaultManager] fileExistsAtPath:path])
            return false;

        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
        if (asset == nil)
            return false;

        // Prefer modern async track load when available; sync path still works.
        NSArray<AVAssetTrack*>* videoTracks = nil;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
#pragma clang diagnostic pop
        if (videoTracks.count == 0)
            return false;

        AVAssetTrack* track = videoTracks.firstObject;
        CGSize natural = track.naturalSize;
        CGAffineTransform transform = track.preferredTransform;
        CGSize display = CGSizeApplyAffineTransform(natural, transform);
        videoW_ = static_cast<int>(std::lround(std::fabs(display.width)));
        videoH_ = static_cast<int>(std::lround(std::fabs(display.height)));
        if (videoW_ <= 0 || videoH_ <= 0)
        {
            videoW_ = static_cast<int>(std::lround(std::fabs(natural.width)));
            videoH_ = static_cast<int>(std::lround(std::fabs(natural.height)));
        }
        if (videoW_ <= 0 || videoH_ <= 0)
        {
            videoW_ = 1920;
            videoH_ = 1080;
        }

        const CMTime dur = asset.duration;
        if (CMTIME_IS_NUMERIC(dur) && dur.timescale > 0)
            durationSec_ = static_cast<float>(CMTimeGetSeconds(dur));
        else
            durationSec_ = 10.0f;

        NSDictionary* attrs = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
        };
        AVPlayerItemVideoOutput* output =
            [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attrs];
        AVPlayerItem* item = [AVPlayerItem playerItemWithAsset:asset];
        [item addOutput:output];

        AVPlayer* player = [AVPlayer playerWithPlayerItem:item];
        player.muted = YES;
        player.volume = 0.0f;
        player.actionAtItemEnd = AVPlayerActionAtItemEndPause;

        IntroVideoImpl* impl = new IntroVideoImpl();
        impl->player = player;
        impl->item = item;
        impl->output = output;
        impl->itemFailed = false;

        // Force RGBA texture so UpdateTexture matches 4-byte pixels.
        Image blank = GenImageColor(videoW_, videoH_, BLANK);
        ImageFormat(&blank, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        texture_ = LoadTextureFromImage(blank);
        UnloadImage(blank);
        if (texture_.id == 0)
        {
            destroyImpl(impl);
            return false;
        }
        textureReady_ = true;
        SetTextureFilter(texture_, TEXTURE_FILTER_BILINEAR);

        impl_ = impl;
        open_ = true;
        finished_ = false;
        timeSec_ = 0.0f;

        [player play];
        return true;
    }
}

void IntroVideoPlayer::close()
{
    IntroVideoImpl* impl = static_cast<IntroVideoImpl*>(impl_);
    destroyImpl(impl);
    impl_ = nullptr;

    if (textureReady_)
    {
        UnloadTexture(texture_);
        texture_ = Texture2D{};
        textureReady_ = false;
    }
    open_ = false;
    finished_ = false;
    durationSec_ = 0.0f;
    timeSec_ = 0.0f;
    videoW_ = 0;
    videoH_ = 0;
}

void IntroVideoPlayer::update(float /*deltaSeconds*/)
{
    if (!open_ || impl_ == nullptr)
        return;

    IntroVideoImpl* impl = static_cast<IntroVideoImpl*>(impl_);
    @autoreleasepool
    {
        if (impl->itemFailed)
        {
            finished_ = true;
            return;
        }
        if (impl->item != nil && impl->item.status == AVPlayerItemStatusFailed)
        {
            impl->itemFailed = true;
            finished_ = true;
            return;
        }

        if (impl->player == nil || impl->output == nil)
        {
            finished_ = true;
            return;
        }

        // Wait until the item can supply frames.
        if (impl->item != nil && impl->item.status != AVPlayerItemStatusReadyToPlay
            && impl->item.status != AVPlayerItemStatusUnknown)
        {
            return;
        }

        const CMTime t = impl->player.currentTime;
        if (CMTIME_IS_NUMERIC(t) && t.timescale > 0)
            timeSec_ = static_cast<float>(CMTimeGetSeconds(t));

        if (durationSec_ > 0.05f && timeSec_ >= durationSec_ - 0.02f)
        {
            finished_ = true;
            return;
        }

        // Query by player item time (not host clock) — safer with AVPlayerItemVideoOutput.
        CMTime itemTime = t;
        if (!CMTIME_IS_NUMERIC(itemTime) || itemTime.timescale <= 0)
            return;
        if (![impl->output hasNewPixelBufferForItemTime:itemTime])
            return;

        CVPixelBufferRef pixelBuffer =
            [impl->output copyPixelBufferForItemTime:itemTime itemTimeForDisplay:nil];
        if (pixelBuffer == nullptr)
            return;

        if (CVPixelBufferGetPixelFormatType(pixelBuffer) != kCVPixelFormatType_32BGRA)
        {
            CVPixelBufferRelease(pixelBuffer);
            return;
        }

        CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        const size_t width = CVPixelBufferGetWidth(pixelBuffer);
        const size_t height = CVPixelBufferGetHeight(pixelBuffer);
        const size_t stride = CVPixelBufferGetBytesPerRow(pixelBuffer);
        void* base = CVPixelBufferGetBaseAddress(pixelBuffer);
        if (base != nullptr && width > 0 && height > 0 && textureReady_ && stride >= width * 4)
        {
            std::vector<unsigned char> rgba(static_cast<size_t>(width * height * 4));
            const unsigned char* srcRow = static_cast<const unsigned char*>(base);
            for (size_t y = 0; y < height; ++y)
            {
                const unsigned char* src = srcRow + y * stride;
                unsigned char* dst = rgba.data() + y * width * 4;
                for (size_t x = 0; x < width; ++x)
                {
                    dst[x * 4 + 0] = src[x * 4 + 2]; // R
                    dst[x * 4 + 1] = src[x * 4 + 1]; // G
                    dst[x * 4 + 2] = src[x * 4 + 0]; // B
                    dst[x * 4 + 3] = src[x * 4 + 3]; // A
                }
            }

            if (static_cast<int>(width) != texture_.width
                || static_cast<int>(height) != texture_.height)
            {
                UnloadTexture(texture_);
                Image img = {
                    rgba.data(),
                    static_cast<int>(width),
                    static_cast<int>(height),
                    1,
                    PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                texture_ = LoadTextureFromImage(img);
                // Do not UnloadImage — data owned by vector.
                if (texture_.id != 0)
                    SetTextureFilter(texture_, TEXTURE_FILTER_BILINEAR);
                videoW_ = static_cast<int>(width);
                videoH_ = static_cast<int>(height);
            }
            else
            {
                UpdateTexture(texture_, rgba.data());
            }
        }
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(pixelBuffer);
    }
}

void IntroVideoPlayer::draw(int screenWidth, int screenHeight) const
{
    ClearBackground(BLACK);
    if (!textureReady_ || texture_.id == 0 || screenWidth <= 0 || screenHeight <= 0)
        return;

    const float tw = static_cast<float>(texture_.width);
    const float th = static_cast<float>(texture_.height);
    if (tw < 1.0f || th < 1.0f)
        return;

    const float scale = std::max(
        static_cast<float>(screenWidth) / tw,
        static_cast<float>(screenHeight) / th);
    const float dw = tw * scale;
    const float dh = th * scale;
    const float dx = (static_cast<float>(screenWidth) - dw) * 0.5f;
    const float dy = (static_cast<float>(screenHeight) - dh) * 0.5f;
    DrawTexturePro(
        texture_,
        Rectangle{0, 0, tw, th},
        Rectangle{dx, dy, dw, dh},
        Vector2{0, 0},
        0.0f,
        WHITE);
}

} // namespace timberline_engine

#endif /* __APPLE__ */
