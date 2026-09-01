/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#include "AudioManager.h"
#include "AssetStore.h"
#include "ImageCompression.h"
#include "JobSystem.h"
#include "OpusAudio.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_set>

namespace timberline_engine
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

std::string stringAttributeOrEmpty(const AudioClipDef& clip, const char* key)
{
    std::map<std::string, std::string>::const_iterator it = clip.stringAttributes.find(key);
    if (it != clip.stringAttributes.end())
        return it->second;
    return "";
}

bool matchesTransitionConstraint(
    const std::string& requiredRoom,
    const std::string& actualRoom)
{
    return requiredRoom.empty() || requiredRoom == actualRoom;
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

    deviceReady = IsAudioDeviceReady();
    if (deviceReady)
        SetMasterVolume(volumes.master);

    return true;
}

bool AudioManager::ensureDeviceReady()
{
    if (deviceReady)
        return true;

    InitAudioDevice();
    deviceReady = IsAudioDeviceReady();
    if (!deviceReady)
    {
        TraceLog(LOG_WARNING, "Audio device failed to initialize");
        return false;
    }

    SetMasterVolume(volumes.master);
    return true;
}

void AudioManager::shutdown()
{
    stopDialog();
    clearItemExamineAudio();
    unloadMusicTrack(musicTrack);
    unloadAmbientTracks();

    for (std::pair<const std::string, CachedAmbientSample>& cachedSample : ambientSampleCache)
    {
        if (cachedSample.second.loaded)
            UnloadSound(cachedSample.second.sound);
        removeTempFile(cachedSample.second.tempFilePath);
    }
    ambientSampleCache.clear();

    for (ActiveSound& activeSound : activeSounds)
    {
        if (activeSound.loaded)
            UnloadSound(activeSound.sound);
        removeTempFile(activeSound.tempFilePath);
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

    refreshCategoryVolumes();
}

void AudioManager::setGameplayPaused(bool paused, float fadeSeconds)
{
    gameplayPaused = paused;
    gameplayMixTarget = paused ? 0.0f : 1.0f;
    gameplayMixFadeRate = fadeSeconds > 0.0f ? (1.0f / fadeSeconds) : 1000.0f;
}

float AudioManager::appliedVolume(float baseVolume) const
{
    return baseVolume * gameplayMix;
}

void AudioManager::updateGameplayMix(float deltaSeconds)
{
    if (gameplayMix < gameplayMixTarget)
        gameplayMix = std::min(gameplayMixTarget, gameplayMix + gameplayMixFadeRate * deltaSeconds);
    else if (gameplayMix > gameplayMixTarget)
        gameplayMix = std::max(gameplayMixTarget, gameplayMix - gameplayMixFadeRate * deltaSeconds);
}

void AudioManager::refreshCategoryVolumes()
{
    if (musicTrack.loaded)
    {
        musicTrack.targetVolume = effectiveVolume(AudioCategory::Music, musicTrack.sourceClipVolume);
        if (musicTrack.playing && !musicTrack.fadingIn && !musicTrack.fadingOut)
            musicTrack.currentVolume = musicTrack.targetVolume;
    }

    for (FadingAmbientTrack& track : ambientTracks)
    {
        if (!track.loaded)
            continue;

        track.targetVolume = effectiveVolume(AudioCategory::Ambient, track.sourceClipVolume);
        if (track.playing && !track.fadingIn && !track.fadingOut)
            track.currentVolume = track.targetVolume;
    }
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

void AudioManager::removeTempFile(const std::string& path)
{
    if (path.empty())
        return;

    if (path.compare(0, 5, "/tmp/") == 0)
        std::remove(path.c_str());
}

bool AudioManager::resolveMusicAssetFile(
    const std::string& relativePath,
    std::string& outPlayablePath,
    std::string& outTempFile) const
{
    outPlayablePath.clear();
    outTempFile.clear();

    const std::vector<std::string> paths = buildAssetSearchPaths(assetRoot, relativePath);
    for (const std::string& path : paths)
    {
        if (FileExists(path.c_str()))
        {
            outPlayablePath = path;
            return true;
        }

        const std::string compressedPath = compressedAssetPath(path);
        if (!FileExists(compressedPath.c_str()))
            continue;

        std::vector<unsigned char> bytes;
        if (!loadAssetBytesFromFile(compressedPath, bytes) || bytes.empty())
            continue;

        std::ostringstream tempName;
        tempName << "/tmp/timberline_engine_audio_" << GetRandomValue(100000, 999999) << ".mp3";
        outTempFile = tempName.str();

        std::ofstream tempFile(outTempFile.c_str(), std::ios::binary);
        if (!tempFile.is_open())
            return false;

        tempFile.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!tempFile.good())
        {
            std::remove(outTempFile.c_str());
            outTempFile.clear();
            return false;
        }

        outPlayablePath = outTempFile;
        return true;
    }

    return false;
}

bool AudioManager::preferSyncAudioLoad()
{
    // Always sync for now. Async JobSystem audio decompress raced with repeated
    // onRoomEnter/syncRoomStreams and silenced beds; re-enable behind a flag only
    // after that path is hardened end-to-end.
    return true;
}

bool AudioManager::resolveAssetBytes(const std::string& relativePath, std::vector<unsigned char>& outBytes) const
{
    outBytes.clear();

    // Pak / DiskAssetStore (transparent .xz) — preferred for release + editor.
    {
        AssetBytes packed;
        if (assets().readBytes(relativePath, packed) && !packed.data.empty())
        {
            outBytes.swap(packed.data);
            return true;
        }
    }

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

bool AudioManager::loadMusicClip(
    const std::string& path,
    Music& outMusic,
    std::vector<unsigned char>& outMemoryBytes,
    std::string& outTempFile)
{
    outMusic = Music{};
    outMemoryBytes.clear();
    outTempFile.clear();

    const std::string extension = fileExtensionFromPath(stripXzSuffix(path));
    if (extension != ".mp3")
    {
        TraceLog(LOG_WARNING, "Unsupported music format for '%s' (expected .mp3)", path.c_str());
        return false;
    }

    // Proven path: resolve to a real file (or /tmp extract from .xz) + LoadMusicStream.
    // FromMemory is opt-in — drmp3 keeps a pointer into the buffer and we hit
    // silent-failure cases when mixing that with track lifetime / JobSystem.
    std::string playablePath;
    if (!resolveMusicAssetFile(path, playablePath, outTempFile))
        return false;

    outMusic = LoadMusicStream(playablePath.c_str());
    if (outMusic.stream.buffer == nullptr)
        return false;

    outMusic.looping = true;
    return true;
}

bool AudioManager::loadSoundClip(
    const std::string& path,
    Sound& outSound,
    float& outDurationSeconds,
    std::string& outTempFile)
{
    outSound = Sound{};
    outDurationSeconds = 0.0f;
    outTempFile.clear();

    const std::string extension = fileExtensionFromPath(stripXzSuffix(path));
    if (extension != ".mp3")
    {
        TraceLog(LOG_WARNING, "Unsupported sound format for '%s' (expected .mp3)", path.c_str());
        return false;
    }

    std::string playablePath;
    if (!resolveMusicAssetFile(path, playablePath, outTempFile))
        return false;

    outSound = LoadSound(playablePath.c_str());
    if (outSound.stream.buffer == nullptr)
        return false;

    if (outSound.frameCount > 0 && outSound.stream.sampleRate > 0)
    {
        outDurationSeconds = static_cast<float>(outSound.frameCount)
            / static_cast<float>(outSound.stream.sampleRate);
    }
    if (outDurationSeconds <= 0.0f)
        outDurationSeconds = 0.2f;

    return true;
}

bool AudioManager::loadDialogClip(
    const std::string& path,
    Sound& outSound,
    float& outDurationSeconds)
{
    outSound = Sound{};
    outDurationSeconds = 0.0f;

    const std::string extension = fileExtensionFromPath(stripXzSuffix(path));
    if (extension != ".opus")
    {
        TraceLog(LOG_WARNING, "Unsupported dialog format for '%s' (expected .opus)", path.c_str());
        return false;
    }

    std::vector<unsigned char> bytes;
    if (!resolveAssetBytes(path, bytes) || bytes.empty())
        return false;

    Wave wave = {};
    if (!decodeOpusBytesToWave(bytes.data(), static_cast<int>(bytes.size()), wave))
        return false;

    outSound = LoadSoundFromWave(wave);
    unloadDecodedWave(wave);
    if (outSound.stream.buffer == nullptr)
        return false;

    if (outSound.frameCount > 0 && outSound.stream.sampleRate > 0)
    {
        outDurationSeconds = static_cast<float>(outSound.frameCount)
            / static_cast<float>(outSound.stream.sampleRate);
    }
    if (outDurationSeconds <= 0.0f)
        outDurationSeconds = 0.2f;

    return true;
}

bool AudioManager::acquireAmbientSound(const std::string& path, Sound& outSound, bool& outUsesAlias)
{
    outSound = Sound{};
    outUsesAlias = false;

    if (!ensureAmbientSampleLoadedSync(path))
        return false;

    CachedAmbientSample& cachedSample = ambientSampleCache[path];
    outSound = LoadSoundAlias(cachedSample.sound);
    if (outSound.frameCount == 0)
        return false;

    outUsesAlias = true;
    return true;
}

void AudioManager::releaseAmbientSound(const std::string& path, Sound& sound, bool usesAlias)
{
    if (!sound.frameCount)
        return;

    if (usesAlias)
        UnloadSoundAlias(sound);
    else
        UnloadSound(sound);

    sound = Sound{};
    (void)path;
}

void AudioManager::unloadMusicTrack(FadingMusicTrack& track)
{
    if (!track.loaded && track.memoryBytes.empty() && track.tempFilePath.empty())
        return;

    if (track.loaded)
    {
        if (track.playing)
            StopMusicStream(track.music);
        UnloadMusicStream(track.music);
    }
    removeTempFile(track.tempFilePath);
    // Free retained MP3 bytes only after UnloadMusicStream (drmp3 pointed at them).
    track = FadingMusicTrack{};
}

void AudioManager::unloadAmbientTrack(FadingAmbientTrack& track)
{
    if (!track.loaded)
        return;

    if (track.playing)
        StopSound(track.sound);

    releaseAmbientSound(track.path, track.sound, track.usesCachedAlias);
    track = FadingAmbientTrack{};
}

void AudioManager::unloadAmbientTracks()
{
    for (FadingAmbientTrack& track : ambientTracks)
        unloadAmbientTrack(track);

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

void AudioManager::fadeOutAmbientTrack(FadingAmbientTrack& track, float fadeOutSeconds)
{
    if (!track.loaded || !track.playing)
        return;

    track.fadeOutSeconds = std::max(0.0f, fadeOutSeconds);
    track.fadeElapsed = 0.0f;
    track.fadingOut = track.fadeOutSeconds > 0.0f;
    track.fadingIn = false;

    if (!track.fadingOut)
    {
        StopSound(track.sound);
        track.playing = false;
        track.currentVolume = 0.0f;
    }
}

void AudioManager::applyMusicFromMemory(
    FadingMusicTrack& track,
    const AudioClipDef& clip,
    std::vector<unsigned char>&& bytes)
{
    // Prefer writing a temp file + LoadMusicStream — same as the stable sync path.
    (void)bytes;
    startMusicTrackSync(track, clip);
}

void AudioManager::startMusicTrackSync(FadingMusicTrack& track, const AudioClipDef& clip)
{
    unloadMusicTrack(track);

    if (clip.path.empty() || !ensureDeviceReady())
        return;

    Music music{};
    std::vector<unsigned char> memoryBytes;
    std::string tempFile;
    if (!loadMusicClip(clip.path, music, memoryBytes, tempFile))
    {
        TraceLog(LOG_WARNING, "Failed to load music clip: %s", clip.path.c_str());
        return;
    }

    track.music = music;
    track.loaded = true;
    track.path = clip.path;
    track.memoryBytes = std::move(memoryBytes);
    track.tempFilePath = tempFile;
    track.sourceClipVolume = clip.volume;
    track.targetVolume = effectiveVolume(AudioCategory::Music, clip.volume);
    track.fadeInSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_in", clip.fadeIn));
    track.fadeOutSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_out", clip.fadeOut));
    track.fadeElapsed = 0.0f;
    track.fadingIn = track.fadeInSeconds > 0.0f;
    track.fadingOut = false;
    track.currentVolume = track.fadingIn ? 0.0f : track.targetVolume;
    track.loop = clip.loop;
    track.music.looping = clip.loop;

    SetMusicVolume(track.music, appliedVolume(track.currentVolume));
    PlayMusicStream(track.music);
    track.playing = true;
    TraceLog(LOG_INFO, "Started music: %s", clip.path.c_str());
}

void AudioManager::startMusicTrack(FadingMusicTrack& track, const AudioClipDef& clip)
{
    if (clip.path.empty())
    {
        unloadMusicTrack(track);
        pendingMusicLoadPath.clear();
        return;
    }
    if (!ensureDeviceReady())
        return;

    if (preferSyncAudioLoad())
    {
        pendingMusicLoadPath.clear();
        startMusicTrackSync(track, clip);
        return;
    }

    // Already fetching this exact bed — don't cancel ourselves.
    if (pendingMusicLoadPath == clip.path && !track.loaded
        && (&track == &musicTrack || &track == &itemMusicTrack))
    {
        return;
    }

    // Fade/stop current, then decompress on a worker; stream on main when ready.
    unloadMusicTrack(track);
    const std::uint64_t generation = ++musicLoadGeneration;
    pendingMusicLoadPath = clip.path;
    const std::string root = assetRoot;
    const AudioClipDef clipCopy = clip;
    FadingMusicTrack* trackPtr = &track;

    struct DecodeResult
    {
        std::uint64_t generation = 0;
        std::string path;
        std::string assetRoot;
        std::vector<unsigned char> bytes;
        bool ok = false;
    };
    auto result = std::make_shared<DecodeResult>();
    result->generation = generation;
    result->path = clip.path;
    result->assetRoot = root;

    JobSystem::global().enqueue(
        [result]() {
            AssetBytes packed;
            if (assets().readBytes(result->path, packed) && !packed.data.empty())
            {
                result->bytes.swap(packed.data);
                result->ok = true;
                return;
            }
            const std::vector<std::string> paths =
                buildAssetSearchPaths(result->assetRoot, result->path);
            for (const std::string& diskPath : paths)
            {
                if (FileExists(diskPath.c_str())
                    && loadAssetBytesFromFile(diskPath, result->bytes))
                {
                    result->ok = true;
                    return;
                }
                const std::string compressed = compressedAssetPath(diskPath);
                if (FileExists(compressed.c_str())
                    && loadAssetBytesFromFile(compressed, result->bytes))
                {
                    result->ok = true;
                    return;
                }
            }
        },
        [this, result, clipCopy, trackPtr]() {
            if (result->generation != musicLoadGeneration)
                return;
            if (trackPtr != &musicTrack && trackPtr != &itemMusicTrack)
                return;
            pendingMusicLoadPath.clear();
            if (!result->ok || result->bytes.empty())
            {
                TraceLog(
                    LOG_WARNING,
                    "Async music decode failed: %s — falling back to sync",
                    clipCopy.path.c_str());
                startMusicTrackSync(*trackPtr, clipCopy);
                return;
            }
            applyMusicFromMemory(*trackPtr, clipCopy, std::move(result->bytes));
        });
}

bool AudioManager::ensureAmbientSampleLoadedSync(const std::string& path)
{
    CachedAmbientSample& cachedSample = ambientSampleCache[path];
    if (cachedSample.loaded)
        return true;

    float durationSeconds = 0.0f;
    if (!loadSoundClip(path, cachedSample.sound, durationSeconds, cachedSample.tempFilePath))
        return false;

    cachedSample.loaded = true;
    return true;
}

void AudioManager::startAmbientTrackSync(const AudioClipDef& clip)
{
    if (clip.path.empty() || !ensureDeviceReady())
        return;

    FadingAmbientTrack track;
    Sound sound{};
    bool usesAlias = false;
    if (!acquireAmbientSound(clip.path, sound, usesAlias))
    {
        TraceLog(LOG_WARNING, "Failed to load ambient clip: %s", clip.path.c_str());
        return;
    }

    track.sound = sound;
    track.loaded = true;
    track.path = clip.path;
    track.usesCachedAlias = usesAlias;
    track.sourceClipVolume = clip.volume;
    track.targetVolume = effectiveVolume(AudioCategory::Ambient, clip.volume);
    track.fadeInSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_in", clip.fadeIn));
    track.fadeOutSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_out", clip.fadeOut));
    track.fadeElapsed = 0.0f;
    track.fadingIn = track.fadeInSeconds > 0.0f;
    track.fadingOut = false;
    track.currentVolume = track.fadingIn ? 0.0f : track.targetVolume;
    track.loop = clip.loop;

    SetSoundVolume(track.sound, appliedVolume(track.currentVolume));
    PlaySound(track.sound);
    track.playing = true;
    ambientTracks.push_back(track);
    TraceLog(LOG_INFO, "Started ambient: %s (vol %.2f)", clip.path.c_str(), track.currentVolume);
}

void AudioManager::startAmbientTrack(const AudioClipDef& clip)
{
    if (clip.path.empty() || !ensureDeviceReady())
        return;

    // Already decoded — start immediately.
    if (ambientSampleCache.count(clip.path) && ambientSampleCache[clip.path].loaded)
    {
        startAmbientTrackSync(clip);
        return;
    }

    if (preferSyncAudioLoad())
    {
        startAmbientTrackSync(clip);
        return;
    }

    // If a decode is already running for this path, keep it — do not drop the
    // request when syncRoomStreams bumps ambientLoadGeneration.
    if (pendingAmbientLoads.count(clip.path) > 0)
        return;

    pendingAmbientLoads.insert(clip.path);
    const std::string root = assetRoot;
    const AudioClipDef clipCopy = clip;

    struct DecodeResult
    {
        std::string path;
        std::string assetRoot;
        std::vector<unsigned char> bytes;
        bool ok = false;
    };
    auto result = std::make_shared<DecodeResult>();
    result->path = clip.path;
    result->assetRoot = root;

    JobSystem::global().enqueue(
        [result]() {
            AssetBytes packed;
            if (assets().readBytes(result->path, packed) && !packed.data.empty())
            {
                result->bytes.swap(packed.data);
                result->ok = true;
                return;
            }
            const std::vector<std::string> paths =
                buildAssetSearchPaths(result->assetRoot, result->path);
            for (const std::string& diskPath : paths)
            {
                if (FileExists(diskPath.c_str())
                    && loadAssetBytesFromFile(diskPath, result->bytes))
                {
                    result->ok = true;
                    return;
                }
                const std::string compressed = compressedAssetPath(diskPath);
                if (FileExists(compressed.c_str())
                    && loadAssetBytesFromFile(compressed, result->bytes))
                {
                    result->ok = true;
                    return;
                }
            }
        },
        [this, result, clipCopy]() {
            pendingAmbientLoads.erase(result->path);
            // Still desired for the *current* room (generation may have moved on
            // if sync ran again with the same ambient list).
            if (desiredAmbientPaths.count(result->path) == 0)
                return;
            if (!result->ok || result->bytes.empty())
            {
                TraceLog(
                    LOG_WARNING,
                    "Async ambient decode failed: %s — trying sync",
                    clipCopy.path.c_str());
                if (desiredAmbientPaths.count(clipCopy.path) > 0
                    && findAmbientTrackByPath(clipCopy.path) == nullptr)
                    startAmbientTrackSync(clipCopy);
                return;
            }

            // Cache + start via the stable file LoadSound path.
            if (!ensureAmbientSampleLoadedSync(result->path))
            {
                TraceLog(
                    LOG_WARNING,
                    "Async ambient upload failed: %s",
                    clipCopy.path.c_str());
                return;
            }

            if (findAmbientTrackByPath(clipCopy.path) == nullptr)
                startAmbientTrackSync(clipCopy);
        });
}

void AudioManager::updateMusicTrack(FadingMusicTrack& track, float deltaSeconds)
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
        SetMusicVolume(track.music, appliedVolume(track.currentVolume));
}

void AudioManager::updateAmbientTrack(FadingAmbientTrack& track, float deltaSeconds)
{
    if (!track.loaded)
        return;

    if (track.loop && track.playing && !track.fadingOut && !IsSoundPlaying(track.sound))
    {
        SetSoundVolume(track.sound, appliedVolume(track.currentVolume));
        PlaySound(track.sound);
    }

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
            StopSound(track.sound);
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
                StopSound(track.sound);
                track.playing = false;
            }
        }
    }
    else if (track.playing)
    {
        track.currentVolume = track.targetVolume;
    }

    if (track.playing || track.fadingOut)
        SetSoundVolume(track.sound, appliedVolume(track.currentVolume));
}

void AudioManager::stopDialog()
{
    for (ActiveSound& queuedClip : dialogQueue)
    {
        if (queuedClip.loaded)
        {
            // Stop first so a mid-line clip is cut off immediately on interrupt/load.
            if (IsSoundPlaying(queuedClip.sound))
                StopSound(queuedClip.sound);
            UnloadSound(queuedClip.sound);
            queuedClip.loaded = false;
        }
        removeTempFile(queuedClip.tempFilePath);
    }

    dialogQueue.clear();
    dialogQueueIndex = 0;
}

void AudioManager::startNextQueuedDialogClip()
{
    while (dialogQueueIndex < dialogQueue.size())
    {
        ActiveSound& queuedClip = dialogQueue[dialogQueueIndex];
        if (!queuedClip.loaded)
        {
            ++dialogQueueIndex;
            continue;
        }

        const float volume = effectiveVolume(AudioCategory::Sfx, dialogClipVolume);
        queuedClip.baseVolume = volume;
        SetSoundVolume(queuedClip.sound, appliedVolume(volume));
        PlaySound(queuedClip.sound);
        return;
    }
}

void AudioManager::updateDialogQueue(float deltaSeconds)
{
    if (dialogQueue.empty() || dialogQueueIndex >= dialogQueue.size())
        return;

    ActiveSound& currentClip = dialogQueue[dialogQueueIndex];
    if (!currentClip.loaded)
    {
        ++dialogQueueIndex;
        startNextQueuedDialogClip();
        return;
    }

    currentClip.remainingSeconds -= deltaSeconds;
    if (currentClip.remainingSeconds > 0.0f)
    {
        SetSoundVolume(currentClip.sound, appliedVolume(currentClip.baseVolume));
        return;
    }

    removeTempFile(currentClip.tempFilePath);
    UnloadSound(currentClip.sound);
    currentClip.loaded = false;
    ++dialogQueueIndex;
    startNextQueuedDialogClip();
}

void AudioManager::playDialog(const std::string& relativePath, float volume)
{
    if (relativePath.empty())
        return;

    playDialogSequence({ relativePath }, volume);
}

bool AudioManager::hasDialogAsset(const std::string& relativePath) const
{
    if (relativePath.empty())
        return false;

    std::string playablePath;
    std::string tempFile;
    return resolveMusicAssetFile(relativePath, playablePath, tempFile);
}

bool AudioManager::playDialogAsset(const std::string& relativePath, float volume)
{
    return playDialogAssetSequence({ relativePath }, volume);
}

bool AudioManager::playDialogAssetSequence(
    const std::vector<std::string>& relativePaths,
    float volume)
{
    if (!ensureDeviceReady())
        return false;

    stopDialog();
    dialogClipVolume = volume;

    bool queuedAny = false;
    for (const std::string& relativePath : relativePaths)
    {
        if (relativePath.empty())
            continue;

        std::string playablePath;
        std::string tempFile;
        if (!resolveMusicAssetFile(relativePath, playablePath, tempFile))
        {
            TraceLog(LOG_WARNING, "Failed to load bundled TTS clip: %s", relativePath.c_str());
            continue;
        }

        Sound sound = LoadSound(playablePath.c_str());
        if (sound.stream.buffer == nullptr)
        {
            TraceLog(LOG_WARNING, "Failed to decode bundled TTS clip: %s", playablePath.c_str());
            removeTempFile(tempFile);
            continue;
        }

        float durationSeconds = 0.2f;
        if (sound.frameCount > 0 && sound.stream.sampleRate > 0)
        {
            durationSeconds = static_cast<float>(sound.frameCount)
                / static_cast<float>(sound.stream.sampleRate);
        }

        ActiveSound queuedClip;
        queuedClip.sound = sound;
        queuedClip.loaded = true;
        queuedClip.baseVolume = effectiveVolume(AudioCategory::Sfx, volume);
        queuedClip.remainingSeconds = std::max(0.05f, durationSeconds);
        queuedClip.tempFilePath = tempFile;
        dialogQueue.push_back(queuedClip);
        TraceLog(LOG_INFO, "Queued bundled TTS: %s (%.2fs)", relativePath.c_str(), durationSeconds);
        queuedAny = true;
    }

    if (!queuedAny)
        return false;

    dialogQueueIndex = 0;
    startNextQueuedDialogClip();
    return true;
}

void AudioManager::playDialogMp3File(
    const std::string& filePath,
    float volume,
    const std::string& tempFilePath)
{
    if (filePath.empty() || !ensureDeviceReady() || !FileExists(filePath.c_str()))
        return;

    stopDialog();
    dialogClipVolume = volume;

    Sound sound = LoadSound(filePath.c_str());
    if (sound.stream.buffer == nullptr)
    {
        TraceLog(LOG_WARNING, "Failed to load TTS dialog clip: %s", filePath.c_str());
        return;
    }

    float durationSeconds = 0.2f;
    if (sound.frameCount > 0 && sound.stream.sampleRate > 0)
    {
        durationSeconds = static_cast<float>(sound.frameCount)
            / static_cast<float>(sound.stream.sampleRate);
    }

    ActiveSound queuedClip;
    queuedClip.sound = sound;
    queuedClip.loaded = true;
    queuedClip.baseVolume = effectiveVolume(AudioCategory::Sfx, 1.0f);
    queuedClip.remainingSeconds = std::max(0.05f, durationSeconds);
    queuedClip.tempFilePath = tempFilePath;
    dialogQueue.push_back(queuedClip);
    dialogQueueIndex = 0;
    startNextQueuedDialogClip();
    TraceLog(LOG_INFO, "Playing TTS dialog: %s (%.2fs)", filePath.c_str(), durationSeconds);
}

void AudioManager::playDialogSequence(const std::vector<std::string>& relativePaths, float volume)
{
    if (!ensureDeviceReady())
        return;

    stopDialog();
    dialogClipVolume = volume;

    for (const std::string& relativePath : relativePaths)
    {
        if (relativePath.empty())
            continue;

        Sound sound{};
        float durationSeconds = 0.0f;
        if (!loadDialogClip(relativePath, sound, durationSeconds))
        {
            TraceLog(LOG_WARNING, "Failed to load dialog clip: %s", relativePath.c_str());
            continue;
        }

        ActiveSound queuedClip;
        queuedClip.sound = sound;
        queuedClip.loaded = true;
        queuedClip.baseVolume = effectiveVolume(AudioCategory::Sfx, volume);
        queuedClip.remainingSeconds = std::max(0.05f, durationSeconds);
        dialogQueue.push_back(queuedClip);
        TraceLog(LOG_INFO, "Queued dialog: %s (%.2fs)", relativePath.c_str(), durationSeconds);
    }

    dialogQueueIndex = 0;
    startNextQueuedDialogClip();
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
            SetSoundVolume(activeSound.sound, appliedVolume(activeSound.baseVolume));
            remainingSounds.push_back(activeSound);
            continue;
        }

        UnloadSound(activeSound.sound);
        removeTempFile(activeSound.tempFilePath);
    }

    activeSounds.swap(remainingSounds);
}

void AudioManager::update(float deltaSeconds)
{
    if (!deviceReady)
        return;

    updateGameplayMix(deltaSeconds);
    updateActiveSounds(deltaSeconds);
    updateDialogQueue(deltaSeconds);

    const bool duckRoomAudio = !dialogQueue.empty()
        && dialogQueueIndex < dialogQueue.size()
        && dialogQueue[dialogQueueIndex].loaded;
    const float dialogDuck = duckRoomAudio ? 0.15f : 1.0f;
    const float sceneMix = itemExamineAudioActive ? itemSceneMix : 1.0f;
    const float roomAudioDuck = dialogDuck * sceneMix;
    if (musicTrack.loaded && musicTrack.playing)
        musicTrack.targetVolume = effectiveVolume(AudioCategory::Music, musicTrack.sourceClipVolume) * roomAudioDuck;
    for (FadingAmbientTrack& track : ambientTracks)
    {
        if (track.playing)
            track.targetVolume = effectiveVolume(AudioCategory::Ambient, track.sourceClipVolume) * roomAudioDuck;
    }

    updateMusicTrack(musicTrack, deltaSeconds);
    updateMusicTrack(itemMusicTrack, deltaSeconds);

    std::vector<FadingAmbientTrack> remainingAmbient;
    remainingAmbient.reserve(ambientTracks.size());

    for (FadingAmbientTrack& track : ambientTracks)
    {
        updateAmbientTrack(track, deltaSeconds);
        if (track.loaded && (track.playing || track.fadingIn || track.fadingOut))
            remainingAmbient.push_back(track);
        else
            unloadAmbientTrack(track);
    }

    ambientTracks.swap(remainingAmbient);

    std::vector<FadingAmbientTrack> remainingItemAmbient;
    remainingItemAmbient.reserve(itemAmbientTracks.size());
    for (FadingAmbientTrack& track : itemAmbientTracks)
    {
        updateAmbientTrack(track, deltaSeconds);
        if (track.loaded && (track.playing || track.fadingIn || track.fadingOut))
            remainingItemAmbient.push_back(track);
        else
            unloadAmbientTrack(track);
    }
    itemAmbientTracks.swap(remainingItemAmbient);

    if (pendingMusicStart && musicTrack.loaded && !musicTrack.playing && !musicTrack.fadingIn && !musicTrack.fadingOut)
    {
        const AudioClipDef clip = pendingMusicClip;
        pendingMusicStart = false;
        pendingMusicClip = AudioClipDef{};
        startMusicTrack(musicTrack, clip);
    }
}

void AudioManager::playSfx(const AudioClipDef& clip)
{
    if (clip.path.empty() || !ensureDeviceReady())
        return;

    Sound sound{};
    float durationSeconds = 0.0f;
    std::string tempFile;
    if (!loadSoundClip(clip.path, sound, durationSeconds, tempFile))
    {
        TraceLog(LOG_WARNING, "Failed to load sfx clip: %s", clip.path.c_str());
        return;
    }

    const float volume = effectiveVolume(AudioCategory::Sfx, clip.volume);
    SetSoundVolume(sound, appliedVolume(volume));
    PlaySound(sound);
    TraceLog(LOG_INFO, "Playing sfx: %s (%.2fs at volume %.2f)", clip.path.c_str(), durationSeconds, volume);

    ActiveSound activeSound;
    activeSound.sound = sound;
    activeSound.loaded = true;
    activeSound.baseVolume = volume;
    activeSound.remainingSeconds = std::max(0.05f, durationSeconds);
    activeSound.tempFilePath = tempFile;
    activeSounds.push_back(activeSound);
}

void AudioManager::playRoomSfx(
    const RoomAudioConfig& roomAudio,
    const std::string& trigger,
    const std::string& fromRoom,
    const std::string& toRoom)
{
    for (const AudioClipDef& sfxClip : roomAudio.sfx)
    {
        const std::string clipTrigger = sfxClip.trigger.empty() ? "on_enter" : sfxClip.trigger;
        if (clipTrigger != trigger)
            continue;

        const std::string requiredFromRoom = stringAttributeOrEmpty(sfxClip, "from_room");
        const std::string requiredToRoom = stringAttributeOrEmpty(sfxClip, "to_room");
        if (!matchesTransitionConstraint(requiredFromRoom, fromRoom))
            continue;
        if (!matchesTransitionConstraint(requiredToRoom, toRoom))
            continue;

        playSfx(sfxClip);
    }
}

bool AudioManager::isMusicStreamActive(const FadingMusicTrack& track) const
{
    return track.loaded && (track.playing || track.fadingIn || track.fadingOut);
}

bool AudioManager::isAmbientTrackActive(const FadingAmbientTrack& track) const
{
    return track.loaded && (track.playing || track.fadingIn || track.fadingOut);
}

void AudioManager::retainMusicTrack(FadingMusicTrack& track, const AudioClipDef& clip)
{
    track.fadingOut = false;
    track.fadingIn = false;
    track.fadeElapsed = 0.0f;
    track.sourceClipVolume = clip.volume;
    track.targetVolume = effectiveVolume(AudioCategory::Music, clip.volume);
    track.fadeInSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_in", clip.fadeIn));
    track.fadeOutSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_out", clip.fadeOut));
    track.loop = clip.loop;
    track.music.looping = clip.loop;

    if (track.playing)
        track.currentVolume = track.targetVolume;

    SetMusicVolume(track.music, appliedVolume(track.currentVolume));
}

void AudioManager::retainAmbientTrack(FadingAmbientTrack& track, const AudioClipDef& clip)
{
    track.fadingOut = false;
    track.fadingIn = false;
    track.fadeElapsed = 0.0f;
    track.sourceClipVolume = clip.volume;
    track.targetVolume = effectiveVolume(AudioCategory::Ambient, clip.volume);
    track.fadeInSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_in", clip.fadeIn));
    track.fadeOutSeconds = std::max(0.0f, attributeOrDefault(clip, "fade_out", clip.fadeOut));
    track.loop = clip.loop;

    if (track.playing)
        track.currentVolume = track.targetVolume;

    SetSoundVolume(track.sound, appliedVolume(track.currentVolume));

    if (track.loop && !IsSoundPlaying(track.sound))
    {
        track.playing = true;
        PlaySound(track.sound);
    }
}

AudioManager::FadingAmbientTrack* AudioManager::findAmbientTrackByPath(const std::string& path)
{
    for (FadingAmbientTrack& track : ambientTracks)
    {
        if (track.loaded && track.path == path)
            return &track;
    }

    return nullptr;
}

void AudioManager::syncRoomStreams(const RoomAudioConfig& roomAudio)
{
    if (!ensureDeviceReady())
        return;

    TraceLog(LOG_INFO, "Room audio sync: music=%s ambient_tracks=%zu",
        roomAudio.hasMusic ? roomAudio.music.path.c_str() : "(none)",
        roomAudio.ambient.size());

    ++ambientLoadGeneration;
    desiredAmbientPaths.clear();
    for (const AudioClipDef& ambientClip : roomAudio.ambient)
    {
        if (!ambientClip.path.empty())
            desiredAmbientPaths.insert(ambientClip.path);
    }

    if (roomAudio.hasMusic)
    {
        if (musicTrack.loaded && musicTrack.path == roomAudio.music.path && isMusicStreamActive(musicTrack))
        {
            retainMusicTrack(musicTrack, roomAudio.music);
            pendingMusicStart = false;
        }
        else if (isMusicStreamActive(musicTrack))
        {
            fadeOutMusicTrack(musicTrack, musicTrack.fadeOutSeconds);
            ++musicLoadGeneration; // cancel in-flight async loads for the old bed
            pendingMusicClip = roomAudio.music;
            pendingMusicStart = true;
        }
        else
        {
            pendingMusicStart = false;
            startMusicTrack(musicTrack, roomAudio.music);
        }
    }
    else if (isMusicStreamActive(musicTrack))
    {
        pendingMusicStart = false;
        fadeOutMusicTrack(musicTrack, musicTrack.fadeOutSeconds);
    }
    else
    {
        pendingMusicStart = false;
        unloadMusicTrack(musicTrack);
    }

    std::unordered_set<std::string> retainedAmbientPaths;

    for (const AudioClipDef& ambientClip : roomAudio.ambient)
    {
        FadingAmbientTrack* existingTrack = findAmbientTrackByPath(ambientClip.path);
        if (existingTrack != nullptr)
        {
            retainAmbientTrack(*existingTrack, ambientClip);
            retainedAmbientPaths.insert(ambientClip.path);
            continue;
        }

        startAmbientTrack(ambientClip);
        retainedAmbientPaths.insert(ambientClip.path);
    }

    for (FadingAmbientTrack& track : ambientTracks)
    {
        if (retainedAmbientPaths.count(track.path) > 0)
            continue;

        if (isAmbientTrackActive(track))
            fadeOutAmbientTrack(track, track.fadeOutSeconds);
        else if (track.loaded)
            unloadAmbientTrack(track);
    }
}

void AudioManager::onRoomExit(const RoomAudioConfig& roomAudio, const std::string& toRoom)
{
    playRoomSfx(roomAudio, "on_exit", "", toRoom);
}

void AudioManager::onRoomEnter(const RoomAudioConfig& roomAudio, const std::string& fromRoom)
{
    playRoomSfx(roomAudio, "on_enter", fromRoom, "");
    syncRoomStreams(roomAudio);
}

void AudioManager::startTitleScreenBed()
{
    RoomAudioConfig room;
    room.hasMusic = true;
    room.music.path = "resources/audio/music/title_theme.mp3";
    room.music.volume = 0.55f;
    room.music.loop = true;
    room.music.fadeIn = 1.8f;
    room.music.fadeOut = 1.2f;

    AudioClipDef wind;
    wind.path = "resources/audio/ambient/wind.mp3";
    wind.volume = 0.28f; // muffled wind bed under the theme
    wind.loop = true;
    wind.fadeIn = 1.2f;
    wind.fadeOut = 1.0f;
    room.ambient.push_back(wind);

    // Title bed should be audible even if gameplay was ducked.
    setGameplayPaused(false, 0.01f);
    onRoomEnter(room, "");
}

void AudioManager::stopTitleScreenBed()
{
    RoomAudioConfig empty;
    onRoomEnter(empty, "");
}

void AudioManager::applyItemExamineAudio(const ItemAudioOverlayDef& overlay)
{
    clearItemExamineAudio();

    if (!overlay.hasMusic && overlay.ambient.empty() && !overlay.muteSceneAudio)
        return;

    itemExamineAudioActive = true;
    activeItemAudio = overlay;
    itemSceneMix = overlay.muteSceneAudio ? 0.0f : std::max(0.0f, overlay.sceneMix);

    if (overlay.hasMusic)
        startMusicTrack(itemMusicTrack, overlay.music);

    for (const AudioClipDef& clip : overlay.ambient)
    {
        if (clip.path.empty() || !ensureDeviceReady())
            continue;

        FadingAmbientTrack track;
        Sound sound{};
        bool usesAlias = false;
        if (!acquireAmbientSound(clip.path, sound, usesAlias))
            continue;

        track.sound = sound;
        track.loaded = true;
        track.path = clip.path;
        track.usesCachedAlias = usesAlias;
        track.sourceClipVolume = clip.volume;
        track.targetVolume = effectiveVolume(AudioCategory::Ambient, clip.volume);
        track.fadeInSeconds = std::max(0.0f, clip.fadeIn);
        track.fadeOutSeconds = std::max(0.0f, clip.fadeOut);
        track.fadeElapsed = 0.0f;
        track.fadingIn = track.fadeInSeconds > 0.0f;
        track.fadingOut = false;
        track.currentVolume = track.fadingIn ? 0.0f : track.targetVolume;
        track.loop = clip.loop;

        SetSoundVolume(track.sound, appliedVolume(track.currentVolume));
        PlaySound(track.sound);
        track.playing = true;
        itemAmbientTracks.push_back(track);
    }
}

void AudioManager::clearItemExamineAudio()
{
    if (itemMusicTrack.loaded)
    {
        if (itemMusicTrack.playing)
            fadeOutMusicTrack(itemMusicTrack, itemMusicTrack.fadeOutSeconds > 0.0f ? itemMusicTrack.fadeOutSeconds : 0.35f);
        else
            unloadMusicTrack(itemMusicTrack);
    }

    for (FadingAmbientTrack& track : itemAmbientTracks)
    {
        if (track.playing)
            fadeOutAmbientTrack(track, track.fadeOutSeconds > 0.0f ? track.fadeOutSeconds : 0.35f);
        else if (track.loaded)
            unloadAmbientTrack(track);
    }

    itemExamineAudioActive = false;
    activeItemAudio = ItemAudioOverlayDef{};
    itemSceneMix = 1.0f;
}

}