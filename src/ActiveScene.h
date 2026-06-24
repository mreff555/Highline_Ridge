#ifndef ACTIVE_SCENE_H
#define ACTIVE_SCENE_H

#include <LocationStruct.h>
#include <string>

namespace testgame
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

    private:
    std::string sceneId;
    LocationStruct view;
};

}

#endif /* ACTIVE_SCENE_H */