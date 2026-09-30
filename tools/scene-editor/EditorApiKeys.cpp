/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Session-only API key store + async validation (#56).
 ******************************************************************************/

#include "EditorApiKeys.h"
#include "EditorTheme.h"
#include "PlatformPath.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

using timberline_engine::pathJoin;

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

namespace
{

std::string trimAscii(std::string s)
{
    while (!s.empty()
           && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'
               || s.back() == '\r'))
        s.pop_back();
    size_t i = 0;
    while (i < s.size()
           && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
    return s.substr(i);
}

std::string readKeyFile(const std::string& path, const char* envNames[], int envCount)
{
    std::ifstream in(path.c_str());
    if (!in)
        return {};
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string text = trimAscii(buffer.str());
    if (text.empty())
        return {};
    if (text.find('=') != std::string::npos)
    {
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line))
        {
            line = trimAscii(line);
            if (line.empty() || line[0] == '#')
                continue;
            for (int i = 0; i < envCount; ++i)
            {
                const std::string prefix = std::string(envNames[i]) + "=";
                if (line.rfind(prefix, 0) == 0)
                {
                    std::string val = trimAscii(line.substr(prefix.size()));
                    if (!val.empty() && val.front() == '"' && val.back() == '"')
                        val = val.substr(1, val.size() - 2);
                    return val;
                }
            }
        }
        return {};
    }
    return text;
}

} // namespace

void EditorApiKeys::bootstrapFromEnvAndFiles()
{
    if (bootstrapped)
        return;
    bootstrapped = true;

    const char* xaiEnvNames[] = {"XAI_API_KEY", "xAI_API_KEY", "GROK_API_KEY"};
    const char* elEnvNames[] = {
        "ELEVENLABS_API_KEY", "ELEVEN_API_KEY", "XI_API_KEY"};

    std::string xai = xaiKey;
    std::string el = elevenLabsKey;

    if (xai.empty())
    {
        for (const char* name : xaiEnvNames)
        {
            if (const char* env = std::getenv(name);
                env != nullptr && env[0] != '\0')
            {
                xai = env;
                break;
            }
        }
    }
    if (el.empty())
    {
        for (const char* name : elEnvNames)
        {
            if (const char* env = std::getenv(name);
                env != nullptr && env[0] != '\0')
            {
                el = env;
                break;
            }
        }
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0')
    {
        const std::string cfg =
            pathJoin(pathJoin(pathJoin(home, ".config"), "highline-ridge"), "");
        if (xai.empty())
        {
            xai = readKeyFile(
                pathJoin(
                    pathJoin(pathJoin(home, ".config"), "highline-ridge"),
                    "xai_api_key"),
                xaiEnvNames,
                3);
        }
        if (el.empty())
        {
            el = readKeyFile(
                pathJoin(
                    pathJoin(pathJoin(home, ".config"), "highline-ridge"),
                    "elevenlabs_api_key"),
                elEnvNames,
                3);
        }
        (void)cfg;
    }

    if ((!xai.empty() && xai != xaiKey) || (!el.empty() && el != elevenLabsKey))
        applySessionKeys(
            xai.empty() ? xaiKey : xai, el.empty() ? elevenLabsKey : el);
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
        // Prefer /v1/user when the key has user_read. Restricted music-only keys
        // often return 401 missing_permissions instead of invalid_api_key — that
        // still means the key is real and can compose music.
        std::ostringstream safe;
        safe << "RESP=$(curl -sS -w \"\\n%{http_code}\" --max-time 8 "
             << "-H " << shellSingleQuote("xi-api-key: " + key) << " "
             << "https://api.elevenlabs.io/v1/user 2>/dev/null); "
             << "CODE=$(printf '%s' \"$RESP\" | tail -n1); "
             << "BODY=$(printf '%s' \"$RESP\" | sed '$d'); "
             << "if [ \"$CODE\" = \"200\" ]; then echo 1; "
             << "elif [ \"$CODE\" = \"401\" ] && printf '%s' \"$BODY\" | grep -q missing_permissions; "
             << "then echo 1; "
             << "elif [ \"$CODE\" = \"401\" ] && printf '%s' \"$BODY\" | grep -q invalid_api_key; "
             << "then echo 0; "
             << "elif [ \"$CODE\" = \"401\" ]; then echo 1; "
             << "else echo 0; fi";
        const int code = curlHttpCode(safe.str());
        // curlHttpCode reads first line as atoi — our script echoes 0/1.
        elevenLabsCheckResult.store(code == 1 ? 1 : 0);
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
