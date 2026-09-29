/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Session-only API key store + async validation (#56).
 ******************************************************************************/

#include "EditorApiKeys.h"
#include "EditorTheme.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <thread>

namespace timberline_editor
{

namespace
{

std::string shellSingleQuote(const std::string& value)
{
    std::string out = "'";
    for (char c : value)
    {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

int curlHttpCode(const std::string& cmd)
{
#if !defined(_WIN32)
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr)
        return 0;
    char buf[32] = {};
    const char* got = fgets(buf, sizeof(buf), pipe);
    (void)pclose(pipe);
    if (got == nullptr)
        return 0;
    return std::atoi(buf);
#else
    (void)cmd;
    return 0;
#endif
}

} // namespace

const char* apiKeyRequiredHint(ApiKeyProvider provider)
{
    if (provider == ApiKeyProvider::ElevenLabs)
        return "ElevenLabs key required — go to elevenlabs.io/app/settings/api-keys";
    return "XAI key required — go to console.x.ai";
}

std::string aiPathFieldHint(
    ApiKeyProvider provider,
    const EditorApiKeys* keys,
    const char* fallbackWhenKeyOk)
{
    // Missing / unknown / invalid → ask for Options keys.
    if (keys == nullptr || keys->validity(provider) != ApiKeyValidity::Valid)
        return apiKeyRequiredHint(provider);
    return fallbackWhenKeyOk != nullptr ? fallbackWhenKeyOk : "";
}

Rectangle aiFieldWithKeyIcon(Rectangle fullFieldRow, Rectangle* outIcon)
{
    Rectangle icon = {
        fullFieldRow.x,
        fullFieldRow.y + (fullFieldRow.height - kApiKeyIconSize) * 0.5f,
        kApiKeyIconSize,
        kApiKeyIconSize};
    if (outIcon != nullptr)
        *outIcon = icon;
    return {
        fullFieldRow.x + kApiKeyIconSize + kApiKeyIconGap,
        fullFieldRow.y,
        std::max(40.0f, fullFieldRow.width - kApiKeyIconSize - kApiKeyIconGap),
        fullFieldRow.height};
}

void drawApiKeyStatusIcon(Font boldFont, Rectangle iconRect, ApiKeyValidity validity)
{
    const Color border = kPanelInnerEdge;
    DrawRectangleRec(iconRect, Color{22, 20, 28, 255});
    DrawRectangleLinesEx(iconRect, 1.0f, border);

    const char* glyph = "–";
    Color color = kTextMuted;
    if (validity == ApiKeyValidity::Valid)
    {
        glyph = "✓";
        color = Color{80, 200, 110, 255};
    }
    else if (
        validity == ApiKeyValidity::Invalid || validity == ApiKeyValidity::Missing
        || validity == ApiKeyValidity::Unknown)
    {
        glyph = "X";
        color = Color{220, 70, 70, 255};
        if (validity == ApiKeyValidity::Unknown)
            color = Color{220, 160, 70, 255}; // checking
    }

    const float fontSize = kFontBody;
    const Vector2 size = MeasureTextEx(boldFont, glyph, fontSize, 1.0f);
    DrawTextEx(
        boldFont,
        glyph,
        {iconRect.x + (iconRect.width - size.x) * 0.5f,
         iconRect.y + (iconRect.height - size.y) * 0.5f},
        fontSize,
        1.0f,
        color);
}

void EditorApiKeys::applySessionKeys(
    const std::string& xai, const std::string& elevenLabs)
{
    xaiKey = xai;
    elevenLabsKey = elevenLabs;
    xaiValidatedFingerprint.clear();
    elevenLabsValidatedFingerprint.clear();
    xaiValidity = xaiKey.empty() ? ApiKeyValidity::Missing : ApiKeyValidity::Unknown;
    elevenLabsValidity =
        elevenLabsKey.empty() ? ApiKeyValidity::Missing : ApiKeyValidity::Unknown;
    xaiNextCheckTime = 0.0;
    elevenLabsNextCheckTime = 0.0;
}

void EditorApiKeys::scheduleXaiCheck(const std::string& key)
{
    if (key.empty() || xaiThread.joinable())
        return;
    xaiCheckResult.store(-1);
    {
        std::lock_guard<std::mutex> lock(mutex);
        xaiCheckFingerprint = key;
    }
    xaiThread = std::thread([this, key]() {
        std::ostringstream safe;
        safe << "curl -sS -o /dev/null -w \"%{http_code}\" --max-time 8 "
             << "-H " << shellSingleQuote("Authorization: Bearer " + key) << " "
             << "https://api.x.ai/v1/models 2>/dev/null";
        const int code = curlHttpCode(safe.str());
        xaiCheckResult.store(code == 200 ? 1 : 0);
    });
}

void EditorApiKeys::scheduleElevenLabsCheck(const std::string& key)
{
    if (key.empty() || elevenLabsThread.joinable())
        return;
    elevenLabsCheckResult.store(-1);
    {
        std::lock_guard<std::mutex> lock(mutex);
        elevenLabsCheckFingerprint = key;
    }
    elevenLabsThread = std::thread([this, key]() {
        std::ostringstream safe;
        safe << "curl -sS -o /dev/null -w \"%{http_code}\" --max-time 8 "
             << "-H " << shellSingleQuote("xi-api-key: " + key) << " "
             << "https://api.elevenlabs.io/v1/user 2>/dev/null";
        const int code = curlHttpCode(safe.str());
        elevenLabsCheckResult.store(code == 200 ? 1 : 0);
    });
}

void EditorApiKeys::poll()
{
    const double now = GetTime();

    auto pollOne =
        [&](std::string& key,
            ApiKeyValidity& validity,
            std::string& validatedFp,
            double& nextCheck,
            std::atomic<int>& checkResult,
            std::string& checkFp,
            std::thread& thread,
            auto scheduleFn)
    {
        if (key.empty())
        {
            validity = ApiKeyValidity::Missing;
            validatedFp.clear();
            if (thread.joinable())
            {
                const int pending = checkResult.load();
                if (pending == 0 || pending == 1)
                    thread.join();
            }
            return;
        }

        if (thread.joinable())
        {
            const int pending = checkResult.load();
            if (pending == 0 || pending == 1)
            {
                thread.join();
                std::string checked;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    checked = checkFp;
                }
                if (checked == key)
                {
                    validity = (pending == 1) ? ApiKeyValidity::Valid
                                              : ApiKeyValidity::Invalid;
                    validatedFp = key;
                }
                checkResult.store(-1);
            }
        }

        if (validatedFp != key)
            validity = ApiKeyValidity::Unknown;

        if (now >= nextCheck)
        {
            nextCheck = now + 1.0;
            if (validatedFp != key && !thread.joinable())
                scheduleFn(key);
        }
    };

    pollOne(
        xaiKey,
        xaiValidity,
        xaiValidatedFingerprint,
        xaiNextCheckTime,
        xaiCheckResult,
        xaiCheckFingerprint,
        xaiThread,
        [this](const std::string& k) { scheduleXaiCheck(k); });
    pollOne(
        elevenLabsKey,
        elevenLabsValidity,
        elevenLabsValidatedFingerprint,
        elevenLabsNextCheckTime,
        elevenLabsCheckResult,
        elevenLabsCheckFingerprint,
        elevenLabsThread,
        [this](const std::string& k) { scheduleElevenLabsCheck(k); });
}

void EditorApiKeys::shutdown()
{
    // Best-effort join so threads do not outlive the app.
    if (xaiThread.joinable())
    {
        // Wait briefly for an in-flight check.
        for (int i = 0; i < 50 && xaiCheckResult.load() < 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (xaiCheckResult.load() >= 0)
            xaiThread.join();
        else
            xaiThread.detach();
    }
    if (elevenLabsThread.joinable())
    {
        for (int i = 0; i < 50 && elevenLabsCheckResult.load() < 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (elevenLabsCheckResult.load() >= 0)
            elevenLabsThread.join();
        else
            elevenLabsThread.detach();
    }
}

} // namespace timberline_editor
