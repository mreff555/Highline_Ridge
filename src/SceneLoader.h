#ifndef SCENE_LOADER_H
#define SCENE_LOADER_H

#include <AudioTypes.h>
#include <ConversationStruct.h>
#include <LocationStruct.h>
#include <ExitRequirementDef.h>
#include <SceneInteractionDef.h>
#include <TakeableItemDef.h>
#include <raylib.h>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace testgame
{

bool loadResourceTexture(const std::string& assetRoot, const std::string& relativePath, Texture2D& outTexture);

struct SceneData
{
    std::string id;
    std::string imagePath;
    std::string alternateImagePath;
    std::string alternateImageFlag;
    std::string alternateImageUntilPhase;
    std::string description;
    std::string examineDetails;
    std::string examineFlag;
    std::string speakDetails;
    std::string useDetails;
    float useHealthDelta = 0.0f;
    float useEnergyDelta = 0.0f;
    bool useRepeatStatus = false;
    bool useRequiresExamine = true;
    bool useAdvancesDay = false;
    std::string useExit;
    MovementStruct movement;
    ActionStruct actions;
    bool isStart;
    std::map<std::string, std::string> exits;
    std::map<std::string, ExitRequirementDef> exitRequirements;
    SceneSpeakConfig speakConfig;
    RoomAudioConfig audio;
    std::vector<TakeableItemDef> takeables;
    std::vector<SceneInteractionDef> interactions;
};

class SceneDatabase
{
    public:
    SceneDatabase();
    ~SceneDatabase();

    bool load(const std::string& configPath, const std::string& assetRoot);
    bool loadStartScene(LocationStruct& outLocation, std::string& outSceneId) const;
    bool loadScene(const std::string& sceneId, LocationStruct& outLocation) const;
    std::string getExitSceneId(const std::string& sceneId, const std::string& direction) const;
    bool getExitRequirement(
        const std::string& sceneId,
        const std::string& direction,
        ExitRequirementDef& outRequirement) const;
    const SceneSpeakConfig& getSpeakConfig(const std::string& sceneId) const;
    const RoomAudioConfig& getSceneAudio(const std::string& sceneId) const;
    const std::vector<TakeableItemDef>& getTakeables(const std::string& sceneId) const;
    const std::vector<SceneInteractionDef>& getInteractions(const std::string& sceneId) const;
    const std::string& getAssetRoot() const { return assetRoot; }
    const SceneData* getScene(const std::string& sceneId) const;
    std::string resolveSceneImagePath(
        const SceneData& scene,
        const std::set<std::string>& storyFlags,
        const std::function<bool(const std::string& phaseId)>& isPhaseComplete) const;
    bool loadSceneTexture(const std::string& imagePath, Texture2D& outTexture) const;

    private:
    bool buildLocationStruct(const SceneData& scene, LocationStruct& outLocation) const;
    bool tryLoadSceneImage(const std::string& imagePath, Texture2D& outTexture) const;
    void ensureUnderConstructionImage() const;
    Texture2D createOwnedPlaceholderTexture() const;

    std::map<std::string, SceneData> scenes;
    Font descriptionFont;
    Font boldFont;
    Font uiFont;
    bool fontsLoaded;
    std::string assetRoot;
    mutable Image underConstructionImage{};
    mutable bool underConstructionImageReady = false;
};

bool loadStartLocation(
    const std::string& configPath,
    const std::string& assetRoot,
    LocationStruct& outLocation);

}

#endif /* SCENE_LOADER_H */
