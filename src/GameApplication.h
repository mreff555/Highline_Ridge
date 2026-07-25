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

#ifndef GAME_APPLICATION_H
#define GAME_APPLICATION_H

#include <AudioManager.h>
#include <GameConfig.h>
#include <GameSession.h>
#include <ItemCombinationDatabase.h>
#include <ItemDatabase.h>
#include <MilestoneDatabase.h>
#include <SceneLoader.h>
#include <UiBackdrop.h>
#include <memory>
#include <string>

namespace timberline_engine
{

class GameApplication
{
    public:
    int run(int argc, char* argv[]);

    private:
    bool locateGameResources();
    bool initializeWindow(const GameConfig& gameConfig);
    bool loadDatabases();
    void shutdown();

    GameConfig gameConfig;
    std::string gameConfigPath = "resources/game_config.json";
    AudioManager audioManager;
    SceneDatabase sceneDatabase;
    MilestoneDatabase milestoneDatabase;
    ItemDatabase itemDatabase;
    ItemCombinationDatabase itemCombinationDatabase;
    UiBackdrop uiBackdrop;
    std::unique_ptr<GameSession> session;
};

}

#endif /* GAME_APPLICATION_H */