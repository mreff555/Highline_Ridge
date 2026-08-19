/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * New Scene dialog for the scenes tab (manual fields + AI generate).
 ******************************************************************************/

#ifndef TIMBERLINE_SCENE_AUTHORING_DIALOG_H
#define TIMBERLINE_SCENE_AUTHORING_DIALOG_H

#include "DocumentWorkspace.h"
#include "SceneAuthoring.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <raylib.h>

namespace timberline_editor
{

struct SceneAuthoringDialog
{
    DocumentWorkspace* docs = nullptr;
    std::function<void(const std::string&)> onCreated; // select new scene
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;
    float scrollY = 0.0f;

    SceneAuthoringPayload payload{};
    std::string sessionApiKey;
    std::string status;
    std::string error;
    int focusField = 0; // 0=id, 1=description, 2=examine, 3=api key, 4=image, 5=ambient, 6=music

    std::atomic<bool> generateBusy{false};
    std::atomic<int> generateTarget{0};
    std::mutex generateMutex;
    std::string generateResultStatus;
    bool generateResultPending = false;
    std::thread generateThread;

    void openDialog();
    void closeDialog();
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);
    void pollGenerateResult();

private:
    void commitCreate(bool runAi, int aiTarget);
    void startGenerate(int aiTarget);
    void typeIntoFocusedField();
};

} // namespace timberline_editor

#endif
