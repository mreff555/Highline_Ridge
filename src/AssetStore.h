/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Logical asset reads (disk or embedded pak) + platform writable data root.
 ******************************************************************************/

#ifndef ASSET_STORE_H
#define ASSET_STORE_H

#include <cstdint>
#include <string>
#include <vector>

namespace timberline_engine
{

struct AssetBytes
{
    std::vector<unsigned char> data;
    /** Extension of the logical asset, e.g. ".png", ".json", ".mp3". */
    std::string logicalExt;
};

/**
 * Read-only game content. Dev builds use DiskAssetStore; release embeds use
 * PakAssetStore. Writable user data (saves, user_config) is never in the store.
 */
class AssetStore
{
public:
    virtual ~AssetStore() = default;

    /**
     * Load logicalPath (e.g. "resources/scenes.json"). Implementations may
     * transparently try logicalPath + ".xz" and decompress.
     */
    virtual bool readBytes(const std::string& logicalPath, AssetBytes& out) const = 0;

    virtual bool exists(const std::string& logicalPath) const = 0;

    bool readText(const std::string& logicalPath, std::string& out) const;
};

/** Current process-wide store (set during GameApplication init). */
AssetStore& assets();
void setAssets(AssetStore* store);

/**
 * Platform user-data directory (creates if missing):
 *   Linux:   ~/.highline_ridge
 *   macOS:   ~/Library/Application Support/Highline Ridge
 *   Windows: %AppData%/Highline Ridge
 * Override: HIGHLINE_DATA_DIR
 */
std::string userDataRoot();

std::string userSavesDirectory();
std::string userConfigPath();

/** Disk-backed store used for development and the scene editor. */
class DiskAssetStore : public AssetStore
{
public:
    explicit DiskAssetStore(std::string assetRoot = ".");

    bool readBytes(const std::string& logicalPath, AssetBytes& out) const override;
    bool exists(const std::string& logicalPath) const override;

    const std::string& assetRoot() const { return root; }

private:
    std::string root;
};

/**
 * Reads HLAP v1 packs (see tools/pack_assets.py).
 * Construct from an in-memory pak blob (release embed) or a pak file path.
 */
class PakAssetStore : public AssetStore
{
public:
    /** Parse pak bytes already in memory (incbin / linked blob). */
    static bool openMemory(
        const unsigned char* pakBytes,
        size_t pakSize,
        PakAssetStore& out,
        std::string& error);

    /** Load and parse a .pak file from disk. */
    static bool openFile(const std::string& pakPath, PakAssetStore& out, std::string& error);

    bool readBytes(const std::string& logicalPath, AssetBytes& out) const override;
    bool exists(const std::string& logicalPath) const override;

    size_t entryCount() const { return entries.size(); }

private:
    struct Entry
    {
        std::string logicalPath;
        uint32_t offset = 0;
        uint32_t size = 0;
        uint32_t flags = 0;
    };

    bool parse(const unsigned char* pakBytes, size_t pakSize, std::string& error);
    const Entry* find(const std::string& logicalPath) const;

    std::vector<unsigned char> ownedPak; // used when loaded from file
    const unsigned char* pakData = nullptr;
    size_t pakSize = 0;
    size_t blobOffset = 0; // absolute offset of blob within pakData
    std::vector<Entry> entries;
};

} // namespace timberline_engine

#endif /* ASSET_STORE_H */
