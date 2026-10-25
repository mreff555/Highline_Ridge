/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Full-screen writing-desk parchment editor for prose / TTS dialog fields.
 ******************************************************************************/

#ifndef TIMBERLINE_FULLSCREEN_PARCHMENT_EDITOR_H
#define TIMBERLINE_FULLSCREEN_PARCHMENT_EDITOR_H

#include "EditorApiKeys.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <raylib.h>

namespace timberline_editor
{

/**
 * Immersive fullscreen text editor: ironwood desk background, centered
 * yellowed parchment, readable script type, optional TTS syntax colors,
 * standard Confirm / Cancel editor buttons.
 *
 * When editing a TTS field (highlightTts):
 * - **Generate TTS** — rewrite companion on-screen prose into spoken markup
 * - **Generate voice** — bake the current markup to MP3 via xAI TTS
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

    /**
     * On-screen / dialog prose used as the Generate TTS source.
     * Callers pass the companion text field (not the TTS buffer).
     */
    std::string companionPlainText;

    /**
     * Voice bake context (Generate voice). Empty voiceId = Off — cannot bake.
     * audioRelPath is under the game root; empty → .authoring/parchment_voice.mp3.
     * If bindAudioPath is set, success writes the relative path there.
     */
    std::string voiceId;
    std::string audioRelPath;
    std::string* bindAudioPath = nullptr;
    /** Optional: multi-voice segment rel paths (cleared when single-file). */
    std::vector<std::string>* bindAudioSegments = nullptr;

    /** Session xAI key (Options → API keys). Owned elsewhere. */
    EditorApiKeys* sessionKeys = nullptr;

    Font scriptFont{};
    Texture2D deskTexture{};
    bool deskLoaded = false;
    bool scriptLoaded = false;

    int cursor = 0;
    int selectAnchor = -1; // -1 = no selection; else range with cursor
    bool mouseSelecting = false;
    float scrollY = 0.0f;
    float preferX = -1.0f;
    Rectangle lastParchment{0, 0, 0, 0};
    Rectangle lastTextArea{0, 0, 0, 0};
    Rectangle confirmBtn{0, 0, 0, 0};
    Rectangle cancelBtn{0, 0, 0, 0};
    Rectangle generateTtsBtn{0, 0, 0, 0};
    Rectangle generateVoiceBtn{0, 0, 0, 0};

    std::function<void()> onClosed;

    bool blocksInput() const { return open; }
    bool aiBusy() const { return translateBusy || voiceBusy; }

    void loadAssets(const std::string& resourceDir, const std::string& assetRoot);
    void unloadAssets();

    /**
     * Open editing *target. highlightTts enables TTS markup coloring and the
     * Generate TTS / Generate voice controls. companionPlain is the on-screen
     * dialog used as the Generate TTS source.
     */
    void openEditor(
        std::string* target,
        bool ttsHighlight,
        const std::string& label,
        const std::string& resourceDir,
        const std::string& assetRoot,
        const std::string& companionPlain = {},
        const std::string& bakeVoiceId = {},
        const std::string& bakeAudioRelPath = {},
        std::string* bakeAudioBind = nullptr,
        std::vector<std::string>* bakeAudioSegmentsBind = nullptr);

    void confirm();
    void cancel();

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);

private:
    void typeIntoDraft();
    void layoutChrome(int screenW, int screenH);
    bool hasSelection() const;
    void selectionRange(int& outStart, int& outEnd) const;
    bool deleteSelection();

    void startGenerateTts();
    void startGenerateVoice();
    void pollAiResults();
    void joinAiThreads();

    std::string storedResourceDir;
    std::string storedAssetRoot;

    bool translateBusy = false;
    bool translateResultPending = false;
    std::string translateResultText;
    std::string translateError;
    std::mutex translateMutex;
    std::thread translateThread;
    std::atomic<bool> translateCancel{false};

    bool voiceBusy = false;
    bool voiceResultPending = false;
    std::string voiceResultPath;
    std::vector<std::string> voiceResultSegments;
    std::string voiceError;
    std::mutex voiceMutex;
    std::thread voiceThread;
    std::atomic<bool> voiceCancel{false};

    std::string aiStatus;
};

} // namespace timberline_editor

#endif
