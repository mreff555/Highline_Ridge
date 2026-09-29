/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Options → Configure API keys (session memory only, #56).
 ******************************************************************************/

#ifndef TIMBERLINE_API_KEYS_DIALOG_H
#define TIMBERLINE_API_KEYS_DIALOG_H

#include "EditorApiKeys.h"

#include <string>

#include <raylib.h>

namespace timberline_editor
{

struct ApiKeysDialog
{
    EditorApiKeys* keys = nullptr;
    Font uiFont{};
    Font uiFontBold{};

    bool open = false;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;

    /** Drafts edited in the dialog; applied on Confirm only. */
    std::string draftXai;
    std::string draftElevenLabs;
    int focusField = 0; // 0=xAI, 1=ElevenLabs

    std::string status;
    std::string error;

    void openDialog();
    void closeDialog(bool apply);
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    void typeIntoFocused();
};

} // namespace timberline_editor

#endif
