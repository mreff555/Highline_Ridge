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
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

using timberline_engine::pathJoin;
namespace fs = std::filesystem;

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

/** Run a shell command; return first line as int (atoi), or 0 on failure. */
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

std::string readFileBytes(const std::string& path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string makeTempPath(const char* prefix)
{
#if !defined(_WIN32)
    char tmpl[256];
    std::snprintf(tmpl, sizeof(tmpl), "/tmp/%sXXXXXX", prefix);
    const int fd = mkstemp(tmpl);
    if (fd >= 0)
    {
        close(fd);
        return std::string(tmpl);
    }
#endif
    // Fallback unique-ish path.
    std::ostringstream ss;
    ss << "/tmp/" << prefix << "_" << std::this_thread::get_id() << "_"
       << std::chrono::steady_clock::now().time_since_epoch().count();
    return ss.str();
}

/**
 * ElevenLabs: music-only keys often 401 with missing_permissions on /v1/user.
 * Treat 200 as valid; 401 invalid_api_key as invalid; any other 401 as valid
 * (restricted but live). Network/curl failure → -1 (caller may retry).
 */
int classifyElevenLabsUserResponse(int httpCode, const std::string& body)
{
    if (httpCode == 200)
        return 1;
    if (httpCode == 401)
    {
        if (body.find("invalid_api_key") != std::string::npos)
            return 0;
        // missing_permissions / unauthorized / etc. — key is real.
        return 1;
    }
    if (httpCode <= 0)
        return -1; // curl/network failure — retry
    return 0;
}

} // namespace

const char* apiKeyRequiredHint(ApiKeyProvider provider)
{
    if (provider == ApiKeyProvider::ElevenLabs)
        return "ElevenLabs key required — Music + Sound Effects scopes";
    return "XAI key required — go to console.x.ai";
}

std::string aiPathFieldHint(
    ApiKeyProvider provider,
    const EditorApiKeys* keys,
    const char* fallbackWhenKeyOk)
{
    if (keys == nullptr || keys->key(provider).empty())
        return apiKeyRequiredHint(provider);
    const ApiKeyValidity v = keys->validity(provider);
    if (v == ApiKeyValidity::Invalid)
        return apiKeyRequiredHint(provider);
    if (v == ApiKeyValidity::Unknown)
        return "Checking API key…";
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

    // ASCII only — CourierPrime (and many UI fonts) lack ✓ / … codepoints,
    // which raylib then draws as '?' .
    const char* glyph = "-";
    Color color = kTextMuted;
    if (validity == ApiKeyValidity::Valid)
    {
        glyph = "OK";
        color = Color{80, 200, 110, 255};
    }
    else if (validity == ApiKeyValidity::Unknown)
    {
        glyph = "..";
        color = Color{220, 180, 80, 255};
    }
    else if (
        validity == ApiKeyValidity::Invalid || validity == ApiKeyValidity::Missing)
    {
        glyph = "X";
        color = Color{220, 70, 70, 255};
    }

    // "OK" / ".." need a slightly smaller size to fit the 22px icon square.
    const float fontSize =
        (glyph[0] != '\0' && glyph[1] != '\0') ? kFontTiny : kFontBody;
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

std::string EditorApiKeys::xaiKeyFilePath()
{
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0')
        return {};
    return pathJoin(
        pathJoin(pathJoin(home, ".config"), "highline-ridge"), "xai_api_key");
}

std::string EditorApiKeys::elevenLabsKeyFilePath()
{
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0')
        return {};
    return pathJoin(
        pathJoin(pathJoin(home, ".config"), "highline-ridge"),
        "elevenlabs_api_key");
}

namespace
{

bool writeKeyFileAtomic(const std::string& path, const std::string& value)
{
    if (path.empty())
        return false;
    try
    {
        fs::create_directories(fs::path(path).parent_path());
    }
    catch (...)
    {
        return false;
    }

    if (value.empty())
    {
        std::error_code ec;
        fs::remove(path, ec);
        return true;
    }

    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << value;
        if (!value.empty() && value.back() != '\n')
            out << '\n';
        if (!out.good())
            return false;
    }
#if !defined(_WIN32)
    ::chmod(tmp.c_str(), 0600);
#endif
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec)
    {
        // Fallback copy+remove if rename across volumes fails.
        try
        {
            fs::copy_file(tmp, path, fs::copy_options::overwrite_existing);
            fs::remove(tmp);
#if !defined(_WIN32)
            ::chmod(path.c_str(), 0600);
#endif
            return true;
        }
        catch (...)
        {
            fs::remove(tmp, ec);
            return false;
        }
    }
#if !defined(_WIN32)
    ::chmod(path.c_str(), 0600);
#endif
    return true;
}

} // namespace

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

    // Persist so the next launch (and CLI runners) pick up Confirm'd keys.
    (void)writeKeyFileAtomic(xaiKeyFilePath(), xaiKey);
    (void)writeKeyFileAtomic(elevenLabsKeyFilePath(), elevenLabsKey);
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
        safe << "/usr/bin/curl -sS -o /dev/null -w \"%{http_code}\" --max-time 8 "
             << "-H " << shellSingleQuote("Authorization: Bearer " + key) << " "
             << "https://api.x.ai/v1/models 2>/dev/null";
        const int code = curlHttpCode(safe.str());
        // 200 valid; 0/empty → network fail (-1 retry); else invalid
        if (code == 200)
            xaiCheckResult.store(1);
        else if (code <= 0)
            xaiCheckResult.store(-2); // soft fail / retry
        else
            xaiCheckResult.store(0);
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
        // Prefer /usr/bin/curl + body file so we parse JSON in C++ (no fragile
        // multi-line shell). Music-only keys 401 with missing_permissions.
        const std::string bodyPath = makeTempPath("tl_el_key");
        std::ostringstream safe;
        safe << "/usr/bin/curl -sS -o " << shellSingleQuote(bodyPath)
             << " -w \"%{http_code}\" --max-time 8 "
             << "-H " << shellSingleQuote("xi-api-key: " + key) << " "
             << "https://api.elevenlabs.io/v1/user 2>/dev/null";
        const int http = curlHttpCode(safe.str());
        const std::string body = readFileBytes(bodyPath);
#if !defined(_WIN32)
        ::unlink(bodyPath.c_str());
#endif
        const int classified = classifyElevenLabsUserResponse(http, body);
        if (classified < 0)
            elevenLabsCheckResult.store(-2); // retry
        else
            elevenLabsCheckResult.store(classified);
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
            auto scheduleFn,
            double retrySeconds)
    {
        if (key.empty())
        {
            validity = ApiKeyValidity::Missing;
            validatedFp.clear();
            if (thread.joinable())
            {
                const int pending = checkResult.load();
                if (pending == 0 || pending == 1 || pending == -2)
                    thread.join();
            }
            return;
        }

        if (thread.joinable())
        {
            const int pending = checkResult.load();
            // 1=valid, 0=invalid, -2=soft/network fail (retry without marking Invalid)
            if (pending == 0 || pending == 1 || pending == -2)
            {
                thread.join();
                std::string checked;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    checked = checkFp;
                }
                if (checked == key)
                {
                    if (pending == 1)
                    {
                        validity = ApiKeyValidity::Valid;
                        validatedFp = key;
                    }
                    else if (pending == 0)
                    {
                        validity = ApiKeyValidity::Invalid;
                        validatedFp = key;
                        // Allow a later retry even after confirmed invalid.
                        nextCheck = now + retrySeconds;
                    }
                    else // -2 network
                    {
                        validity = ApiKeyValidity::Unknown;
                        validatedFp.clear();
                        nextCheck = now + retrySeconds;
                    }
                }
                checkResult.store(-1);
            }
        }

        if (validatedFp != key && validity != ApiKeyValidity::Invalid)
            validity = ApiKeyValidity::Unknown;

        // Re-check when fingerprint drifted, or periodically retry Invalid.
        const bool needsCheck = validatedFp != key;
        const bool retryInvalid =
            validity == ApiKeyValidity::Invalid && validatedFp == key
            && now >= nextCheck;
        if (now >= nextCheck || needsCheck)
        {
            if (retryInvalid)
            {
                validatedFp.clear();
                validity = ApiKeyValidity::Unknown;
            }
            if ((validatedFp != key) && !thread.joinable())
            {
                nextCheck = now + 1.0;
                scheduleFn(key);
            }
            else if (!needsCheck && !retryInvalid && now >= nextCheck)
            {
                nextCheck = now + 1.0;
            }
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
        [this](const std::string& k) { scheduleXaiCheck(k); },
        5.0);
    pollOne(
        elevenLabsKey,
        elevenLabsValidity,
        elevenLabsValidatedFingerprint,
        elevenLabsNextCheckTime,
        elevenLabsCheckResult,
        elevenLabsCheckFingerprint,
        elevenLabsThread,
        [this](const std::string& k) { scheduleElevenLabsCheck(k); },
        5.0);
}

void EditorApiKeys::shutdown()
{
    // Best-effort join so threads do not outlive the app.
    if (xaiThread.joinable())
    {
        for (int i = 0; i < 50 && xaiCheckResult.load() == -1; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (xaiCheckResult.load() != -1)
            xaiThread.join();
        else
            xaiThread.detach();
    }
    if (elevenLabsThread.joinable())
    {
        for (int i = 0; i < 50 && elevenLabsCheckResult.load() == -1; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (elevenLabsCheckResult.load() != -1)
            elevenLabsThread.join();
        else
            elevenLabsThread.detach();
    }
}

} // namespace timberline_editor
