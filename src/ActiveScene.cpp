#include "ActiveScene.h"

#include <SceneLoader.h>

namespace testgame
{

ActiveScene::ActiveScene()
{
}

void ActiveScene::loadFromStruct(const std::string& id, const LocationStruct& locationStruct)
{
    unloadOwnedImage();
    sceneId = id;
    view = locationStruct;
}

void ActiveScene::unloadOwnedImage()
{
    if (view.ownsLocationImage && view.locationImage.id != 0)
    {
        UnloadTexture(view.locationImage);
        view.locationImage = Texture2D{};
        view.ownsLocationImage = false;
    }
}

bool ActiveScene::replaceLocationImage(const SceneDatabase& database, const std::string& imagePath)
{
    Texture2D sceneTexture{};
    if (!database.loadSceneTexture(imagePath, sceneTexture))
        return false;

    unloadOwnedImage();
    view.locationImage = sceneTexture;
    view.ownsLocationImage = true;
    view.isUnderConstruction = false;
    return true;
}

}