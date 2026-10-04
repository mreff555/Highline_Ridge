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

#ifndef ACTIVE_SCENE_H
#define ACTIVE_SCENE_H

#include <LocationStruct.h>
#include <string>

namespace timberline_engine
{

class SceneDatabase;

class ActiveScene
{
    public:
    ActiveScene();

    const std::string& getId() const { return sceneId; }
    const LocationStruct& getView() const { return view; }
    bool isUnderConstruction() const { return view.isUnderConstruction; }

    void loadFromStruct(const std::string& id, const LocationStruct& locationStruct);
    void unloadOwnedImage();
    bool replaceLocationImage(const SceneDatabase& database, const std::string& imagePath);
    /** Swap in a main-thread texture; unloads the previous owned image. */
    void adoptOwnedTexture(Texture2D texture, bool underConstruction = false);

    private:
    std::string sceneId;
    LocationStruct view;
};

}

#endif /* ACTIVE_SCENE_H */