#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <AudioTypes.h>
#include <GameConfig.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace testgame
{

class AudioManager
{
    public:
    AudioManager();
    ~AudioManager();

    bool initialize(const std::string& assetRoot, const AudioVolumeConfig& volumes);
    void shutdown();

    void setVolumes(const AudioVolumeConfig& volumes);
    void update(float deltaSeconds);

    void onRoomEnter(const RoomAudioConfig& roomAudio, const std::string& fromRoom = "");
    void onRoomExit(const RoomAudioConfig& roomAudio, const std::string& toRoom = "");
    void playSfx(const AudioClipDef& clip);

    private:
    struct FadingMusicTrack
    {
        Music music{};
        bool loaded = false;
        bool playing = false;
        float targetVolume = 1.0f;
        float currentVolume = 0.0f;
        float fadeInSeconds = 0.0f;
        float fadeOutSeconds = 0.0f;
        float fadeElapsed = 0.0f;
        bool fadingIn = false;
        bool fadingOut = false;
        std::string path;
        std::string tempFilePath;
        bool loop = true;
    };

    struct FadingAmbientTrack
    {
        Sound sound{};
        bool loaded = false;
        bool playing = false;
        float targetVolume = 1.0f;
        float currentVolume = 0.0f;
        float fadeInSeconds = 0.0f;
        float fadeOutSeconds = 0.0f;
        float fadeElapsed = 0.0f;
        bool fadingIn = false;
        bool fadingOut = false;
        std::string path;
        std::string tempFilePath;
        bool loop = true;
    };

    struct ActiveSound
    {
        Sound sound{};
        bool loaded = false;
        float remainingSeconds = 0.0f;
        std::string tempFilePath;
    };

    bool ensureDeviceReady();
    bool loadMusicClip(const std::string& path, Music& outMusic, std::string& outTempFile);
    bool loadSoundClip(const std::string& path, Sound& outSound, float& outDurationSeconds, std::string& outTempFile);
    bool resolveAssetBytes(const std::string& relativePath, std::vector<unsigned char>& outBytes) const;
    bool resolveMusicAssetFile(const std::string& relativePath, std::string& outPlayablePath, std::string& outTempFile) const;
    void removeTempFile(const std::string& path);
    float effectiveVolume(AudioCategory category, float clipVolume) const;
    void startMusicTrack(FadingMusicTrack& track, const AudioClipDef& clip);
    void startAmbientTrack(const AudioClipDef& clip);
    void fadeOutMusicTrack(FadingMusicTrack& track, float fadeOutSeconds);
    void fadeOutAmbientTrack(FadingAmbientTrack& track, float fadeOutSeconds);
    void updateMusicTrack(FadingMusicTrack& track, float deltaSeconds);
    void updateAmbientTrack(FadingAmbientTrack& track, float deltaSeconds);
    void unloadMusicTrack(FadingMusicTrack& track);
    void unloadAmbientTrack(FadingAmbientTrack& track);
    void unloadAmbientTracks();
    void updateActiveSounds(float deltaSeconds);
    void syncRoomStreams(const RoomAudioConfig& roomAudio);
    void retainMusicTrack(FadingMusicTrack& track, const AudioClipDef& clip);
    void retainAmbientTrack(FadingAmbientTrack& track, const AudioClipDef& clip);
    FadingAmbientTrack* findAmbientTrackByPath(const std::string& path);
    bool isMusicStreamActive(const FadingMusicTrack& track) const;
    bool isAmbientTrackActive(const FadingAmbientTrack& track) const;
    void playRoomSfx(
        const RoomAudioConfig& roomAudio,
        const std::string& trigger,
        const std::string& fromRoom,
        const std::string& toRoom);

    std::string assetRoot;
    AudioVolumeConfig volumes;
    bool deviceReady = false;

    FadingMusicTrack musicTrack;
    std::vector<FadingAmbientTrack> ambientTracks;
    std::vector<ActiveSound> activeSounds;
    bool pendingMusicStart = false;
    AudioClipDef pendingMusicClip;
};

}

#endif /* AUDIO_MANAGER_H */