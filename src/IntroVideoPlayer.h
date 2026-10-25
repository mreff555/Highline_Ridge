/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Boot intro video (muted). macOS uses AVFoundation; other platforms stub out.
 ******************************************************************************/

#ifndef INTRO_VIDEO_PLAYER_H
#define INTRO_VIDEO_PLAYER_H

#include <raylib.h>
#include <string>

namespace timberline_engine
{

/**
 * Fullscreen cover-scaled intro clip. Audio is intentionally unused — title
 * music is owned by AudioManager so it can continue uninterrupted.
 */
class IntroVideoPlayer
{
public:
    IntroVideoPlayer() = default;
    ~IntroVideoPlayer() { close(); }

    IntroVideoPlayer(const IntroVideoPlayer&) = delete;
    IntroVideoPlayer& operator=(const IntroVideoPlayer&) = delete;

    /** Open and start muted playback. Returns false if unavailable / missing. */
    bool open(const std::string& absoluteOrRelativePath);
    void close();

    void update(float deltaSeconds);
    void draw(int screenWidth, int screenHeight) const;

    bool isOpen() const { return open_; }
    bool finished() const { return open_ && finished_; }
    float durationSeconds() const { return durationSec_; }
    float timeSeconds() const { return timeSec_; }

private:
    bool open_ = false;
    bool finished_ = false;
    float durationSec_ = 0.0f;
    float timeSec_ = 0.0f;
    Texture2D texture_{};
    bool textureReady_ = false;
    int videoW_ = 0;
    int videoH_ = 0;

    void* impl_ = nullptr; // platform opaque
};

} // namespace timberline_engine

#endif /* INTRO_VIDEO_PLAYER_H */
