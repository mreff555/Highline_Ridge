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

#include "ActiveScene.h"

#include <SceneLoader.h>

namespace timberline_engine
{

ActiveScene::ActiveScene()
{
}

void ActiveScene::loadFromStruct(const std::string& id, const LocationStruct& locationStruct)
{
    // Async scene loads pass a struct without a texture so we can keep drawing
    // the previous room until JobSystem uploads the next one.
    Texture2D preserved{};
    bool preserve = false;
    if ((!locationStruct.ownsLocationImage || locationStruct.locationImage.id == 0)
        && view.ownsLocationImage
        && view.locationImage.id != 0)
    {
        preserved = view.locationImage;
        preserve = true;
        view.ownsLocationImage = false;
        view.locationImage = Texture2D{};
    }

    unloadOwnedImage();
    sceneId = id;
    view = locationStruct;
    if (preserve)
    {
        view.locationImage = preserved;
        view.ownsLocationImage = true;
    }
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

    adoptOwnedTexture(sceneTexture, false);
    return true;
}

void ActiveScene::adoptOwnedTexture(Texture2D texture, bool underConstruction)
{
    unloadOwnedImage();
    view.locationImage = texture;
    view.ownsLocationImage = (texture.id != 0);
    view.isUnderConstruction = underConstruction;
}

}