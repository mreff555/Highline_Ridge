/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "SceneAuthoringDialog.h"
#include "EditorInput.h"
#include "EditorButton.h"
#include "EditorPrefs.h"
#include "EditorTheme.h"
#include "EditorUiDraw.h"
#include "PlatformPath.h"
#include "TtsVoiceMarkup.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using timberline_engine::builtinVoiceIds;
using timberline_engine::isKnownBuiltinVoiceId;
using timberline_engine::normalizeVoiceId;
using timberline_engine::pathJoin;

namespace timberline_editor
{

namespace
{

void insertUtf8At(std::string& buffer, int& cursor, int codepoint)
{
    if (codepoint <= 0)
        return;
    cursor = std::clamp(cursor, 0, static_cast<int>(buffer.size()));
    char bytes[5] = {};
    int size = 0;
    if (codepoint < 0x80)
    {
        bytes[0] = static_cast<char>(codepoint);
        size = 1;
    }
    else if (codepoint <= 0x7FF)
    {
        bytes[0] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
        bytes[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        bytes[0] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 3;
    }
    else
    {
        bytes[0] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    }
    buffer.insert(static_cast<size_t>(cursor), bytes, static_cast<size_t>(size));
    cursor += size;
}

void backspaceAt(std::string& buffer, int& cursor)
{
    if (cursor <= 0 || buffer.empty())
        return;
    const int prev = utf8PrevIndex(buffer, cursor);
    buffer.erase(static_cast<size_t>(prev), static_cast<size_t>(cursor - prev));
    cursor = prev;
}

void drawFieldBox(Rectangle r, bool focused)
{
    DrawRectangleRec(r, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(r, 1.0f, focused ? kPanelBorder : kPanelInnerEdge);
}

void drawLabel(Font font, const char* text, float x, float y)
{
    DrawTextEx(font, text, {x, y}, kFontTiny, 1.0f, kTextMuted);
}

Color fadedPlaceholderColor()
{
    // Dimmer than kTextMuted so hints stay secondary until the user types.
    return Color{90, 84, 78, 200};
}

bool multilineHasSelection(const SceneAuthoringDialog::MultilineState& state)
{
    return state.selectAnchor >= 0 && state.selectAnchor != state.cursor;
}

void multilineSelectionRange(
    const SceneAuthoringDialog::MultilineState& state,
    int bufferSize,
    int& outStart,
    int& outEnd)
{
    outStart = std::min(state.selectAnchor, state.cursor);
    outEnd = std::max(state.selectAnchor, state.cursor);
    outStart = std::clamp(outStart, 0, bufferSize);
    outEnd = std::clamp(outEnd, 0, bufferSize);
    if (outStart > outEnd)
        outStart = outEnd;
}

std::string shellSingleQuote(const std::string& value)
{
    std::string out = "'";
    for (char ch : value)
    {
        if (ch == '\'')
            out += "'\\''";
        else
            out += ch;
    }
    out += "'";
    return out;
}

std::string trimAscii(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size()
           && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    size_t end = value.size();
    while (end > begin
           && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(begin, end - begin);
}

std::string readApiKeyFile(const std::string& path)
{
    std::ifstream in(path.c_str());
    if (!in.is_open())
        return {};
    std::stringstream buffer;
    buffer << in.rdbuf();
    return trimAscii(buffer.str());
}

} // namespace

void SceneAuthoringDialog::syncSpeakWithTts()
{
    payload.speakEnabled = payload.ttsEnabled;
}

std::string SceneAuthoringDialog::effectiveApiKey() const
{
    if (!sessionApiKey.empty())
        return sessionApiKey;
    if (const char* env = std::getenv("XAI_API_KEY");
        env != nullptr && env[0] != '\0')
        return std::string(env);
    if (docs != nullptr)
    {
        const std::string root = docs->assetRoot.empty() ? "." : docs->assetRoot;
        const std::string a = pathJoin(pathJoin(root, "resources"), "xai_api_key");
        const std::string b = pathJoin(root, "xai_api_key");
        const std::string fromA = readApiKeyFile(a);
        if (!fromA.empty())
            return fromA;
        const std::string fromB = readApiKeyFile(b);
        if (!fromB.empty())
            return fromB;
        if (!docs->resourceDir.empty())
        {
            const std::string fromRes =
                readApiKeyFile(pathJoin(docs->resourceDir, "xai_api_key"));
            if (!fromRes.empty())
                return fromRes;
        }
    }
    return {};
}

void SceneAuthoringDialog::scheduleApiKeyCheck(const std::string& key)
{
    if (key.empty())
        return;
    if (apiKeyThread.joinable())
        return;

    apiKeyCheckResult.store(-1);
    {
        std::lock_guard<std::mutex> lock(apiKeyMutex);
        apiKeyCheckFingerprint = key;
    }
    apiKeyThread = std::thread([this, key]() {
        // Short curl probe; Authorization via shell-quoted bearer token.
        std::ostringstream safe;
        safe << "curl -sS -o /dev/null -w \"%{http_code}\" --max-time 8 "
             << "-H " << shellSingleQuote("Authorization: Bearer " + key) << " "
             << "https://api.x.ai/v1/models 2>/dev/null";
        int valid = 0;
#if !defined(_WIN32)
        FILE* pipe = popen(safe.str().c_str(), "r");
        if (pipe != nullptr)
        {
            char buf[32] = {};
            const char* got = fgets(buf, sizeof(buf), pipe);
            (void)pclose(pipe);
            if (got != nullptr && std::atoi(buf) == 200)
                valid = 1;
        }
#else
        (void)safe;
#endif
        apiKeyCheckResult.store(valid);
    });
}

void SceneAuthoringDialog::pollApiKeyValidity()
{
    const std::string key = effectiveApiKey();
    if (key.empty())
    {
        apiKeyValidity = ApiKeyValidity::Missing;
        apiKeyValidatedFingerprint.clear();
        if (apiKeyThread.joinable())
        {
            // Let in-flight check finish so we can join cleanly next open/close.
            const int pending = apiKeyCheckResult.load();
            if (pending == 0 || pending == 1)
                apiKeyThread.join();
        }
        return;
    }

    if (apiKeyThread.joinable())
    {
        const int pending = apiKeyCheckResult.load();
        if (pending == 0 || pending == 1)
        {
            apiKeyThread.join();
            std::string checked;
            {
                std::lock_guard<std::mutex> lock(apiKeyMutex);
                checked = apiKeyCheckFingerprint;
            }
            if (checked == key)
            {
                apiKeyValidity =
                    (pending == 1) ? ApiKeyValidity::Valid : ApiKeyValidity::Invalid;
                apiKeyValidatedFingerprint = key;
            }
            apiKeyCheckResult.store(-1);
        }
    }

    if (apiKeyValidatedFingerprint != key)
        apiKeyValidity = ApiKeyValidity::Unknown;

    const double now = GetTime();
    if (now >= apiKeyNextCheckTime)
    {
        apiKeyNextCheckTime = now + 1.0;
        if (apiKeyValidatedFingerprint != key && !apiKeyThread.joinable())
            scheduleApiKeyCheck(key);
    }
}

void SceneAuthoringDialog::openDialog()
{
    open = true;
    editingExisting = false;
    ignoreInputFrames = 1;
    waitMouseRelease = true;
    scrollY = 0.0f;
    lastContentHeight = 0.0f;
    lastContentRect = {0, 0, 0, 0};
    focusField = 0;
    error.clear();
    status.clear();
    payload = SceneAuthoringPayload{};
    payload.ttsEnabled = false;
    idDraft.clear();
    syncSpeakWithTts();
    if (docs != nullptr && !docs->resourceDir.empty())
        payload.ttsDefaultVoice = preferredTtsDefaultVoice(docs->resourceDir);
    else
        payload.ttsDefaultVoice = normalizeVoiceId(payload.ttsDefaultVoice);

    sessionApiKey.clear();
    // Prefer putting an available key into the field so generation has a visible key.
    if (const char* env = std::getenv("XAI_API_KEY");
        env != nullptr && env[0] != '\0')
        sessionApiKey = env;

    descriptionEdit = MultilineState{};
    examineEdit = MultilineState{};
    ttsDescriptionEdit = MultilineState{};
    ttsExamineEdit = MultilineState{};
    idEdit = SingleLineState{};
    keyEdit = SingleLineState{};
    imageEdit = SingleLineState{};
    ambientEdit = SingleLineState{};
    musicEdit = SingleLineState{};
    voiceMenuOpen = false;
    voiceBtnRect = {0, 0, 0, 0};
    voiceMenuRect = {0, 0, 0, 0};
    ttsSwitchTrack = {0, 0, 0, 0};
    lastFormScrollTrack = {0, 0, 0, 0};
    lastFormScrollThumb = {0, 0, 0, 0};
    draggingFormScroll = false;
    formScrollGrabOffset = 0.0f;
    apiKeyValidity = ApiKeyValidity::Missing;
    apiKeyValidatedFingerprint.clear();
    apiKeyNextCheckTime = 0.0;
    apiKeyCheckResult.store(-1);
    pendingVoiceRefresh = false;
    generateCancel.store(false);
}

void SceneAuthoringDialog::openEditDialog(const std::string& sceneId)
{
    if (docs == nullptr || sceneId.empty())
        return;

    SceneAuthoringPayload loaded{};
    if (!fillPayloadFromScene(*docs, sceneId, loaded))
        return;

    openDialog();
    editingExisting = true;
    payload = loaded;
    idDraft = payload.id;
    syncSpeakWithTts();
    if (docs != nullptr && !docs->resourceDir.empty())
        payload.ttsDefaultVoice = normalizeVoiceId(
            payload.ttsDefaultVoice.empty()
                ? preferredTtsDefaultVoice(docs->resourceDir)
                : payload.ttsDefaultVoice);
    else
        payload.ttsDefaultVoice = normalizeVoiceId(payload.ttsDefaultVoice);
    // Start on description; id field is editable for Rename only.
    focusField = 1;
    idEdit.cursor = static_cast<int>(idDraft.size());
    descriptionEdit.cursor = static_cast<int>(payload.description.size());
    examineEdit.cursor = static_cast<int>(payload.examineDetails.size());
    imageEdit.cursor = static_cast<int>(payload.imagePath.size());
    ambientEdit.cursor = static_cast<int>(payload.ambientPath.size());
    musicEdit.cursor = static_cast<int>(payload.musicPath.size());
    ttsDescriptionEdit.cursor = static_cast<int>(payload.ttsDescription.size());
    ttsExamineEdit.cursor = static_cast<int>(payload.ttsExamineDetails.size());
}

void SceneAuthoringDialog::setMultilineCursor(
    MultilineState& state,
    int pos,
    bool extendSelection,
    int bufferSize)
{
    pos = std::clamp(pos, 0, bufferSize);
    if (extendSelection)
    {
        if (state.selectAnchor < 0)
            state.selectAnchor = state.cursor;
    }
    else
        state.selectAnchor = -1;
    state.cursor = pos;
}

bool SceneAuthoringDialog::deleteMultilineSelection(std::string& buffer, MultilineState& state)
{
    if (!multilineHasSelection(state))
        return false;
    int start = 0;
    int end = 0;
    multilineSelectionRange(state, static_cast<int>(buffer.size()), start, end);
    buffer.erase(static_cast<size_t>(start), static_cast<size_t>(end - start));
    state.cursor = start;
    state.selectAnchor = -1;
    state.preferX = -1.0f;
    return true;
}

void SceneAuthoringDialog::requestCancelGenerate()
{
    if (!generateBusy.load())
        return;
    generateCancel.store(true);
}

void SceneAuthoringDialog::closeDialog()
{
    requestCancelGenerate();
    if (generateThread.joinable())
        generateThread.join();
    generateBusy = false;
    generateCancel.store(false);
    if (apiKeyThread.joinable())
        apiKeyThread.join();
    apiKeyCheckResult.store(-1);
    voiceMenuOpen = false;
    open = false;
    editingExisting = false;
    error.clear();
}

void SceneAuthoringDialog::pollGenerateResult()
{
    if (!generateResultPending)
        return;

    bool chainVoiceRefresh = false;
    bool wasCancelled = false;
    {
        std::lock_guard<std::mutex> lock(generateMutex);
        if (!generateResultPending)
            return;
        status = generateResultStatus;
        wasCancelled = (generateResultStatus.find("Cancel") != std::string::npos);
        generateResultPending = false;
        generateBusy = false;
        generateCancel.store(false);
        const int finishedTarget = generateTarget.load();
        generateTarget = 0;
        if (!wasCancelled && docs != nullptr && !payload.id.empty())
            applySceneAiOutputsToPayload(payload, *docs, payload.id);
        if (!wasCancelled && onCreated && !payload.id.empty() && docs != nullptr
            && docs->scenes.hasScene(payload.id))
            onCreated(payload.id);

        // After Generate all, optionally chain voice refresh (outside this lock).
        if (!wasCancelled && pendingVoiceRefresh
            && finishedTarget != 10
            && (sceneTtsTextHasWord(payload.ttsDescription)
                || sceneTtsTextHasWord(payload.ttsExamineDetails)))
            chainVoiceRefresh = true;
        pendingVoiceRefresh = false;
    }

    if (chainVoiceRefresh)
        startVoiceRefresh();
}

void SceneAuthoringDialog::ensureMultilineCursorVisible(
    Font font,
    const std::string& buffer,
    MultilineState& state,
    Rectangle field,
    float fontSize) const
{
    const auto& cfg = editorButtons();
    const float padX = cfg.textFieldPadX;
    const float padY = std::max(cfg.textFieldPadY, cfg.textFieldPadX + 4.0f);
    const float gutter = cfg.textFieldScrollGutter;
    const float wrapW = std::max(8.0f, field.width - padX - gutter - 4.0f);
    const float lineHeight = fontSize + 3.0f;
    const auto lines = layoutWrappedTextLines(font, buffer, wrapW, fontSize);
    const int lineIndex = visualLineIndexForCursor(
        lines, state.cursor, static_cast<int>(buffer.size()));
    const float caretY = static_cast<float>(lineIndex) * lineHeight;
    const float viewH = std::max(8.0f, field.height - padY * 2.0f);
    if (caretY < state.scrollY)
        state.scrollY = caretY;
    if (caretY + lineHeight > state.scrollY + viewH)
        state.scrollY = caretY + lineHeight - viewH;
    const float contentH = static_cast<float>(lines.size()) * lineHeight;
    const float maxScroll = std::max(0.0f, contentH - viewH);
    state.scrollY = std::clamp(state.scrollY, 0.0f, maxScroll);
}

void SceneAuthoringDialog::handleMultilineNavigation(
    std::string& buffer,
    MultilineState& state,
    Font font,
    float fontSize)
{
    const int bufSize = static_cast<int>(buffer.size());
    state.cursor = std::clamp(state.cursor, 0, bufSize);

    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool left = IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT);
    const bool right = IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT);
    const bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
    const bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);
    if (!left && !right && !up && !down)
        return;

    // Without Shift, Left/Right collapse selection to an edge first.
    if (!shift && multilineHasSelection(state) && (left || right))
    {
        int start = 0;
        int end = 0;
        multilineSelectionRange(state, bufSize, start, end);
        state.cursor = left ? start : end;
        state.selectAnchor = -1;
        state.preferX = -1.0f;
        return;
    }

    if (left || right)
        state.preferX = -1.0f;

    if (left)
        setMultilineCursor(state, utf8PrevIndex(buffer, state.cursor), shift, bufSize);
    else if (right)
        setMultilineCursor(state, utf8NextIndex(buffer, state.cursor), shift, bufSize);
    else
    {
        if (shift && state.selectAnchor < 0)
            state.selectAnchor = state.cursor;
        const float navWrap = std::max(8.0f, state.lastWrapWidth);
        const auto lines = layoutWrappedTextLines(font, buffer, navWrap, fontSize);
        const int next = moveCursorVertical(
            font,
            lines,
            buffer,
            state.cursor,
            up ? -1 : 1,
            fontSize,
            state.preferX);
        setMultilineCursor(state, next, shift, bufSize);
    }
}

float SceneAuthoringDialog::estimateFormContentHeight() const
{
    // Must stay in sync with draw() layout spacing.
    float h = 12.0f;          // top pad
    h += 16.0f + 32.0f + 12.0f; // id
    h += 16.0f + 110.0f + 12.0f; // description
    h += 16.0f + 88.0f + 12.0f; // examine
    h += 16.0f + 32.0f + 12.0f; // api key
    h += 3.0f * (16.0f + 32.0f + 12.0f); // image/ambient/music
    h += 16.0f + 26.0f + 12.0f; // TTS switch (+ label)
    if (payload.ttsEnabled)
    {
        h += 16.0f + 28.0f + 8.0f;   // default voice
        h += 16.0f + 100.0f + 10.0f; // TTS description
        h += 28.0f + 12.0f;          // gen buttons
        h += 16.0f + 88.0f + 10.0f;  // TTS examine
        h += 28.0f + 12.0f;          // gen buttons
    }
    h += 24.0f; // bottom pad so last control clears the clip
    return h;
}

SceneAuthoringDialog::SingleLineState* SceneAuthoringDialog::singleLineStateForFocus(int field)
{
    switch (field)
    {
    case 0:
        return &idEdit;
    case 3:
        return &keyEdit;
    case 4:
        return &imageEdit;
    case 5:
        return &ambientEdit;
    case 6:
        return &musicEdit;
    default:
        return nullptr;
    }
}

std::string* SceneAuthoringDialog::singleLineBufferForFocus(int field)
{
    switch (field)
    {
    case 0:
        return &idDraft;
    case 3:
        return &sessionApiKey;
    case 4:
        return &payload.imagePath;
    case 5:
        return &payload.ambientPath;
    case 6:
        return &payload.musicPath;
    default:
        return nullptr;
    }
}

void SceneAuthoringDialog::handleSingleLineNavigation(std::string& buffer, SingleLineState& state)
{
    const int n = static_cast<int>(buffer.size());
    state.cursor = std::clamp(state.cursor, 0, n);
    const bool left = IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT);
    const bool right = IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT);
    const bool home = IsKeyPressed(KEY_HOME);
    const bool end = IsKeyPressed(KEY_END);
    if (home)
        state.cursor = 0;
    else if (end)
        state.cursor = n;
    else if (left)
        state.cursor = utf8PrevIndex(buffer, state.cursor);
    else if (right)
        state.cursor = utf8NextIndex(buffer, state.cursor);
}

void SceneAuthoringDialog::drawSingleLineField(
    Font font,
    Rectangle field,
    const std::string& buffer,
    const char* placeholder,
    SingleLineState& state,
    bool focused,
    float fontSize) const
{
    const auto& cfg = editorButtons();
    const float padX = 8.0f;
    state.lastField = field;
    state.cursor = std::clamp(state.cursor, 0, static_cast<int>(buffer.size()));
    drawFieldBox(field, focused);

    const bool showPlaceholder = buffer.empty();
    const std::string& shown = showPlaceholder
        ? (placeholder != nullptr ? std::string(placeholder) : std::string{})
        : buffer;
    const Color color = showPlaceholder ? fadedPlaceholderColor() : kTextPrimary;
    const float textY = field.y + (field.height - fontSize) * 0.5f;

    // Do not Begin/EndScissorMode here — that would cancel the parent form
    // content scissor and let scrolled controls paint outside the dialog box.
    if (!shown.empty())
    {
        DrawTextEx(
            font,
            shown.c_str(),
            {field.x + padX, textY},
            fontSize,
            1.0f,
            color);
    }

    if (focused && !showPlaceholder && caretBlinkVisible(cfg.caretBlinkHz))
    {
        const std::string before = buffer.substr(0, static_cast<size_t>(state.cursor));
        const float caretX =
            field.x + padX + MeasureTextEx(font, before.c_str(), fontSize, 1.0f).x;
        DrawRectangleRec({caretX, textY, 2.0f, fontSize}, kPanelBorder);
    }
    else if (focused && showPlaceholder && caretBlinkVisible(cfg.caretBlinkHz))
    {
        DrawRectangleRec({field.x + padX, textY, 2.0f, fontSize}, kPanelBorder);
    }
}

namespace
{

int singleLineCursorFromClick(
    Font font,
    const std::string& buffer,
    Rectangle field,
    float fontSize,
    Vector2 mouse)
{
    const float padX = 8.0f;
    const float localX = mouse.x - field.x - padX;
    if (localX <= 0.0f || buffer.empty())
        return 0;
    int best = static_cast<int>(buffer.size());
    float bestDist = 1.0e9f;
    int i = 0;
    while (true)
    {
        const std::string prefix = buffer.substr(0, static_cast<size_t>(i));
        const float w = MeasureTextEx(font, prefix.c_str(), fontSize, 1.0f).x;
        const float dist = std::fabs(w - localX);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = i;
        }
        if (i >= static_cast<int>(buffer.size()))
            break;
        i = utf8NextIndex(buffer, i);
    }
    return best;
}

} // namespace

void SceneAuthoringDialog::drawMultilineField(
    Font font,
    Rectangle field,
    const std::string& buffer,
    const char* placeholder,
    MultilineState& state,
    bool focused) const
{
    const auto& cfg = editorButtons();
    const float padX = cfg.textFieldPadX;
    // Vertical inset: config padY, but never tighter than padX+4 — glyph tops
    // sit closer to the border than the bottom gap looks.
    const float padY = std::max(cfg.textFieldPadY, cfg.textFieldPadX + 4.0f);
    const float gutter = cfg.textFieldScrollGutter;
    const float fontSize = kFontSmall;
    const float lineHeight = fontSize + 3.0f;
    const float wrapW = std::max(8.0f, field.width - padX - gutter - 4.0f);
    state.lastWrapWidth = wrapW;
    state.lastField = field;
    state.cursor = std::clamp(state.cursor, 0, static_cast<int>(buffer.size()));

    drawFieldBox(field, focused);

    const bool showPlaceholder = buffer.empty();
    const std::vector<EditorVisualLine> lines = layoutWrappedTextLines(
        font,
        showPlaceholder ? std::string{} : buffer,
        wrapW,
        fontSize);

    const float contentH =
        std::max(lineHeight, static_cast<float>(std::max<size_t>(1, lines.size())) * lineHeight);
    const float viewH = std::max(8.0f, field.height - padY * 2.0f);
    const float maxScroll = std::max(0.0f, contentH - viewH);
    state.scrollY = std::clamp(state.scrollY, 0.0f, maxScroll);
    state.lastContentH = contentH;
    state.lastViewH = viewH;
    state.lastMaxScroll = maxScroll;

    BeginScissorMode(
        static_cast<int>(field.x),
        static_cast<int>(field.y),
        static_cast<int>(field.width - gutter),
        static_cast<int>(field.height));

    if (showPlaceholder)
    {
        DrawTextEx(
            font,
            placeholder != nullptr ? placeholder : "",
            {field.x + padX, field.y + padY},
            fontSize,
            1.0f,
            fadedPlaceholderColor());
    }
    else
    {
        int selStart = 0;
        int selEnd = 0;
        const bool hasSel = focused && multilineHasSelection(state);
        if (hasSel)
            multilineSelectionRange(state, static_cast<int>(buffer.size()), selStart, selEnd);

        for (size_t i = 0; i < lines.size(); ++i)
        {
            const float y = field.y + padY + static_cast<float>(i) * lineHeight - state.scrollY;
            if (y + lineHeight < field.y || y > field.y + field.height)
                continue;

            if (hasSel)
            {
                const int lineSelStart = std::max(selStart, lines[i].start);
                const int lineSelEnd = std::min(selEnd, lines[i].end);
                if (lineSelStart < lineSelEnd)
                {
                    const float x0 =
                        field.x + padX + caretXOnVisualLine(font, lines[i], lineSelStart, fontSize);
                    const float x1 =
                        field.x + padX + caretXOnVisualLine(font, lines[i], lineSelEnd, fontSize);
                    DrawRectangleRec(
                        {x0, y, std::max(2.0f, x1 - x0), fontSize + 2.0f},
                        Color{70, 90, 140, 180});
                }
            }

            if (!lines[i].text.empty())
            {
                DrawTextEx(
                    font,
                    lines[i].text.c_str(),
                    {field.x + padX, y},
                    fontSize,
                    1.0f,
                    kTextPrimary);
            }
        }
    }

    const bool showCaret =
        focused && !multilineHasSelection(state) && caretBlinkVisible(cfg.caretBlinkHz);
    if (showCaret)
    {
        const int lineIndex = visualLineIndexForCursor(
            lines, state.cursor, static_cast<int>(buffer.size()));
        const EditorVisualLine& line =
            lines.empty() ? EditorVisualLine{} : lines[static_cast<size_t>(lineIndex)];
        const float cx = field.x + padX
            + (showPlaceholder ? 0.0f : caretXOnVisualLine(font, line, state.cursor, fontSize));
        const float cy = field.y + padY + static_cast<float>(lineIndex) * lineHeight - state.scrollY;
        if (cy + fontSize >= field.y && cy <= field.y + field.height)
            DrawRectangleRec({cx, cy, 2.0f, fontSize}, kPanelBorder);
    }
    EndScissorMode();

    state.lastScrollTrack = {};
    state.lastScrollThumb = {};
    if (maxScroll > 0.5f)
    {
        const float trackX = field.x + field.width - gutter + 2.0f;
        const float trackY = field.y + padY;
        const float trackH = std::max(8.0f, field.height - padY * 2.0f);
        const float trackW = std::max(4.0f, gutter - 4.0f);
        state.lastScrollTrack = {trackX, trackY, trackW, trackH};
        DrawRectangleRec(state.lastScrollTrack, Color{30, 28, 38, 255});

        const float thumbH = std::max(16.0f, trackH * (viewH / std::max(viewH, contentH)));
        const float t = (maxScroll > 0.0f) ? (state.scrollY / maxScroll) : 0.0f;
        const float thumbY = trackY + t * (trackH - thumbH);
        state.lastScrollThumb = {trackX, thumbY, trackW, thumbH};
        DrawRectangleRec(
            state.lastScrollThumb,
            (focused || state.draggingScroll) ? kPanelBorder : kPanelAccent);
    }
}

void SceneAuthoringDialog::typeIntoFocusedField()
{
    std::string* target = nullptr;
    MultilineState* multi = nullptr;
    SingleLineState* single = singleLineStateForFocus(focusField);
    switch (focusField)
    {
    case 0:
        target = &idDraft;
        break;
    case 1:
        target = &payload.description;
        multi = &descriptionEdit;
        break;
    case 2:
        target = &payload.examineDetails;
        multi = &examineEdit;
        break;
    case 3:
        target = &sessionApiKey;
        break;
    case 4:
        target = &payload.imagePath;
        break;
    case 5:
        target = &payload.ambientPath;
        break;
    case 6:
        target = &payload.musicPath;
        break;
    case 7:
        target = &payload.ttsDescription;
        multi = &ttsDescriptionEdit;
        break;
    case 8:
        target = &payload.ttsExamineDetails;
        multi = &ttsExamineEdit;
        break;
    default:
        break;
    }
    if (target == nullptr)
        return;

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const float fontSize = kFontSmall;
    const int bufSize = static_cast<int>(target->size());

    if (multi != nullptr)
        handleMultilineNavigation(*target, *multi, font, fontSize);
    else if (single != nullptr)
        handleSingleLineNavigation(*target, *single);

    const bool mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

    if (multi != nullptr && mod && IsKeyPressed(KEY_A))
    {
        multi->selectAnchor = 0;
        multi->cursor = static_cast<int>(target->size());
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    if (multi != nullptr && mod && IsKeyPressed(KEY_C) && multilineHasSelection(*multi))
    {
        int start = 0;
        int end = 0;
        multilineSelectionRange(*multi, static_cast<int>(target->size()), start, end);
        SetClipboardText(
            target->substr(static_cast<size_t>(start), static_cast<size_t>(end - start)).c_str());
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    if (multi != nullptr && mod && IsKeyPressed(KEY_X) && multilineHasSelection(*multi))
    {
        int start = 0;
        int end = 0;
        multilineSelectionRange(*multi, static_cast<int>(target->size()), start, end);
        SetClipboardText(
            target->substr(static_cast<size_t>(start), static_cast<size_t>(end - start)).c_str());
        deleteMultilineSelection(*target, *multi);
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    // Cmd/Ctrl+V paste (replaces selection in multiline fields).
    if (mod && IsKeyPressed(KEY_V))
    {
        const char* clip = GetClipboardText();
        if (clip != nullptr && clip[0] != '\0')
        {
            std::string pasted = clip;
            if (focusField == 3 || single != nullptr)
            {
                pasted.erase(
                    std::remove_if(
                        pasted.begin(),
                        pasted.end(),
                        [](unsigned char ch) {
                            return ch == '\n' || ch == '\r';
                        }),
                    pasted.end());
                if (focusField == 3)
                {
                    *target = pasted;
                    if (single != nullptr)
                        single->cursor = static_cast<int>(target->size());
                }
                else if (single != nullptr)
                {
                    single->cursor =
                        std::clamp(single->cursor, 0, static_cast<int>(target->size()));
                    target->insert(static_cast<size_t>(single->cursor), pasted);
                    single->cursor += static_cast<int>(pasted.size());
                }
            }
            else if (multi != nullptr)
            {
                deleteMultilineSelection(*target, *multi);
                multi->cursor =
                    std::clamp(multi->cursor, 0, static_cast<int>(target->size()));
                target->insert(static_cast<size_t>(multi->cursor), pasted);
                multi->cursor += static_cast<int>(pasted.size());
                multi->selectAnchor = -1;
                multi->preferX = -1.0f;
            }
            else
                *target += pasted;
        }
        while (GetCharPressed() > 0)
        {
        }
        return;
    }

    int cursor = multi != nullptr
        ? std::clamp(multi->cursor, 0, static_cast<int>(target->size()))
        : (single != nullptr
               ? std::clamp(single->cursor, 0, static_cast<int>(target->size()))
               : static_cast<int>(target->size()));

    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (focusField == 0)
        {
            if (std::isalnum(static_cast<unsigned char>(cp)) || cp == '_' || cp == '-'
                || cp == ' ')
            {
                insertUtf8At(*target, cursor, cp);
            }
        }
        else if (cp == '\n')
        {
            if (focusField == 1 || focusField == 2 || focusField == 7 || focusField == 8)
            {
                if (multi != nullptr)
                    deleteMultilineSelection(*target, *multi);
                cursor = multi != nullptr ? multi->cursor : cursor;
                insertUtf8At(*target, cursor, '\n');
            }
        }
        else if (cp >= 32)
        {
            if (multi != nullptr)
                deleteMultilineSelection(*target, *multi);
            cursor = multi != nullptr
                ? multi->cursor
                : (single != nullptr ? single->cursor : cursor);
            insertUtf8At(*target, cursor, cp);
        }
        cp = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
    {
        if (multi != nullptr && deleteMultilineSelection(*target, *multi))
            cursor = multi->cursor;
        else
            backspaceAt(*target, cursor);
    }

    if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE))
    {
        if (multi != nullptr && deleteMultilineSelection(*target, *multi))
            cursor = multi->cursor;
        else if (cursor < static_cast<int>(target->size()))
        {
            const int next = utf8NextIndex(*target, cursor);
            target->erase(static_cast<size_t>(cursor), static_cast<size_t>(next - cursor));
        }
    }

    if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        && (focusField == 1 || focusField == 2 || focusField == 7 || focusField == 8))
    {
        if (multi != nullptr)
            deleteMultilineSelection(*target, *multi);
        cursor = multi != nullptr ? multi->cursor : cursor;
        insertUtf8At(*target, cursor, '\n');
    }

    if (multi != nullptr)
    {
        multi->cursor = cursor;
        // Typing clears selection unless we just extended via arrows (handled above).
        if (!IsKeyDown(KEY_LEFT_SHIFT) && !IsKeyDown(KEY_RIGHT_SHIFT)
            && (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_DELETE)
                || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)))
            multi->preferX = -1.0f;
    }
    else if (single != nullptr)
        single->cursor = cursor;

    // New Scene: keep payload.id aligned with the draft for path defaults / generate.
    if (!editingExisting && focusField == 0)
        payload.id = idDraft;
    (void)bufSize;
}

bool SceneAuthoringDialog::canEnableCreate() const
{
    if (editingExisting || docs == nullptr || generateBusy.load())
        return false;
    const std::string id = sanitizeSceneId(idDraft);
    if (!isValidSceneId(id) || docs->scenes.hasScene(id))
        return false;
    std::string desc = payload.description;
    // trim
    while (!desc.empty() && std::isspace(static_cast<unsigned char>(desc.front())))
        desc.erase(desc.begin());
    while (!desc.empty() && std::isspace(static_cast<unsigned char>(desc.back())))
        desc.pop_back();
    if (desc.empty())
        return false;
    const std::string root = docs->assetRoot.empty() ? "." : docs->assetRoot;
    return sceneImageReferenceExists(root, payload.imagePath);
}

bool SceneAuthoringDialog::canEnableRename() const
{
    if (!editingExisting || docs == nullptr || generateBusy.load())
        return false;
    const std::string next = sanitizeSceneId(idDraft);
    if (!isValidSceneId(next) || next == payload.id)
        return false;
    return !docs->scenes.hasScene(next);
}

void SceneAuthoringDialog::applyRename()
{
    if (docs == nullptr || !canEnableRename())
        return;
    error.clear();
    std::string err;
    const std::string oldId = payload.id;
    if (!renameSceneAuthoring(*docs, oldId, idDraft, &payload, err))
    {
        error = err;
        return;
    }
    idDraft = payload.id;
    idEdit.cursor = static_cast<int>(idDraft.size());
    status = "Renamed scene \"" + oldId + "\" → \"" + payload.id + "\"";
    if (onCreated)
        onCreated(payload.id);
}

void SceneAuthoringDialog::commitCreate(bool runAi, int aiTarget)
{
    commitSave(runAi, aiTarget);
}

void SceneAuthoringDialog::commitSave(bool runAi, int aiTarget)
{
    if (docs == nullptr)
        return;
    error.clear();
    // Confirm (edit) never applies a pending rename — only Rename does.
    if (!editingExisting)
        payload.id = sanitizeSceneId(idDraft);
    else
        payload.id = sanitizeSceneId(payload.id);
    syncSpeakWithTts();
    if (!editingExisting)
    {
        if (!canEnableCreate())
        {
            error = "Need a unique valid id, non-empty description, and an existing image path.";
            return;
        }
        if (docs->scenes.hasScene(payload.id))
        {
            error = "Scene id already exists: " + payload.id;
            return;
        }
    }
    SceneUpsertResult result = upsertScene(*docs, payload, aiTarget, runAi);
    if (!result.ok)
    {
        error = result.message;
        return;
    }
    status = result.message;
    if (runAi && !result.jobsFilePath.empty())
    {
        startGenerate(aiTarget);
        return;
    }
    if (onCreated)
        onCreated(payload.id);
    closeDialog();
}

void SceneAuthoringDialog::startGenerate(int aiTarget)
{
    if (docs == nullptr || generateBusy.load())
        return;

    payload.id = sanitizeSceneId(payload.id);
    if (!isValidSceneId(payload.id))
    {
        error = "Enter a valid scene id before generating.";
        return;
    }
    if (payload.description.empty())
    {
        error = "Description is required for AI context.";
        return;
    }

    // Ensure scene exists first (so the map can show it while AI runs).
    // When editing / regenerating an existing scene, rotate live assets to
    // name_1 / name_2 before the runner overwrites the live path.
    const bool rotateBackups = editingExisting || docs->scenes.hasScene(payload.id);
    if (!docs->scenes.hasScene(payload.id))
    {
        SceneUpsertResult result =
            upsertScene(*docs, payload, aiTarget, true, false);
        if (!result.ok)
        {
            error = result.message;
            return;
        }
        status = result.message;
        if (onCreated)
            onCreated(payload.id);
    }
    else
    {
        // Refresh authoring fields + rewrite jobs for this target.
        if (payload.imagePath.empty())
            payload.imagePath = "resources/images/" + payload.id + ".png";
        if (payload.ambientPath.empty())
            payload.ambientPath = "resources/audio/ambient/" + payload.id + ".mp3";
        if (payload.musicPath.empty())
            payload.musicPath = "resources/audio/music/" + payload.id + "_theme.mp3";
        SceneUpsertResult refresh =
            upsertScene(*docs, payload, aiTarget, true, rotateBackups);
        if (!refresh.ok)
        {
            error = refresh.message;
            return;
        }
        if (refresh.jobsFilePath.empty())
        {
            error = "Failed to write AI jobs file.";
            return;
        }
        status = "Jobs ready: " + refresh.jobsFilePath;
    }

    if (aiTarget == 0 && payload.ttsEnabled
        && apiKeyValidity == ApiKeyValidity::Valid)
        pendingVoiceRefresh = true;

    generateCancel.store(false);
    generateBusy = true;
    generateTarget = aiTarget;
    generateResultPending = false;
    const std::string keySnap = effectiveApiKey();
    const std::string idSnap = payload.id;
    const std::string assetRoot = docs->assetRoot;
    const std::string resourceDir = docs->resourceDir;
    if (generateThread.joinable())
        generateThread.join();
    generateThread = std::thread([this, keySnap, idSnap, assetRoot, resourceDir]() {
        const std::string msg = runSceneAuthoringAiJobs(
            assetRoot, resourceDir, idSnap, keySnap, &generateCancel);
        std::lock_guard<std::mutex> lock(generateMutex);
        generateResultStatus = msg;
        generateResultPending = true;
    });
}

void SceneAuthoringDialog::startVoiceRefresh()
{
    if (docs == nullptr || generateBusy.load())
        return;

    payload.id = sanitizeSceneId(payload.id);
    if (!isValidSceneId(payload.id))
    {
        error = "Enter a valid scene id before generating voice data.";
        return;
    }
    if (apiKeyValidity != ApiKeyValidity::Valid)
    {
        error = "Paste a valid xAI API key before generating voice data.";
        return;
    }
    if (!sceneTtsTextHasWord(payload.ttsDescription)
        && !sceneTtsTextHasWord(payload.ttsExamineDetails))
    {
        error = "Add TTS description or examine text before generating voice data.";
        return;
    }

    syncSpeakWithTts();
    const bool createdNow = !docs->scenes.hasScene(payload.id);
    {
        SceneUpsertResult result = upsertScene(*docs, payload, 0, false);
        if (!result.ok)
        {
            error = result.message;
            return;
        }
        status = result.message;
        if (createdNow && onCreated)
            onCreated(payload.id);
    }

    // When regenerating voices for an existing scene, rotate live TTS mp3s.
    if (editingExisting || !createdNow)
    {
        const std::string root =
            docs->assetRoot.empty() ? "." : docs->assetRoot;
        auto rotateBagAudio = [&](const char* bagKey) {
            const nlohmann::json* scene = docs->scenes.sceneJson(payload.id);
            if (scene == nullptr || !scene->is_object())
                return;
            if (!scene->contains(bagKey) || !(*scene)[bagKey].is_object())
                return;
            const std::string audio =
                (*scene)[bagKey].value("ttsAudio", std::string{});
            if (!audio.empty())
                rotateLiveAssetBackup(root, audio);
        };
        rotateBagAudio("descriptionTts");
        rotateBagAudio("examineTts");
    }

    pendingVoiceRefresh = false;
    generateCancel.store(false);
    generateBusy = true;
    generateTarget = 10;
    generateResultPending = false;
    const std::string keySnap = effectiveApiKey();
    const std::string idSnap = payload.id;
    const std::string assetRoot = docs->assetRoot;
    const std::string resourceDir = docs->resourceDir;
    if (generateThread.joinable())
        generateThread.join();
    generateThread = std::thread([this, keySnap, idSnap, assetRoot, resourceDir]() {
        const std::string msg = runSceneVoiceRefresh(
            assetRoot, resourceDir, idSnap, keySnap, &generateCancel);
        std::lock_guard<std::mutex> lock(generateMutex);
        generateResultStatus = msg;
        generateResultPending = true;
    });
}

bool SceneAuthoringDialog::handleVoiceMenuClick(Vector2 mouse)
{
    if (!voiceMenuOpen)
        return false;
    if (voiceMenuRect.width < 1.0f)
        return false;

    if (CheckCollisionPointRec(mouse, voiceMenuRect))
    {
        const std::vector<std::string>& voices = builtinVoiceIds();
        const float rowH = 22.0f;
        int i = static_cast<int>((mouse.y - voiceMenuRect.y - 2.0f) / rowH);
        if (i >= 0 && i < static_cast<int>(voices.size()))
        {
            payload.ttsDefaultVoice = normalizeVoiceId(voices[static_cast<size_t>(i)]);
            if (docs != nullptr && !docs->resourceDir.empty())
                rememberTtsDefaultVoice(docs->resourceDir, payload.ttsDefaultVoice);
        }
        voiceMenuOpen = false;
        ignoreInputFrames = 1;
        return true;
    }

    if (CheckCollisionPointRec(mouse, voiceBtnRect))
        return false;
    voiceMenuOpen = false;
    ignoreInputFrames = 1;
    return true;
}

void SceneAuthoringDialog::drawVoiceMenu(Font font)
{
    if (!voiceMenuOpen)
    {
        voiceMenuRect = {0, 0, 0, 0};
        return;
    }
    const std::vector<std::string>& voices = builtinVoiceIds();
    const float rowH = 22.0f;
    const float menuH = static_cast<float>(voices.size()) * rowH + 4.0f;
    voiceMenuRect = {
        voiceBtnRect.x,
        voiceBtnRect.y + voiceBtnRect.height + 2.0f,
        std::max(voiceBtnRect.width, 140.0f),
        menuH};
    if (lastContentRect.height > 1.0f
        && voiceMenuRect.y + voiceMenuRect.height
            > lastContentRect.y + lastContentRect.height - 4.0f)
        voiceMenuRect.y = voiceBtnRect.y - voiceMenuRect.height - 2.0f;

    DrawRectangleRec(voiceMenuRect, Color{36, 32, 44, 255});
    DrawRectangleLinesEx(voiceMenuRect, 1.0f, kPanelBorder);
    const Vector2 mouse = GetMousePosition();
    float my = voiceMenuRect.y + 2.0f;
    for (const std::string& v : voices)
    {
        const Rectangle row = {
            voiceMenuRect.x + 2.0f, my, voiceMenuRect.width - 4.0f, rowH - 2.0f};
        const bool hov = CheckCollisionPointRec(mouse, row);
        const bool selected = (v == payload.ttsDefaultVoice);
        if (selected)
            DrawRectangleRec(row, kSelection);
        else if (hov)
            DrawRectangleRec(row, Color{60, 54, 72, 220});
        DrawTextEx(
            font,
            v.c_str(),
            {row.x + 8.0f, row.y + 3.0f},
            kFontSmall,
            1.0f,
            kTextPrimary);
        my += rowH;
    }
}

void SceneAuthoringDialog::handleInput(int screenW, int screenH)
{
    (void)screenW;
    (void)screenH;
    if (!open)
        return;
    pollGenerateResult();
    pollApiKeyValidity();

    if (waitMouseRelease)
    {
        if (!editorMouseDown(MOUSE_BUTTON_LEFT))
            waitMouseRelease = false;
        return;
    }
    if (ignoreInputFrames > 0)
    {
        --ignoreInputFrames;
        return;
    }

    typeIntoFocusedField();

    // Scrollbar drag, click / drag selection (rects from last draw).
    const Vector2 mouse = GetMousePosition();
    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const float fontSize = kFontSmall;
    const auto& cfg = editorButtons();
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    auto handleScrollbarPress = [&](MultilineState& state, int fieldIndex) -> bool {
        if (state.lastMaxScroll <= 0.5f || state.lastScrollTrack.width <= 0.0f)
            return false;
        if (CheckCollisionPointRec(mouse, state.lastScrollThumb))
        {
            focusField = fieldIndex;
            state.draggingScroll = true;
            state.scrollGrabOffset = mouse.y - state.lastScrollThumb.y;
            state.mouseSelecting = false;
            return true;
        }
        if (CheckCollisionPointRec(mouse, state.lastScrollTrack))
        {
            focusField = fieldIndex;
            const float trackH = state.lastScrollTrack.height;
            const float thumbH = state.lastScrollThumb.height;
            const float ratio = (mouse.y - state.lastScrollTrack.y - thumbH * 0.5f)
                / std::max(1.0f, trackH - thumbH);
            state.scrollY = std::clamp(ratio, 0.0f, 1.0f) * state.lastMaxScroll;
            state.draggingScroll = true;
            state.scrollGrabOffset = thumbH * 0.5f;
            state.mouseSelecting = false;
            return true;
        }
        return false;
    };

    auto handleScrollbarDrag = [&](MultilineState& state) {
        if (!state.draggingScroll || state.lastMaxScroll <= 0.5f)
            return;
        const float trackH = state.lastScrollTrack.height;
        const float thumbH = std::max(16.0f, state.lastScrollThumb.height);
        const float y = mouse.y - state.scrollGrabOffset - state.lastScrollTrack.y;
        const float ratio = y / std::max(1.0f, trackH - thumbH);
        state.scrollY = std::clamp(ratio, 0.0f, 1.0f) * state.lastMaxScroll;
    };

    auto beginOrExtendClick = [&](MultilineState& state, std::string& buffer, int fieldIndex) {
        focusField = fieldIndex;
        const float wrapW = std::max(8.0f, state.lastWrapWidth);
        const auto lines = layoutWrappedTextLines(font, buffer, wrapW, fontSize);
        const int clickPos = cursorIndexFromClick(
            font,
            lines,
            buffer,
            state.lastField,
            cfg.textFieldPadX,
            fontSize,
            fontSize + 3.0f,
            state.scrollY,
            mouse);
        if (shift && state.selectAnchor < 0)
            state.selectAnchor = state.cursor;
        setMultilineCursor(state, clickPos, shift, static_cast<int>(buffer.size()));
        if (!shift)
        {
            // Start a fresh selection drag from this click.
            state.selectAnchor = clickPos;
            state.cursor = clickPos;
        }
        state.mouseSelecting = true;
        state.preferX = -1.0f;
    };

    if (!generateBusy.load() && editorMousePressed(MOUSE_BUTTON_LEFT))
    {
        draggingFormScroll = false;
        if (handleVoiceMenuClick(mouse))
        {
            // Voice menu consumed the click.
        }
        else if (lastFormScrollTrack.width > 1.0f
                 && (CheckCollisionPointRec(mouse, lastFormScrollThumb)
                     || CheckCollisionPointRec(mouse, lastFormScrollTrack)))
        {
            const float formMax =
                std::max(0.0f, lastContentHeight - lastContentRect.height);
            if (CheckCollisionPointRec(mouse, lastFormScrollThumb))
            {
                draggingFormScroll = true;
                formScrollGrabOffset = mouse.y - lastFormScrollThumb.y;
            }
            else
            {
                const float trackH = lastFormScrollTrack.height;
                const float thumbH = std::max(16.0f, lastFormScrollThumb.height);
                const float ratio = (mouse.y - lastFormScrollTrack.y - thumbH * 0.5f)
                    / std::max(1.0f, trackH - thumbH);
                scrollY = std::clamp(ratio, 0.0f, 1.0f) * formMax;
                draggingFormScroll = true;
                formScrollGrabOffset = thumbH * 0.5f;
            }
        }
        else
        {
            descriptionEdit.mouseSelecting = false;
            examineEdit.mouseSelecting = false;
            ttsDescriptionEdit.mouseSelecting = false;
            ttsExamineEdit.mouseSelecting = false;
            descriptionEdit.draggingScroll = false;
            examineEdit.draggingScroll = false;
            ttsDescriptionEdit.draggingScroll = false;
            ttsExamineEdit.draggingScroll = false;

            auto clickSingle = [&](SingleLineState& state, std::string& buffer, int index, float fontSz) {
                if (state.lastField.width <= 1.0f
                    || !CheckCollisionPointRec(mouse, state.lastField))
                    return false;
                // Ignore clicks on fields scrolled out of the content clip.
                if (lastContentRect.width > 1.0f
                    && !CheckCollisionPointRec(mouse, lastContentRect))
                    return false;
                focusField = index;
                state.cursor = singleLineCursorFromClick(
                    font, buffer, state.lastField, fontSz, mouse);
                return true;
            };

            if (handleScrollbarPress(descriptionEdit, 1))
            {
            }
            else if (handleScrollbarPress(examineEdit, 2))
            {
            }
            else if (handleScrollbarPress(ttsDescriptionEdit, 7))
            {
            }
            else if (handleScrollbarPress(ttsExamineEdit, 8))
            {
            }
            else if (descriptionEdit.lastField.width > 1.0f
                     && CheckCollisionPointRec(mouse, descriptionEdit.lastField)
                     && CheckCollisionPointRec(mouse, lastContentRect)
                     && !CheckCollisionPointRec(mouse, descriptionEdit.lastScrollTrack))
                beginOrExtendClick(descriptionEdit, payload.description, 1);
            else if (examineEdit.lastField.width > 1.0f
                     && CheckCollisionPointRec(mouse, examineEdit.lastField)
                     && CheckCollisionPointRec(mouse, lastContentRect)
                     && !CheckCollisionPointRec(mouse, examineEdit.lastScrollTrack))
                beginOrExtendClick(examineEdit, payload.examineDetails, 2);
            else if (ttsDescriptionEdit.lastField.width > 1.0f
                     && CheckCollisionPointRec(mouse, ttsDescriptionEdit.lastField)
                     && CheckCollisionPointRec(mouse, lastContentRect)
                     && !CheckCollisionPointRec(mouse, ttsDescriptionEdit.lastScrollTrack))
                beginOrExtendClick(ttsDescriptionEdit, payload.ttsDescription, 7);
            else if (ttsExamineEdit.lastField.width > 1.0f
                     && CheckCollisionPointRec(mouse, ttsExamineEdit.lastField)
                     && CheckCollisionPointRec(mouse, lastContentRect)
                     && !CheckCollisionPointRec(mouse, ttsExamineEdit.lastScrollTrack))
                beginOrExtendClick(ttsExamineEdit, payload.ttsExamineDetails, 8);
            else if (clickSingle(idEdit, idDraft, 0, kFontSmall)
                     || clickSingle(keyEdit, sessionApiKey, 3, kFontSmall)
                     || clickSingle(imageEdit, payload.imagePath, 4, kFontTiny)
                     || clickSingle(ambientEdit, payload.ambientPath, 5, kFontTiny)
                     || clickSingle(musicEdit, payload.musicPath, 6, kFontTiny))
            {
            }
            else
            {
                descriptionEdit.selectAnchor = -1;
                examineEdit.selectAnchor = -1;
                ttsDescriptionEdit.selectAnchor = -1;
                ttsExamineEdit.selectAnchor = -1;
            }
        }
    }

    if (editorMouseDown(MOUSE_BUTTON_LEFT))
    {
        if (draggingFormScroll && lastFormScrollTrack.height > 1.0f)
        {
            const float formMax =
                std::max(0.0f, lastContentHeight - lastContentRect.height);
            const float trackH = lastFormScrollTrack.height;
            const float thumbH = std::max(16.0f, lastFormScrollThumb.height);
            const float y = mouse.y - formScrollGrabOffset - lastFormScrollTrack.y;
            const float ratio = y / std::max(1.0f, trackH - thumbH);
            scrollY = std::clamp(ratio, 0.0f, 1.0f) * formMax;
        }
        handleScrollbarDrag(descriptionEdit);
        handleScrollbarDrag(examineEdit);
        handleScrollbarDrag(ttsDescriptionEdit);
        handleScrollbarDrag(ttsExamineEdit);

        auto dragSelect = [&](MultilineState& state, std::string& buffer) {
            if (!state.mouseSelecting || state.draggingScroll || state.lastField.width <= 1.0f)
                return;
            const float wrapW = std::max(8.0f, state.lastWrapWidth);
            const auto lines = layoutWrappedTextLines(font, buffer, wrapW, fontSize);
            const int pos = cursorIndexFromClick(
                font,
                lines,
                buffer,
                state.lastField,
                cfg.textFieldPadX,
                fontSize,
                fontSize + 3.0f,
                state.scrollY,
                mouse);
            if (state.selectAnchor < 0)
                state.selectAnchor = state.cursor;
            state.cursor = std::clamp(pos, 0, static_cast<int>(buffer.size()));
        };
        dragSelect(descriptionEdit, payload.description);
        dragSelect(examineEdit, payload.examineDetails);
        dragSelect(ttsDescriptionEdit, payload.ttsDescription);
        dragSelect(ttsExamineEdit, payload.ttsExamineDetails);
    }

    if (editorMouseReleased(MOUSE_BUTTON_LEFT))
    {
        draggingFormScroll = false;
        descriptionEdit.mouseSelecting = false;
        examineEdit.mouseSelecting = false;
        ttsDescriptionEdit.mouseSelecting = false;
        ttsExamineEdit.mouseSelecting = false;
        descriptionEdit.draggingScroll = false;
        examineEdit.draggingScroll = false;
        ttsDescriptionEdit.draggingScroll = false;
        ttsExamineEdit.draggingScroll = false;
        if (descriptionEdit.selectAnchor == descriptionEdit.cursor)
            descriptionEdit.selectAnchor = -1;
        if (examineEdit.selectAnchor == examineEdit.cursor)
            examineEdit.selectAnchor = -1;
        if (ttsDescriptionEdit.selectAnchor == ttsDescriptionEdit.cursor)
            ttsDescriptionEdit.selectAnchor = -1;
        if (ttsExamineEdit.selectAnchor == ttsExamineEdit.cursor)
            ttsExamineEdit.selectAnchor = -1;
    }

    // Prefer multiline wheel when pointer is over a multiline with overflow;
    // otherwise scroll the form. Also accept wheel anywhere over the dialog
    // content / form scrollbar so the TTS section is reachable.
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
    {
        auto tryMultiWheel = [&](MultilineState& state, std::string& buffer, int fieldIndex) -> bool {
            if (state.lastField.height <= 1.0f || state.lastMaxScroll <= 0.5f)
                return false;
            if (!CheckCollisionPointRec(mouse, state.lastField))
                return false;
            if (lastContentRect.width > 1.0f
                && !CheckCollisionPointRec(mouse, lastContentRect))
                return false;
            (void)fieldIndex;
            state.scrollY -= wheel * (fontSize + 3.0f) * 2.0f;
            state.scrollY = std::clamp(state.scrollY, 0.0f, state.lastMaxScroll);
            return true;
        };

        bool usedMulti = false;
        if (tryMultiWheel(descriptionEdit, payload.description, 1))
            usedMulti = true;
        else if (tryMultiWheel(examineEdit, payload.examineDetails, 2))
            usedMulti = true;
        else if (tryMultiWheel(ttsDescriptionEdit, payload.ttsDescription, 7))
            usedMulti = true;
        else if (tryMultiWheel(ttsExamineEdit, payload.ttsExamineDetails, 8))
            usedMulti = true;

        const bool overForm = (lastContentRect.width > 1.0f
                               && CheckCollisionPointRec(mouse, lastContentRect))
            || (lastFormScrollTrack.width > 1.0f
                && CheckCollisionPointRec(mouse, lastFormScrollTrack));
        if (!usedMulti && overForm)
        {
            const float maxScroll =
                std::max(0.0f, lastContentHeight - lastContentRect.height);
            scrollY -= wheel * 40.0f;
            scrollY = std::clamp(scrollY, 0.0f, maxScroll);
        }
    }

    if (focusField == 1)
        ensureMultilineCursorVisible(
            font,
            payload.description,
            descriptionEdit,
            descriptionEdit.lastField.width > 1.0f
                ? descriptionEdit.lastField
                : Rectangle{0, 0, 400, 88},
            fontSize);
    else if (focusField == 2)
        ensureMultilineCursorVisible(
            font,
            payload.examineDetails,
            examineEdit,
            examineEdit.lastField.width > 1.0f
                ? examineEdit.lastField
                : Rectangle{0, 0, 400, 64},
            fontSize);
    else if (focusField == 7)
        ensureMultilineCursorVisible(
            font,
            payload.ttsDescription,
            ttsDescriptionEdit,
            ttsDescriptionEdit.lastField.width > 1.0f
                ? ttsDescriptionEdit.lastField
                : Rectangle{0, 0, 400, 88},
            fontSize);
    else if (focusField == 8)
        ensureMultilineCursorVisible(
            font,
            payload.ttsExamineDetails,
            ttsExamineEdit,
            ttsExamineEdit.lastField.width > 1.0f
                ? ttsExamineEdit.lastField
                : Rectangle{0, 0, 400, 64},
            fontSize);

    if (IsKeyPressed(KEY_ESCAPE) && !generateBusy.load())
    {
        if (voiceMenuOpen)
            voiceMenuOpen = false;
        else
            closeDialog();
    }
}

void SceneAuthoringDialog::draw(int screenW, int screenH)
{
    if (!open)
        return;
    pollGenerateResult();
    pollApiKeyValidity();

    const Font font = (uiFont.texture.id != 0 ? uiFont : GetFontDefault());
    const Font bold = (uiFontBold.texture.id != 0 ? uiFontBold : font);
    const Vector2 mouse = GetMousePosition();
    const bool canClick =
        !waitMouseRelease && ignoreInputFrames <= 0 && !generateBusy.load()
        && editorMousePressed(MOUSE_BUTTON_LEFT);
    const bool busy = generateBusy.load();
    const bool keyValid = apiKeyValidity == ApiKeyValidity::Valid;

    DrawRectangle(0, 0, screenW, screenH, kModalOverlay);

    const float dialogW = std::min(920.0f, screenW - 40.0f);
    const float dialogH = std::min(720.0f, screenH - 40.0f);
    const Rectangle dialog = {
        (screenW - dialogW) * 0.5f,
        (screenH - dialogH) * 0.5f,
        dialogW,
        dialogH};
    DrawRectangleRec(dialog, kModalFill);
    DrawRectangleLinesEx(dialog, 2.0f, kPanelBorder);

    DrawTextEx(
        bold,
        editingExisting ? "Edit Scene" : "New Scene",
        {dialog.x + 20.0f, dialog.y + 16.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);
    DrawTextEx(
        font,
        editingExisting
            ? "Update fields, then Confirm. Use Rename to change the scene id."
            : "Create requires a unique id, description, and existing image path.",
        {dialog.x + 20.0f, dialog.y + 46.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float contentTop = dialog.y + 72.0f;
    const float footerH = 58.0f;
    const Rectangle content = {
        dialog.x + 16.0f,
        contentTop,
        dialog.width - 32.0f,
        dialogH - (contentTop - dialog.y) - footerH};
    lastContentRect = content;
    DrawRectangleRec(content, Color{18, 16, 24, 255});
    DrawRectangleLinesEx(content, 1.0f, kPanelInnerEdge);

    // Estimate first so wheel/scrollbar work even before a full layout pass.
    lastContentHeight = std::max(lastContentHeight, estimateFormContentHeight());
    const float maxScroll = std::max(0.0f, lastContentHeight - content.height);
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);

    auto beginContentScissor = [&]() {
        BeginScissorMode(
            static_cast<int>(content.x),
            static_cast<int>(content.y),
            static_cast<int>(content.width),
            static_cast<int>(content.height));
    };
    beginContentScissor();

    float y = content.y + 12.0f - scrollY;
    const float labelX = content.x + 12.0f;
    const float fieldX = content.x + 12.0f;
    const float fieldW = content.width - 24.0f;

    auto hitInContent = [&](Rectangle r) {
        return CheckCollisionPointRec(mouse, r)
            && CheckCollisionPointRec(mouse, content);
    };
    auto hitField = [&](Rectangle r, int index) {
        if (canClick && hitInContent(r))
            focusField = index;
    };

    // drawMultilineField ends its own scissor; restore the form clip afterward.
    auto drawMultilineClipped =
        [&](Rectangle field,
            const std::string& buffer,
            const char* placeholder,
            MultilineState& state,
            bool focused) {
            drawMultilineField(font, field, buffer, placeholder, state, focused);
            beginContentScissor();
        };

    // ID (+ Rename when editing). Confirm does not apply idDraft — only Rename does.
    drawLabel(
        font,
        editingExisting ? "Scene id * (Rename to change)" : "Scene id *",
        labelX,
        y);
    y += 16.0f;
    const float renameW = editingExisting ? 100.0f : 0.0f;
    Rectangle idField = {
        fieldX,
        y,
        editingExisting ? (fieldW - renameW - 10.0f) : (fieldW * 0.55f),
        32.0f};
    drawSingleLineField(
        font,
        idField,
        idDraft,
        "e.g. abandoned_mine_shaft",
        idEdit,
        focusField == 0,
        kFontSmall);
    hitField(idField, 0);
    if (editingExisting)
    {
        Rectangle renameBtn = {idField.x + idField.width + 10.0f, y, renameW, 32.0f};
        const bool renameOk = canEnableRename();
        drawEditorButton(font, renameBtn, "Rename", true, renameOk && !busy);
        if (canClick && renameOk && hitInContent(renameBtn))
            applyRename();
    }
    y += 44.0f;

    // Description
    drawLabel(font, "Description * (AI context)", labelX, y);
    y += 16.0f;
    Rectangle descField = {fieldX, y, fieldW, 110.0f};
    drawMultilineClipped(
        descField,
        payload.description,
        "What the player sees when entering...",
        descriptionEdit,
        focusField == 1);
    hitField(descField, 1);
    y += 122.0f;

    // Examine
    drawLabel(font, "Examine details", labelX, y);
    y += 16.0f;
    Rectangle examField = {fieldX, y, fieldW, 88.0f};
    drawMultilineClipped(
        examField,
        payload.examineDetails,
        "(optional) What the player learns on Examine...",
        examineEdit,
        focusField == 2);
    hitField(examField, 2);
    y += 100.0f;

    // API key
    drawLabel(font, "Session xAI API key (images / voice; not saved to disk)", labelX, y);
    if (apiKeyValidity == ApiKeyValidity::Invalid)
    {
        const Vector2 labelSize = MeasureTextEx(
            font,
            "Session xAI API key (images / voice; not saved to disk)",
            kFontTiny,
            1.0f);
        DrawTextEx(
            bold,
            "(INVALID)",
            {labelX + labelSize.x + 8.0f, y},
            kFontTiny,
            1.0f,
            Color{220, 70, 60, 255});
    }
    y += 16.0f;
    Rectangle keyField = {fieldX, y, fieldW, 32.0f};
    // Mask when unfocused; show live text while editing so caret/navigation match.
    if (focusField == 3)
    {
        drawSingleLineField(
            font,
            keyField,
            sessionApiKey,
            "(optional if XAI_API_KEY is set)",
            keyEdit,
            true,
            kFontSmall);
    }
    else
    {
        const std::string keyShow = sessionApiKey.empty()
            ? std::string{}
            : std::string(std::min<size_t>(sessionApiKey.size(), 28), '*');
        drawSingleLineField(
            font,
            keyField,
            keyShow,
            "(optional if XAI_API_KEY is set)",
            keyEdit,
            false,
            kFontSmall);
    }
    hitField(keyField, 3);
    y += 44.0f;

    // Paths + generate buttons
    struct PathRow
    {
        const char* label;
        std::string* value;
        SingleLineState* edit;
        int focus;
        int genTarget;
        const char* genLabel;
    };
    PathRow rows[] = {
        {"Image path", &payload.imagePath, &imageEdit, 4, 1, "Generate image"},
        {"Ambient path", &payload.ambientPath, &ambientEdit, 5, 2, "Generate ambient"},
        {"Music path", &payload.musicPath, &musicEdit, 6, 3, "Generate music"},
    };

    for (const PathRow& row : rows)
    {
        drawLabel(font, row.label, labelX, y);
        y += 16.0f;
        const float genW = 150.0f;
        Rectangle pathField = {fieldX, y, fieldW - genW - 10.0f, 32.0f};
        Rectangle genBtn = {fieldX + pathField.width + 10.0f, y, genW, 32.0f};
        drawSingleLineField(
            font,
            pathField,
            *row.value,
            "(auto from id)",
            *row.edit,
            focusField == row.focus,
            kFontTiny);
        hitField(pathField, row.focus);
        drawEditorButton(font, genBtn, row.genLabel, true, !busy);
        if (canClick && hitInContent(genBtn))
            startGenerate(row.genTarget);
        if (busy && generateTarget.load() == row.genTarget)
        {
            DrawTextEx(
                font,
                "Working",
                {genBtn.x + genBtn.width + 8.0f, genBtn.y + 8.0f},
                kFontTiny,
                1.0f,
                Color{220, 80, 70, 255});
        }
        y += 44.0f;
    }

    // TTS section (replaces Speak button)
    drawLabel(font, "TTS on/off", labelX, y);
    y += 16.0f;
    // Enlarge hit target so the switch stays easy to click near the clip edge.
    ttsSwitchTrack = {fieldX, y, 64.0f, 26.0f};
    bool ttsToggled = false;
    drawOnOffSwitch(font, ttsSwitchTrack, payload.ttsEnabled, nullptr, false, ttsToggled);
    if (canClick && hitInContent(ttsSwitchTrack))
        ttsToggled = true;
    if (ttsToggled)
    {
        payload.ttsEnabled = !payload.ttsEnabled;
        syncSpeakWithTts();
        if (!payload.ttsEnabled)
            voiceMenuOpen = false;
        else
        {
            // Reveal the new TTS controls immediately.
            const float need = estimateFormContentHeight();
            lastContentHeight = std::max(lastContentHeight, need);
            scrollY = std::max(0.0f, need - content.height);
        }
    }
    y += 38.0f;

    if (payload.ttsEnabled)
    {
        const float indentX = fieldX + 12.0f;
        const float indentW = fieldW - 12.0f;

        drawLabel(font, "Default voice:", indentX, y);
        y += 16.0f;
        voiceBtnRect = {indentX, y, 180.0f, 28.0f};
        const std::string voiceLabel =
            payload.ttsDefaultVoice.empty() ? "leo" : payload.ttsDefaultVoice;
        drawEditorButton(
            font,
            voiceBtnRect,
            voiceLabel.c_str(),
            true,
            !busy);
        if (canClick && hitInContent(voiceBtnRect))
            voiceMenuOpen = !voiceMenuOpen;
        y += 36.0f;

        drawLabel(font, "TTS Description", indentX, y);
        y += 16.0f;
        Rectangle ttsDescField = {indentX, y, indentW, 100.0f};
        drawMultilineClipped(
            ttsDescField,
            payload.ttsDescription,
            "Spoken enter / description markup (TTS tags allowed)...",
            ttsDescriptionEdit,
            focusField == 7);
        hitField(ttsDescField, 7);
        y += 110.0f;

        const float btnW = 170.0f;
        Rectangle genTtsDescBtn = {indentX, y, btnW, 28.0f};
        Rectangle genVoiceDescBtn = {indentX + btnW + 10.0f, y, btnW, 28.0f};
        const bool canVoiceDesc =
            keyValid && sceneTtsTextHasWord(payload.ttsDescription);
        drawEditorButton(font, genTtsDescBtn, "Generate TTS dialog", true, !busy);
        drawEditorButton(
            font, genVoiceDescBtn, "Generate Voice data", true, !busy && canVoiceDesc);
        if (canClick && hitInContent(genTtsDescBtn))
            startGenerate(6);
        if (canClick && canVoiceDesc && hitInContent(genVoiceDescBtn))
            startVoiceRefresh();
        if (busy && (generateTarget.load() == 6 || generateTarget.load() == 10))
        {
            DrawTextEx(
                font,
                "Working",
                {genVoiceDescBtn.x + genVoiceDescBtn.width + 8.0f, genVoiceDescBtn.y + 6.0f},
                kFontTiny,
                1.0f,
                Color{220, 80, 70, 255});
        }
        y += 40.0f;

        drawLabel(font, "TTS Examine Details", indentX, y);
        y += 16.0f;
        Rectangle ttsExamField = {indentX, y, indentW, 88.0f};
        drawMultilineClipped(
            ttsExamField,
            payload.ttsExamineDetails,
            "Spoken examine markup (TTS tags allowed)...",
            ttsExamineEdit,
            focusField == 8);
        hitField(ttsExamField, 8);
        y += 98.0f;

        Rectangle genTtsExamBtn = {indentX, y, btnW, 28.0f};
        Rectangle genVoiceExamBtn = {indentX + btnW + 10.0f, y, btnW, 28.0f};
        const bool canVoiceExam =
            keyValid && sceneTtsTextHasWord(payload.ttsExamineDetails);
        drawEditorButton(font, genTtsExamBtn, "Generate TTS dialog", true, !busy);
        drawEditorButton(
            font, genVoiceExamBtn, "Generate Voice data", true, !busy && canVoiceExam);
        if (canClick && hitInContent(genTtsExamBtn))
            startGenerate(7);
        if (canClick && canVoiceExam && hitInContent(genVoiceExamBtn))
            startVoiceRefresh();
        y += 40.0f;
    }
    else
    {
        voiceBtnRect = {0, 0, 0, 0};
        voiceMenuOpen = false;
    }

    // Prefer the real laid-out bottom so scroll range always covers the TTS switch.
    const float laidOutBottom = y + scrollY - content.y + 16.0f;
    lastContentHeight = std::max(estimateFormContentHeight(), laidOutBottom);

    EndScissorMode();

    // Form scrollbar (outside field widgets).
    const float formMaxScroll = std::max(0.0f, lastContentHeight - content.height);
    scrollY = std::clamp(scrollY, 0.0f, formMaxScroll);
    lastFormScrollTrack = {0, 0, 0, 0};
    lastFormScrollThumb = {0, 0, 0, 0};
    if (formMaxScroll > 0.5f)
    {
        const float trackW = 8.0f;
        const float trackX = content.x + content.width - trackW - 2.0f;
        const float trackY = content.y + 4.0f;
        const float trackH = content.height - 8.0f;
        lastFormScrollTrack = {trackX, trackY, trackW, trackH};
        DrawRectangleRec(lastFormScrollTrack, Color{30, 28, 38, 255});
        const float thumbH =
            std::max(18.0f, trackH * (content.height / std::max(content.height, lastContentHeight)));
        const float t = (formMaxScroll > 0.0f) ? (scrollY / formMaxScroll) : 0.0f;
        const float thumbY = trackY + t * (trackH - thumbH);
        lastFormScrollThumb = {trackX, thumbY, trackW, thumbH};
        DrawRectangleRec(
            lastFormScrollThumb,
            draggingFormScroll ? kPanelBorder : kPanelAccent);
    }

    // Voice menu must not be clipped by the content scissor.
    drawVoiceMenu(font);

    // Footer buttons
    const float btnW = 130.0f;
    const float btnH = 36.0f;
    const float btnY = dialog.y + dialogH - btnH - 14.0f;
    Rectangle createBtn = {
        dialog.x + dialogW - btnW * 2.0f - 36.0f, btnY, btnW, btnH};
    Rectangle cancelBtn = {dialog.x + dialogW - btnW - 18.0f, btnY, btnW, btnH};
    Rectangle genAllBtn = {dialog.x + 20.0f, btnY, 180.0f, btnH};

    drawEditorButton(font, genAllBtn, "Generate all assets", true, !busy);
    const bool primaryEnabled = editingExisting ? !busy : (canEnableCreate() && !busy);
    drawEditorButton(
        font,
        createBtn,
        editingExisting ? "Confirm" : "Create",
        true,
        primaryEnabled);
    drawEditorButton(font, cancelBtn, "Cancel", false, !busy);

    if (canClick)
    {
        if (voiceMenuOpen && CheckCollisionPointRec(mouse, voiceMenuRect))
        {
            // Handled in handleInput via handleVoiceMenuClick; avoid closing dialog.
        }
        else if (CheckCollisionPointRec(mouse, cancelBtn))
            closeDialog();
        else if (primaryEnabled && CheckCollisionPointRec(mouse, createBtn))
            commitSave(false, 0);
        else if (CheckCollisionPointRec(mouse, genAllBtn))
            startGenerate(0);
        else if (!CheckCollisionPointRec(mouse, dialog) && !busy)
            closeDialog();
    }

    // Status / error sit left of the primary buttons so they never cover Generate all.
    if (!busy && (!status.empty() || !error.empty()))
    {
        const std::string& msg = !error.empty() ? error : status;
        const Color col = !error.empty()
            ? Color{220, 100, 90, 255}
            : Color{120, 180, 120, 255};
        const float msgMaxW = std::max(40.0f, createBtn.x - (dialog.x + 210.0f) - 12.0f);
        std::string clipped = msg;
        while (clipped.size() > 4
               && MeasureTextEx(font, clipped.c_str(), kFontTiny, 1.0f).x > msgMaxW)
            clipped.pop_back();
        if (clipped.size() < msg.size() && clipped.size() >= 3)
        {
            clipped.resize(clipped.size() - 3);
            clipped += "...";
        }
        DrawTextEx(
            font,
            clipped.c_str(),
            {dialog.x + 210.0f, btnY + 10.0f},
            kFontTiny,
            1.0f,
            col);
    }

    if (busy)
        drawWorkingOverlay(screenW, screenH, font, bold);
}

void SceneAuthoringDialog::drawWorkingOverlay(int screenW, int screenH, Font font, Font bold)
{
    const auto& res = editorButtons();
    const auto& cfg = res.working;

    DrawRectangle(0, 0, screenW, screenH, Color{0, 0, 0, 180});

    const float panelW = 360.0f;
    const float spin = static_cast<float>(cfg.sizePx);
    const float panelH = 64.0f + spin + 28.0f + 44.0f + 28.0f;
    const Rectangle panel = {
        (static_cast<float>(screenW) - panelW) * 0.5f,
        (static_cast<float>(screenH) - panelH) * 0.5f - 24.0f,
        panelW,
        panelH};
    DrawRectangleRounded(panel, 0.04f, 8, kModalFill);
    DrawRectangleLinesEx(panel, 2.0f, kPanelBorder);

    const std::string title =
        cfg.title.empty() ? "Working - Please wait" : cfg.title;
    const Vector2 titleSize = MeasureTextEx(bold, title.c_str(), kFontHeading, 1.0f);
    DrawTextEx(
        bold,
        title.c_str(),
        {panel.x + (panel.width - titleSize.x) * 0.5f, panel.y + 18.0f},
        kFontHeading,
        1.0f,
        kTextPrimary);

    // Detail line from current status (job progress message).
    std::string detail = status;
    if (detail.empty())
        detail = "Running authoring job…";
    if (detail.size() > 64)
        detail = detail.substr(0, 61) + "...";
    const Vector2 detailSize = MeasureTextEx(font, detail.c_str(), kFontTiny, 1.0f);
    DrawTextEx(
        font,
        detail.c_str(),
        {panel.x + (panel.width - detailSize.x) * 0.5f, panel.y + 48.0f},
        kFontTiny,
        1.0f,
        kTextMuted);

    const float spinX = panel.x + (panel.width - spin) * 0.5f;
    const float spinY = panel.y + 72.0f;
    const float rpm = std::max(0.05f, cfg.revolutionsPerSecond);
    // Clockwise: positive angle in raylib DrawTexturePro is counterclockwise,
    // so negate.
    const float degrees = -std::fmod(static_cast<float>(GetTime()) * rpm * 360.0f, 360.0f);

    if (res.workingSpinnerLoaded && res.workingSpinner.id != 0)
    {
        const Rectangle src = {
            0,
            0,
            static_cast<float>(res.workingSpinner.width),
            static_cast<float>(res.workingSpinner.height)};
        const Rectangle dst = {spinX + spin * 0.5f, spinY + spin * 0.5f, spin, spin};
        DrawTexturePro(
            res.workingSpinner,
            src,
            dst,
            {spin * 0.5f, spin * 0.5f},
            degrees,
            WHITE);
    }
    else
    {
        // Fallback ring if texture missing / rejected.
        const Vector2 c = {spinX + spin * 0.5f, spinY + spin * 0.5f};
        DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y), spin * 0.36f, kTextPrimary);
        const float rad = degrees * (3.14159265f / 180.0f);
        DrawCircle(
            static_cast<int>(c.x + std::cos(rad) * spin * 0.36f),
            static_cast<int>(c.y + std::sin(rad) * spin * 0.36f),
            5.0f,
            kTextPrimary);
    }

    const float btnW = 120.0f;
    const float btnH = 34.0f;
    const Rectangle cancelBtn = {
        panel.x + (panel.width - btnW) * 0.5f,
        spinY + spin + 16.0f,
        btnW,
        btnH};
    drawEditorButton(font, cancelBtn, "Cancel", false, true);
    if (editorMousePressed(MOUSE_BUTTON_LEFT)
        && CheckCollisionPointRec(GetMousePosition(), cancelBtn))
        requestCancelGenerate();
    if (IsKeyPressed(KEY_ESCAPE))
        requestCancelGenerate();
}

} // namespace timberline_editor
