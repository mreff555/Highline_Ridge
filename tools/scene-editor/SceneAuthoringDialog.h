/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * New Scene dialog for the scenes tab (manual fields + AI generate + TTS).
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
    bool editingExisting = false;
    /** Id shown in the id field. Confirm (edit) ignores this unless Rename was used. */
    std::string idDraft;
    int ignoreInputFrames = 0;
    bool waitMouseRelease = false;
    float scrollY = 0.0f;
    float lastContentHeight = 0.0f;
    Rectangle lastContentRect{0, 0, 0, 0};
    Rectangle lastFormScrollTrack{0, 0, 0, 0};
    Rectangle lastFormScrollThumb{0, 0, 0, 0};
    bool draggingFormScroll = false;
    float formScrollGrabOffset = 0.0f;

    SceneAuthoringPayload payload{};
    std::string sessionApiKey;
    std::string status;
    std::string error;
    // 0=id, 1=description, 2=examine, 3=api key, 4=image, 5=ambient, 6=music,
    // 7=tts description, 8=tts examine
    int focusField = 0;

    /** Caret + selection + per-field scroll for multiline fields. */
    struct MultilineState
    {
        int cursor = 0;
        int selectAnchor = -1; // -1 = no selection; else [min(anchor,cursor), max(...))
        bool mouseSelecting = false;
        bool draggingScroll = false;
        float scrollGrabOffset = 0.0f;
        float scrollY = 0.0f;
        float preferX = -1.0f;
        float lastWrapWidth = 400.0f;
        float lastContentH = 0.0f;
        float lastViewH = 0.0f;
        float lastMaxScroll = 0.0f;
        Rectangle lastField{0, 0, 0, 0};
        Rectangle lastScrollTrack{0, 0, 0, 0};
        Rectangle lastScrollThumb{0, 0, 0, 0};
    };
    MultilineState descriptionEdit{};
    MultilineState examineEdit{};
    MultilineState ttsDescriptionEdit{};
    MultilineState ttsExamineEdit{};

    /** Single-line caret (id / api key / paths). */
    struct SingleLineState
    {
        int cursor = 0;
        Rectangle lastField{0, 0, 0, 0};
    };
    SingleLineState idEdit{};
    SingleLineState keyEdit{};
    SingleLineState imageEdit{};
    SingleLineState ambientEdit{};
    SingleLineState musicEdit{};

    bool voiceMenuOpen = false;
    Rectangle voiceBtnRect{0, 0, 0, 0};
    Rectangle voiceMenuRect{0, 0, 0, 0};
    Rectangle ttsSwitchTrack{0, 0, 0, 0};

    enum class ApiKeyValidity
    {
        Missing,
        Unknown,
        Valid,
        Invalid
    };
    ApiKeyValidity apiKeyValidity = ApiKeyValidity::Missing;
    std::string apiKeyValidatedFingerprint;
    double apiKeyNextCheckTime = 0.0;
    std::atomic<int> apiKeyCheckResult{-1}; // -1 idle, 0 invalid, 1 valid
    std::string apiKeyCheckFingerprint;
    std::mutex apiKeyMutex;
    std::thread apiKeyThread;

    std::atomic<bool> generateBusy{false};
    std::atomic<bool> generateCancel{false};
    std::atomic<int> generateTarget{0};
    std::mutex generateMutex;
    std::string generateResultStatus;
    bool generateResultPending = false;
    std::thread generateThread;
    bool pendingVoiceRefresh = false;

    void openDialog();
    void openEditDialog(const std::string& sceneId);
    void closeDialog();
    bool blocksInput() const { return open; }

    void handleInput(int screenW, int screenH);
    void draw(int screenW, int screenH);
    void pollGenerateResult();

private:
    void commitCreate(bool runAi, int aiTarget);
    void commitSave(bool runAi, int aiTarget);
    void applyRename();
    bool canEnableCreate() const;
    bool canEnableRename() const;
    void startGenerate(int aiTarget);
    void startVoiceRefresh();
    void requestCancelGenerate();
    void drawWorkingOverlay(int screenW, int screenH, Font font, Font bold);
    void typeIntoFocusedField();
    void handleMultilineNavigation(std::string& buffer, MultilineState& state, Font font, float fontSize);
    void handleSingleLineNavigation(std::string& buffer, SingleLineState& state);
    void setMultilineCursor(MultilineState& state, int pos, bool extendSelection, int bufferSize);
    bool deleteMultilineSelection(std::string& buffer, MultilineState& state);
    void drawMultilineField(
        Font font,
        Rectangle field,
        const std::string& buffer,
        const char* placeholder,
        MultilineState& state,
        bool focused) const;
    void drawSingleLineField(
        Font font,
        Rectangle field,
        const std::string& buffer,
        const char* placeholder,
        SingleLineState& state,
        bool focused,
        float fontSize) const;
    void ensureMultilineCursorVisible(
        Font font,
        const std::string& buffer,
        MultilineState& state,
        Rectangle field,
        float fontSize) const;
    float estimateFormContentHeight() const;
    SingleLineState* singleLineStateForFocus(int field);
    std::string* singleLineBufferForFocus(int field);
    void pollApiKeyValidity();
    void scheduleApiKeyCheck(const std::string& key);
    std::string effectiveApiKey() const;
    void syncSpeakWithTts();
    bool handleVoiceMenuClick(Vector2 mouse);
    void drawVoiceMenu(Font font);
};

} // namespace timberline_editor

#endif
