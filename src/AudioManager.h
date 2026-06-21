#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <AudioTypes.h>
#include <ItemDef.h>
#include <GameConfig.h>
#include <raylib.h>
#include <map>
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
    void setGameplayPaused(bool paused, float fadeSeconds = 0.6f);
    bool isGameplayPaused() const { return gameplayPaused; }
    void update(float deltaSeconds);

    void onRoomEnter(const RoomAudioConfig& roomAudio, const std::string& fromRoom = "");
    void onRoomExit(const RoomAudioConfig& roomAudio, const std::string& toRoom = "");
    void playSfx(const AudioClipDef& clip);
    void playDialog(const std::string& relativePath, float volume = 1.0f);
    void playDialogSequence(const std::vector<std::string>& relativePaths, float volume = 1.0f);
    void playDialogMp3File(
        const std::string& filePath,
        float volume = 1.0f,
        const std::string& tempFilePath = "");
    bool hasDialogAsset(const std::string& relativePath) const;
    bool playDialogAsset(const std::string& relativePath, float volume = 1.0f);
    bool playDialogAssetSequence(const std::vector<std::string>& relativePaths, float volume = 1.0f);
    void stopDialog();
    void applyItemExamineAudio(const ItemAudioOverlayDef& overlay);
    void clearItemExamineAudio();
    bool hasItemExamineAudio() const { return itemExamineAudioActive; }

    private:
    struct FadingMusicTrack
    {
        Music music{};
        bool loaded = false;
        bool playing = false;
        float sourceClipVolume = 1.0f;
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
        float sourceClipVolume = 1.0f;
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
        bool usesCachedAlias = false;
    };

    struct ActiveSound
    {
        Sound sound{};
        bool loaded = false;
        float baseVolume = 1.0f;
        float remainingSeconds = 0.0f;
        std::string tempFilePath;
    };

    void refreshCategoryVolumes();
    void updateGameplayMix(float deltaSeconds);
    float appliedVolume(float baseVolume) const;

    bool ensureDeviceReady();
    bool loadMusicClip(const std::string& path, Music& outMusic, std::string& outTempFile);
    bool loadSoundClip(const std::string& path, Sound& outSound, float& outDurationSeconds, std::string& outTempFile);
    bool loadDialogClip(const std::string& path, Sound& outSound, float& outDurationSeconds);
    bool acquireAmbientSound(const std::string& path, Sound& outSound, bool& outUsesAlias);
    void releaseAmbientSound(const std::string& path, Sound& sound, bool usesAlias);
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
    void updateDialogQueue(float deltaSeconds);
    void startNextQueuedDialogClip();
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

    struct CachedAmbientSample
    {
        Sound sound{};
        bool loaded = false;
        std::string tempFilePath;
    };

    std::string assetRoot;
    AudioVolumeConfig volumes;
    bool deviceReady = false;
    std::map<std::string, CachedAmbientSample> ambientSampleCache;

    FadingMusicTrack musicTrack;
    std::vector<FadingAmbientTrack> ambientTracks;
    std::vector<ActiveSound> activeSounds;
    std::vector<ActiveSound> dialogQueue;
    size_t dialogQueueIndex = 0;
    float dialogClipVolume = 1.0f;
    bool pendingMusicStart = false;
    AudioClipDef pendingMusicClip;
    bool gameplayPaused = false;
    float gameplayMix = 1.0f;
    float gameplayMixTarget = 1.0f;
    float gameplayMixFadeRate = 2.0f;

    bool itemExamineAudioActive = false;
    ItemAudioOverlayDef activeItemAudio;
    FadingMusicTrack itemMusicTrack;
    std::vector<FadingAmbientTrack> itemAmbientTracks;
    float itemSceneMix = 1.0f;
};

}

#endif /* AUDIO_MANAGER_H */