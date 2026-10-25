/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Non-Apple stub — intro video unavailable; boot skips to title.
 ******************************************************************************/

#ifndef __APPLE__

#include "IntroVideoPlayer.h"

namespace timberline_engine
{

bool IntroVideoPlayer::open(const std::string& /*absoluteOrRelativePath*/)
{
    close();
    return false;
}

void IntroVideoPlayer::close()
{
    if (textureReady_)
    {
        UnloadTexture(texture_);
        texture_ = Texture2D{};
        textureReady_ = false;
    }
    impl_ = nullptr;
    open_ = false;
    finished_ = false;
    durationSec_ = 0.0f;
    timeSec_ = 0.0f;
    videoW_ = 0;
    videoH_ = 0;
}

void IntroVideoPlayer::update(float /*deltaSeconds*/) {}

void IntroVideoPlayer::draw(int /*screenWidth*/, int /*screenHeight*/) const {}

} // namespace timberline_engine

#endif /* !__APPLE__ */
