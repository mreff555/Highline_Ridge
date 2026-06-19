#include "AudioManager.h"
#include "ImageCompression.h"

#include <algorithm>
#include <cctype>

namespace testgame
{

namespace
{

std::string fileExtensionFromPath(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "";

    std::string extension = path.substr(dot);
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

std::string stripXzSuffix(const std::string& path)
{
    static const char kSuffix[] = ".xz";
    if (path.size() >= sizeof(kSuffix) - 1 &&
        path.compare(path.size() - (sizeof(kSuffix) - 1), sizeof(kSuffix) - 1, kSuffix) == 0)
    {
        return path.substr(0, path.size() - (sizeof(kSuffix) - 1));
    }

    return path;
}

float attributeOrDefault(const AudioClipDef& clip, const char* key, float fallback)
{
    std::map<std::string, float>::const_iterator it = clip.numericAttributes.find(key);
    if (it != clip.numericAttributes.end())
        return it->second;
    return fallback;
}

}

AudioManager::AudioManager() = default;

AudioManager::~AudioManager()
{
    shutdown();
}

bool AudioManager::initialize(const std::string& root, const AudioVolumeConfig& volumeConfig)
{
    assetRoot = root;
    volumes = volumeConfig;

    if (!deviceReady)
    {
        InitAudioDevice();
        deviceReady = IsAudioDeviceReady();
        if (!deviceReady)
        {
            TraceLog(LOG_WARNING, "Audio device failed to initialize");
            return false;
        }
    }

    SetMasterVolume(volumes.master);
    return true;
}

void AudioManager::shutdown()
{
    unloadMusicTrack(musicTrack);
    unloadAmbientTracks();

    for (ActiveSound& activeSound : activeSounds)
    {
        if (activeSound.loaded)
            UnloadSound(activeSound.sound);
    }
    activeSounds.clear();

    if (deviceReady)
    {
        CloseAudioDevice();
        deviceReady = false;
    }
}

void AudioManager::setVolumes(const AudioVolumeConfig& volumeConfig)
{
    volumes = volumeConfig;
    if (deviceReady)
        SetMasterVolume(volumes.master);
}

float AudioManager::effectiveVolume(AudioCategory category, float clipVolume) const
{
    float categoryVolume = 1.0f;
    switch (category)
    {
        case AudioCategory::Music:
            categoryVolume = volumes.music;
            break;
        case AudioCategory::Ambient:
            categoryVolume = volumes.ambient;
            break;
        case AudioCategory::Sfx:
            categoryVolume = volumes.sfx;
            break;
    }

    return std::max(0.0f, std::min(1.0f, clipVolume * categoryVolume));
}

bool AudioManager::resolveAssetBytes(const std::string& relativePath, std::vector<unsigned char>& outBytes) const
{
    const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, relativePath);
    for (const std::string& path : paths)
    {
        if (FileExists(path.c_str()) && loadAssetBytesFromFile(path, outBytes))
            return true;

        const std::string compressedPath = compressedAssetPath(path);
        if (FileExists(compressedPath.c_str()) && loadAssetBytesFromFile(compressedPath, outBytes))
            return true;
    }

    return false;
}

bool AudioManager::loadMusicClip(const std::string& path, Music& outMusic) const
{
    std::vector<unsigned char> bytes;
    if (!resolveAssetBytes(path, bytes) || bytes.empty())
        return false;

    const std::string extension = fileExtensionFromPath(stripXzSuffix(path));
    if (extension != ".mp3")
    {
        TraceLog(LOG_WARNING, "Unsupported music format for '%s' (expected .mp3)", path.c_str());
        return false;
    }

    outMusic = LoadMusicStreamFromMemory(
        extension.c_str(),
        bytes.data(),
        static_cast<int>(bytes.size()));

    return outMusic.frameCount > 0;
}

bool AudioManager::loadSoundClip(const std::string& path, Sound& outSound, float& outDurationSeconds) const
{
    std::vector<unsigned char> bytes;
    if (!resolveAssetBytes(path, bytes) || bytes.empty())
        return false;

    const std::string extension = fileExtensionFromPath(stripXzSuffix(path));
    if (extension != ".mp3")
    {
        TraceLog(LOG_WARNING, "Unsupported sound format for '%s' (expected .mp3)", path.c_str());
        return false;
    }

    Wave wave = LoadWaveFromMemory(
        extension.c_str(),
        bytes.data(),
        static_cast<int>(bytes.size()));
    if (wave.data == nullptr)
        return false;

    outDurationSeconds = (wave.sampleRate > 0)
        ? static_cast<float>(wave.frameCount) / static_cast<float>(wave.sampleRate)
        : 0.0f;

    outSound = LoadSoundFromWave(wave);
    const bool loaded = outSound.frameCount > 0;
    UnloadWave(wave);
    return loaded;
}

void AudioManager::unloadMusicTrack(FadingMusicTrack& track)
{
    if (!track.loaded)
        return;

    if (track.playing)
        StopMusicStream(track.music);

    UnloadMusicStream(track.music);
    track = FadingMusicTrack{};
}

void AudioManager::unloadAmbientTracks()
{
    for (FadingMusicTrack& track : ambientTracks)
        unloadMusicTrack(track);

    ambientTracks.clear();
}

void AudioManager::fadeOutMusicTrack(FadingMusicTrack& track, float fadeOutSeconds)
{
    if (!track.loaded || !track.playing)
        return;

    track.fadeOutSeconds = std::max(0.0f, fadeOutSeconds);
    track.fadeElapsed = 0.0f;
    track.fadingOut = track.fadeOutSeconds > 0.0f;
    track.fadingIn = false;

    if (!track.fadingOut)
    {
        StopMusicStream(track.music);
        track.playing = false;
        track.currentVolume = 0.0f;
    }
}

void AudioManager::fadeOutAmbientTracks(float fadeOutSeconds)
{
    for (FadingMusicTrack& track : ambientTracks)
        fadeOutMusicTrack(track, fadeOutSeconds);
}

void AudioManager::startMusicTrack(FadingMusicTrack& track, const AudioClipDef& clip)
{
    unloadMusicTrack(track);

    if (clip.path.empty())
        return;

    Music music{};
    if (!loadMusicClip(clip.path, music))
    {
        TraceLog(LOG_WARNING, "Failed to load music clip: %s", clip.path.c_str());
        return;
    }

    track.music = music;
    track.loaded = true;
    track.path = clip.path;
    track.targetVolume = effectiveVolume(AudioCategory::Music, clip.volume);
    track.fadeInSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_in", clip.fadeIn));
    track.fadeOutSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_out", clip.fadeOut));
    track.fadeElapsed = 0.0f;
    track.fadingIn = track.fadeInSeconds > 0.0f;
    track.fadingOut = false;
    track.currentVolume = track.fadingIn ? 0.0f : track.targetVolume;

    SetMusicVolume(track.music, track.currentVolume);
    PlayMusicStream(track.music);
    track.playing = true;
}

void AudioManager::startAmbientTrack(const AudioClipDef& clip)
{
    if (clip.path.empty())
        return;

    FadingMusicTrack track;
    Music music{};
    if (!loadMusicClip(clip.path, music))
    {
        TraceLog(LOG_WARNING, "Failed to load ambient clip: %s", clip.path.c_str());
        return;
    }

    track.music = music;
    track.loaded = true;
    track.path = clip.path;
    track.targetVolume = effectiveVolume(AudioCategory::Ambient, clip.volume);
    track.fadeInSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_in", clip.fadeIn));
    track.fadeOutSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_out", clip.fadeOut));
    track.fadeElapsed = 0.0f;
    track.fadingIn = track.fadeInSeconds > 0.0f;
    track.fadingOut = false;
    track.currentVolume = track.fadingIn ? 0.0f : track.targetVolume;

    SetMusicVolume(track.music, track.currentVolume);
    PlayMusicStream(track.music);
    track.playing = true;
    ambientTracks.push_back(track);
}

void AudioManager::updateMusicTrack(FadingMusicTrack& track, float deltaSeconds, AudioCategory category)
{
    if (!track.loaded)
        return;

    if (track.playing)
        UpdateMusicStream(track.music);

    if (track.fadingIn)
    {
        track.fadeElapsed += deltaSeconds;
        if (track.fadeInSeconds <= 0.0f)
        {
            track.currentVolume = track.targetVolume;
            track.fadingIn = false;
        }
        else
        {
            const float progress = std::min(1.0f, track.fadeElapsed / track.fadeInSeconds);
            track.currentVolume = track.targetVolume * progress;
            if (progress >= 1.0f)
                track.fadingIn = false;
        }
    }
    else if (track.fadingOut)
    {
        track.fadeElapsed += deltaSeconds;
        if (track.fadeOutSeconds <= 0.0f)
        {
            track.currentVolume = 0.0f;
            track.fadingOut = false;
            StopMusicStream(track.music);
            track.playing = false;
        }
        else
        {
            const float progress = std::min(1.0f, track.fadeElapsed / track.fadeOutSeconds);
            const float startVolume = track.targetVolume;
            track.currentVolume = startVolume * (1.0f - progress);
            if (progress >= 1.0f)
            {
                track.fadingOut = false;
                StopMusicStream(track.music);
                track.playing = false;
            }
        }
    }
    else if (track.playing)
    {
        track.currentVolume = track.targetVolume;
    }

    if (track.playing || track.fadingOut)
        SetMusicVolume(track.music, track.currentVolume);

    (void)category;
}

void AudioManager::updateActiveSounds(float deltaSeconds)
{
    std::vector<ActiveSound> remainingSounds;
    remainingSounds.reserve(activeSounds.size());

    for (ActiveSound& activeSound : activeSounds)
    {
        if (!activeSound.loaded)
            continue;

        activeSound.remainingSeconds -= deltaSeconds;
        if (activeSound.remainingSeconds > 0.0f)
        {
            remainingSounds.push_back(activeSound);
            continue;
        }

        UnloadSound(activeSound.sound);
    }

    activeSounds.swap(remainingSounds);
}

void AudioManager::update(float deltaSeconds)
{
    if (!deviceReady)
        return;

    updateActiveSounds(deltaSeconds);
    updateMusicTrack(musicTrack, deltaSeconds, AudioCategory::Music);

    std::vector<FadingMusicTrack> remainingAmbient;
    remainingAmbient.reserve(ambientTracks.size());

    for (FadingMusicTrack& track : ambientTracks)
    {
        updateMusicTrack(track, deltaSeconds, AudioCategory::Ambient);
        if (track.loaded && (track.playing || track.fadingIn || track.fadingOut))
            remainingAmbient.push_back(track);
        else
            unloadMusicTrack(track);
    }

    ambientTracks.swap(remainingAmbient);

    if (roomTransitionPending && !hasActiveStreamAudio())
    {
        applyRoomAudio(pendingRoomAudio);
        roomTransitionPending = false;
    }
}

void AudioManager::playSfx(const AudioClipDef& clip)
{
    if (!deviceReady || clip.path.empty())
        return;

    Sound sound{};
    float durationSeconds = 0.0f;
    if (!loadSoundClip(clip.path, sound, durationSeconds))
    {
        TraceLog(LOG_WARNING, "Failed to load sfx clip: %s", clip.path.c_str());
        return;
    }

    const float volume = effectiveVolume(AudioCategory::Sfx, clip.volume);
    SetSoundVolume(sound, volume);
    PlaySound(sound);

    ActiveSound activeSound;
    activeSound.sound = sound;
    activeSound.loaded = true;
    activeSound.remainingSeconds = std::max(0.05f, durationSeconds);
    activeSounds.push_back(activeSound);
}

bool AudioManager::hasActiveStreamAudio() const
{
    if (musicTrack.loaded && (musicTrack.playing || musicTrack.fadingOut))
        return true;

    for (const FadingMusicTrack& track : ambientTracks)
    {
        if (track.loaded && (track.playing || track.fadingOut))
            return true;
    }

    return false;
}

void AudioManager::applyRoomAudio(const RoomAudioConfig& roomAudio)
{
    if (roomAudio.hasMusic)
        startMusicTrack(musicTrack, roomAudio.music);
    else
        unloadMusicTrack(musicTrack);

    unloadAmbientTracks();
    for (const AudioClipDef& ambientClip : roomAudio.ambient)
        startAmbientTrack(ambientClip);

    for (const AudioClipDef& sfxClip : roomAudio.sfx)
    {
        const std::string trigger = sfxClip.trigger.empty() ? "on_enter" : sfxClip.trigger;
        if (trigger == "on_enter")
            playSfx(sfxClip);
    }
}

void AudioManager::onRoomEnter(const RoomAudioConfig& roomAudio)
{
    if (hasActiveStreamAudio())
    {
        fadeOutMusicTrack(musicTrack, musicTrack.fadeOutSeconds);
        float ambientFadeOut = 0.0f;
        for (const FadingMusicTrack& track : ambientTracks)
            ambientFadeOut = std::max(ambientFadeOut, track.fadeOutSeconds);
        fadeOutAmbientTracks(ambientFadeOut);

        pendingRoomAudio = roomAudio;
        roomTransitionPending = true;
        return;
    }

    applyRoomAudio(roomAudio);
}

}