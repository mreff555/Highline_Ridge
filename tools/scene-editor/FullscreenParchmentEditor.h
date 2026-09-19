/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Full-screen writing-desk parchment editor for prose / TTS dialog fields.
 ******************************************************************************/

#ifndef TIMBERLINE_FULLSCREEN_PARCHMENT_EDITOR_H
#define TIMBERLINE_FULLSCREEN_PARCHMENT_EDITOR_H

#include <functional>
#include <string>

#include <raylib.h>

namespace timberline_editor
{

/**
 * Immersive fullscreen text editor: ironwood desk background, centered
 * yellowed parchment, Caveat script type, optional TTS syntax colors,
 * brass Confirm / Cancel nameplates.
 */
struct FullscreenParchmentEditor
{
    bool open = false;
    int ignoreInputFrames = 0;

    /** Draft buffer while open; Confirm copies into bindTarget. */
    std::string draft;
    std::string* bindTarget = nullptr;
    bool highlightTts = false;
    std::string hintLabel;

    Font scriptFont{};
    Texture2D deskTexture{};
    bool deskLoaded = false;
    bool scriptLoaded = false;

    int cursor = 0;
    int selectAnchor = -1;
    float scrollY = 0.0f;
    float preferX = -1.0f;
    Rectangle lastParchment{0, 0, 0, 0};
    Rectangle lastTextArea{0, 0, 0, 0};
    Rectangle confirmBtn{0, 0, 0, 0};
    Rectangle cancelBtn{0, 0, 0, 0};

    std::function<void()> onClosed;

    bool blocksInput() const { return open; }

    void loadAssets(const std::string& resourceDir, const std::string& assetRoot);
    void unloadAssets();

    /** Open editing *target. highlightTts enables TTS markup coloring. */
    void openEditor(
        std::string* target,
        bool ttsHighlight,
        const std::string& label,
        const std::string& resourceDir,
        const std::string& assetRoot);

    void confirm();
    void cancel();

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    void typeIntoDraft();
    void drawBrassPlate(
        Font font,
        Rectangle plate,
        const char* label,
        bool hovered,
        bool pressed) const;
};

} // namespace timberline_editor

#endif
