/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Session-only API keys for the scene editor (Options → Configure API keys).
 * Never written to disk from the editor UI (#56).
 ******************************************************************************/

#ifndef TIMBERLINE_EDITOR_API_KEYS_H
#define TIMBERLINE_EDITOR_API_KEYS_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <raylib.h>

namespace timberline_editor
{

enum class ApiKeyValidity
{
    Missing,
    Unknown,
    Valid,
    Invalid
};

enum class ApiKeyProvider
{
    Xai,
    ElevenLabs
};

/**
 * In-memory keys shared across Edit Scene / Item AI / Assist / Exit Requirements.
 * Validated asynchronously (xAI /models, ElevenLabs /v1/user).
 */
struct EditorApiKeys
{
    std::string xaiKey;
    std::string elevenLabsKey;

    ApiKeyValidity xaiValidity = ApiKeyValidity::Missing;
    ApiKeyValidity elevenLabsValidity = ApiKeyValidity::Missing;

    std::string xaiValidatedFingerprint;
    std::string elevenLabsValidatedFingerprint;

    double xaiNextCheckTime = 0.0;
    double elevenLabsNextCheckTime = 0.0;

    std::atomic<int> xaiCheckResult{-1}; // -1 idle, 0 invalid, 1 valid
    std::atomic<int> elevenLabsCheckResult{-1};
    std::string xaiCheckFingerprint;
    std::string elevenLabsCheckFingerprint;
    std::mutex mutex;
    std::thread xaiThread;
    std::thread elevenLabsThread;

    bool xaiValid() const { return xaiValidity == ApiKeyValidity::Valid; }
    bool elevenLabsValid() const
    {
        return elevenLabsValidity == ApiKeyValidity::Valid;
    }

    /**
     * Generate gating: allow when Valid, or while still checking (Unknown) if a
     * key is present. Block only on Missing / confirmed Invalid.
     * Music-only ElevenLabs keys often 401 on /v1/user; validation treats that
     * as Valid, but Unknown must not look like a hard failure in the UI.
     */
    bool xaiReady() const
    {
        return !xaiKey.empty() && xaiValidity != ApiKeyValidity::Invalid
            && xaiValidity != ApiKeyValidity::Missing;
    }
    bool elevenLabsReady() const
    {
        return !elevenLabsKey.empty()
            && elevenLabsValidity != ApiKeyValidity::Invalid
            && elevenLabsValidity != ApiKeyValidity::Missing;
    }
    bool ready(ApiKeyProvider provider) const
    {
        return provider == ApiKeyProvider::ElevenLabs ? elevenLabsReady()
                                                      : xaiReady();
    }

    ApiKeyValidity validity(ApiKeyProvider provider) const
    {
        return provider == ApiKeyProvider::ElevenLabs ? elevenLabsValidity
                                                      : xaiValidity;
    }

    const std::string& key(ApiKeyProvider provider) const
    {
        return provider == ApiKeyProvider::ElevenLabs ? elevenLabsKey : xaiKey;
    }

    /**
     * Apply confirmed drafts from Configure: update session memory and
     * overwrite ~/.config/highline-ridge/{xai_api_key,elevenlabs_api_key}
     * (empty draft removes that file).
     */
    void applySessionKeys(const std::string& xai, const std::string& elevenLabs);

    /**
     * If a session slot is empty, load from env or
     * ~/.config/highline-ridge/ xai_api_key / elevenlabs_api_key into memory.
     * Call once at editor startup so disk keys enable Generate without re-pasting.
     */
    void bootstrapFromEnvAndFiles();

    /** Absolute paths for the on-disk key files (may be empty if HOME unset). */
    static std::string xaiKeyFilePath();
    static std::string elevenLabsKeyFilePath();

    /** Call once per frame while the editor runs. */
    void poll();

    void shutdown();

private:
    void scheduleXaiCheck(const std::string& key);
    void scheduleElevenLabsCheck(const std::string& key);
    bool bootstrapped = false;
};

/** Faded empty-field hints when a provider key is missing / invalid. */
const char* apiKeyRequiredHint(ApiKeyProvider provider);

/** Empty-path hint: key warning when invalid, otherwise fallback. */
std::string aiPathFieldHint(
    ApiKeyProvider provider,
    const EditorApiKeys* keys,
    const char* fallbackWhenKeyOk);

/** Draw a small square status glyph left of an AI field (red X / green check). */
void drawApiKeyStatusIcon(
    Font boldFont,
    Rectangle iconRect,
    ApiKeyValidity validity);

/** Layout helper: icon square + gap before the field. */
constexpr float kApiKeyIconSize = 22.0f;
constexpr float kApiKeyIconGap = 8.0f;

Rectangle aiFieldWithKeyIcon(Rectangle fullFieldRow, Rectangle* outIcon);

} // namespace timberline_editor

#endif
