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

#ifndef SCENE_LOADER_H
#define SCENE_LOADER_H

#include <AudioTypes.h>
#include <ItemDef.h>
#include <ConversationStruct.h>
#include <LocationStruct.h>
#include <AlternateImageDef.h>
#include <ExitRequirementDef.h>
#include <MovementMappingDef.h>
#include <SceneInteractionDef.h>
#include <SceneInventoryDef.h>
#include <SceneOverlayDef.h>
#include <SubSceneDef.h>
#include <TakeableItemDef.h>
#include <raylib.h>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace timberline_engine
{

enum class SubSceneResolveMode
{
    OnEnter,
    InScene
};

bool loadResourceTexture(const std::string& assetRoot, const std::string& relativePath, Texture2D& outTexture);

struct SceneData
{
    std::string id;
    std::string defaultSubSceneId;
    std::string imagePath;
    std::string alternateImagePath;
    std::string alternateImageFlag;
    std::string alternateImageUntilPhase;
    std::vector<AlternateImageDef> alternateImages;
    std::string description;
    ItemTtsDef descriptionTts;
    std::string wakeNarrative;
    ItemTtsDef wakeTts;
    std::string examineDetails;
    ItemTtsDef examineTts;
    std::string examineFlag;
    float examineLucidityDelta = 0.0f;
    bool examineLucidityOncePerDay = false;
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
    bool highAltitude = false;
    std::map<std::string, std::string> exits;
    std::map<std::string, ExitRequirementDef> exitRequirements;
    std::map<std::string, std::vector<MovementMappingDef>> movementExits;
    std::map<std::string, SubSceneDef> subScenes;
    std::vector<SubSceneRuleDef> subSceneRules;
    std::vector<SceneInventoryEntryDef> sceneInventory;
    SceneSpeakConfig speakConfig;
    RoomAudioConfig audio;
    std::vector<TakeableItemDef> takeables;
    std::vector<SceneInteractionDef> interactions;
    std::vector<SceneOverlayDef> overlays;
};

class SceneDatabase
{
    public:
    SceneDatabase();
    ~SceneDatabase();

    bool load(const std::string& configPath, const std::string& assetRoot);
    bool loadStartScene(LocationStruct& outLocation, std::string& outSceneId) const;
    bool loadScene(const std::string& sceneId, LocationStruct& outLocation) const;
    bool loadScene(
        const std::string& sceneId,
        const std::string& subSceneId,
        LocationStruct& outLocation) const;
    std::string getExitSceneId(const std::string& sceneId, const std::string& direction) const;
    bool getExitRequirement(
        const std::string& sceneId,
        const std::string& direction,
        ExitRequirementDef& outRequirement) const;
    const SceneSpeakConfig& getSpeakConfig(const std::string& sceneId) const;
    std::vector<SceneActorDef> getSceneActors(const std::string& sceneId) const;
    std::vector<SceneActorDef> getSceneActors(
        const std::string& sceneId,
        const std::string& subSceneId) const;
    const RoomAudioConfig& getSceneAudio(const std::string& sceneId) const;
    const RoomAudioConfig& getSceneAudio(
        const std::string& sceneId,
        const std::string& subSceneId) const;
    const std::vector<TakeableItemDef>& getTakeables(const std::string& sceneId) const;
    const std::vector<SceneInteractionDef>& getInteractions(const std::string& sceneId) const;
    const std::vector<SceneInteractionDef>& getInteractions(
        const std::string& sceneId,
        const std::string& subSceneId) const;
    const std::vector<SceneOverlayDef>& getOverlays(const std::string& sceneId) const;
    const std::string& getAssetRoot() const { return assetRoot; }
    const SceneData* getScene(const std::string& sceneId) const;
    const SubSceneDef* getSubScene(const std::string& sceneId, const std::string& subSceneId) const;
    bool isHighAltitudeScene(const std::string& sceneId) const;
    std::string resolveActiveSubSceneId(
        const SceneData& scene,
        const std::set<std::string>& storyFlags,
        const std::string& requestedSubSceneId = "",
        SubSceneResolveMode mode = SubSceneResolveMode::InScene,
        const std::function<bool(const std::string& phaseId)>& isPhaseComplete = nullptr) const;
    std::string resolveSceneImagePath(
        const SceneData& scene,
        const std::string& subSceneId,
        const std::set<std::string>& storyFlags,
        const std::function<bool(const std::string& phaseId)>& isPhaseComplete) const;
    std::vector<std::string> collectSceneImagePaths(const SceneData& scene) const;
    bool loadSceneTexture(const std::string& imagePath, Texture2D& outTexture) const;

    private:
    bool buildLocationStruct(
        const SceneData& scene,
        const std::string& subSceneId,
        LocationStruct& outLocation) const;
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