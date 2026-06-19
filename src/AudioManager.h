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

    void onRoomEnter(const RoomAudioConfig& roomAudio);
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
    };

    struct ActiveSound
    {
        Sound sound{};
        bool loaded = false;
        float remainingSeconds = 0.0f;
    };

    bool loadMusicClip(const std::string& path, Music& outMusic) const;
    bool loadSoundClip(const std::string& path, Sound& outSound, float& outDurationSeconds) const;
    bool resolveAssetBytes(const std::string& relativePath, std::vector<unsigned char>& outBytes) const;
    float effectiveVolume(AudioCategory category, float clipVolume) const;
    void startMusicTrack(FadingMusicTrack& track, const AudioClipDef& clip);
    void startAmbientTrack(const AudioClipDef& clip);
    void fadeOutMusicTrack(FadingMusicTrack& track, float fadeOutSeconds);
    void fadeOutAmbientTracks(float fadeOutSeconds);
    void updateMusicTrack(FadingMusicTrack& track, float deltaSeconds, AudioCategory category);
    void unloadMusicTrack(FadingMusicTrack& track);
    void unloadAmbientTracks();
    void updateActiveSounds(float deltaSeconds);
    void applyRoomAudio(const RoomAudioConfig& roomAudio);
    bool hasActiveStreamAudio() const;

    std::string assetRoot;
    AudioVolumeConfig volumes;
    bool deviceReady = false;

    FadingMusicTrack musicTrack;
    std::vector<FadingMusicTrack> ambientTracks;
    std::vector<ActiveSound> activeSounds;
    bool roomTransitionPending = false;
    RoomAudioConfig pendingRoomAudio;
};

}

#endif /* AUDIO_MANAGER_H */