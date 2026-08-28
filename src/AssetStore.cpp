/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 ******************************************************************************/

#include "AssetStore.h"
#include "ImageCompression.h"
#include "PlatformPath.h"

#include <raylib.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace timberline_engine
{

namespace
{

AssetStore* gAssets = nullptr;
DiskAssetStore gDefaultDiskStore(".");

std::string homeDirectory()
{
#if defined(_WIN32)
    if (const char* profile = std::getenv("USERPROFILE"))
    {
        if (profile[0] != '\0')
            return profile;
    }
    if (const char* homeDrive = std::getenv("HOMEDRIVE"))
    {
        if (const char* homePath = std::getenv("HOMEPATH"))
            return std::string(homeDrive) + homePath;
    }
    return ".";
#else
    if (const char* home = std::getenv("HOME"))
    {
        if (home[0] != '\0')
            return home;
    }
    return ".";
#endif
}

std::string fileExtension(const std::string& path)
{
    std::string p = path;
    if (p.size() > 3 && p.compare(p.size() - 3, 3, ".xz") == 0)
        p = p.substr(0, p.size() - 3);
    const size_t dot = p.find_last_of('.');
    if (dot == std::string::npos)
        return "";
    return p.substr(dot);
}

} // namespace

AssetStore& assets()
{
    if (gAssets != nullptr)
        return *gAssets;
    return gDefaultDiskStore;
}

void setAssets(AssetStore* store)
{
    gAssets = store;
}

std::string userDataRoot()
{
    if (const char* overrideDir = std::getenv("HIGHLINE_DATA_DIR"))
    {
        if (overrideDir[0] != '\0')
        {
            ensureDirectory(overrideDir);
            return overrideDir;
        }
    }

    const std::string home = homeDirectory();
#if defined(_WIN32)
    std::string root;
    if (const char* appData = std::getenv("APPDATA"))
        root = pathJoin(appData, "Highline Ridge");
    else
        root = pathJoin(home, "Highline Ridge");
#elif defined(__APPLE__)
    const std::string root =
        pathJoin(pathJoin(pathJoin(home, "Library"), "Application Support"), "Highline Ridge");
#else
    const std::string root = pathJoin(home, ".highline_ridge");
#endif
    ensureDirectory(root);
    return root;
}

std::string userSavesDirectory()
{
    const std::string dir = pathJoin(userDataRoot(), "saves");
    ensureDirectory(dir);
    return dir;
}

std::string userConfigPath()
{
    return pathJoin(userDataRoot(), "user_config.json");
}

bool AssetStore::readText(const std::string& logicalPath, std::string& out) const
{
    AssetBytes bytes;
    if (!readBytes(logicalPath, bytes))
        return false;
    out.assign(
        reinterpret_cast<const char*>(bytes.data.data()),
        bytes.data.size());
    return true;
}

DiskAssetStore::DiskAssetStore(std::string assetRoot)
    : root(std::move(assetRoot))
{
}

bool DiskAssetStore::exists(const std::string& logicalPath) const
{
    const std::vector<std::string> paths = buildAssetSearchPaths(root, logicalPath);
    for (const std::string& path : paths)
    {
        if (FileExists(path.c_str()))
            return true;
        if (FileExists(compressedAssetPath(path).c_str()))
            return true;
    }
    return false;
}

bool DiskAssetStore::readBytes(const std::string& logicalPath, AssetBytes& out) const
{
    out = AssetBytes{};
    const std::vector<std::string> paths = buildAssetSearchPaths(root, logicalPath);
    for (const std::string& path : paths)
    {
        const std::string compressed = compressedAssetPath(path);
        if (FileExists(compressed.c_str())
            && loadAssetBytesFromFile(compressed, out.data))
        {
            out.logicalExt = fileExtension(logicalPath);
            return true;
        }
        if (FileExists(path.c_str()) && loadAssetBytesFromFile(path, out.data))
        {
            out.logicalExt = fileExtension(logicalPath);
            return true;
        }
    }
    return false;
}

namespace
{

uint32_t readU32LE(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

bool PakAssetStore::parse(const unsigned char* bytes, size_t size, std::string& error)
{
    entries.clear();
    pakData = nullptr;
    pakSize = 0;
    blobOffset = 0;

    if (bytes == nullptr || size < 16)
    {
        error = "pak too small";
        return false;
    }
    if (std::memcmp(bytes, "HLAP", 4) != 0)
    {
        error = "bad pak magic";
        return false;
    }
    const uint32_t version = readU32LE(bytes + 4);
    const uint32_t entryCount = readU32LE(bytes + 8);
    const uint32_t blobSize = readU32LE(bytes + 12);
    if (version != 1)
    {
        error = "unsupported pak version";
        return false;
    }

    const size_t indexBytes = static_cast<size_t>(entryCount) * 16u;
    const size_t indexEnd = 16u + indexBytes;
    if (size < indexEnd)
    {
        error = "pak index truncated";
        return false;
    }

    // String table follows index; find its end by scanning path offsets' max.
    uint32_t maxPathEnd = 0;
    for (uint32_t i = 0; i < entryCount; ++i)
    {
        const unsigned char* e = bytes + 16u + static_cast<size_t>(i) * 16u;
        const uint32_t pathOff = readU32LE(e + 0);
        // Conservative: paths are within string table; we'll resolve after we
        // know string table bounds. First pass: collect offsets.
        if (pathOff > maxPathEnd)
            maxPathEnd = pathOff;
    }

    // String table starts at indexEnd. Each path is NUL-terminated; estimate
    // end by walking all path strings.
    size_t stringTableStart = indexEnd;
    size_t stringTableEnd = stringTableStart;
    std::vector<Entry> parsed;
    parsed.reserve(entryCount);
    for (uint32_t i = 0; i < entryCount; ++i)
    {
        const unsigned char* e = bytes + 16u + static_cast<size_t>(i) * 16u;
        const uint32_t pathOff = readU32LE(e + 0);
        const uint32_t dataOff = readU32LE(e + 4);
        const uint32_t dataSize = readU32LE(e + 8);
        const uint32_t flags = readU32LE(e + 12);
        const size_t pathAbs = stringTableStart + pathOff;
        if (pathAbs >= size)
        {
            error = "pak path offset out of range";
            return false;
        }
        size_t pathEnd = pathAbs;
        while (pathEnd < size && bytes[pathEnd] != 0)
            ++pathEnd;
        if (pathEnd >= size)
        {
            error = "pak path not NUL-terminated";
            return false;
        }
        if (pathEnd + 1 > stringTableEnd)
            stringTableEnd = pathEnd + 1;

        Entry entry;
        entry.logicalPath.assign(
            reinterpret_cast<const char*>(bytes + pathAbs),
            pathEnd - pathAbs);
        entry.offset = dataOff;
        entry.size = dataSize;
        entry.flags = flags;
        parsed.push_back(std::move(entry));
    }

    // Blob is 16-byte aligned after string table (see pack_assets.py).
    size_t blobStart = stringTableEnd;
    blobStart = (blobStart + 15u) & ~static_cast<size_t>(15u);
    if (blobStart + static_cast<size_t>(blobSize) > size)
    {
        error = "pak blob truncated";
        return false;
    }

    for (Entry& entry : parsed)
    {
        if (static_cast<size_t>(entry.offset) + static_cast<size_t>(entry.size) > blobSize)
        {
            error = "pak entry exceeds blob";
            return false;
        }
    }

    entries = std::move(parsed);
    pakData = bytes;
    pakSize = size;
    blobOffset = blobStart;
    (void)maxPathEnd;
    return true;
}

bool PakAssetStore::openMemory(
    const unsigned char* pakBytes,
    size_t pakSizeBytes,
    PakAssetStore& out,
    std::string& error)
{
    out = PakAssetStore{};
    if (!out.parse(pakBytes, pakSizeBytes, error))
        return false;
    // Non-owning view.
    out.ownedPak.clear();
    return true;
}

bool PakAssetStore::openFile(const std::string& pakPath, PakAssetStore& out, std::string& error)
{
    out = PakAssetStore{};
    std::ifstream in(pakPath.c_str(), std::ios::binary);
    if (!in)
    {
        error = "failed to open pak: " + pakPath;
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff len = in.tellg();
    if (len <= 0)
    {
        error = "empty pak";
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.ownedPak.resize(static_cast<size_t>(len));
    in.read(
        reinterpret_cast<char*>(out.ownedPak.data()),
        static_cast<std::streamsize>(out.ownedPak.size()));
    if (!in)
    {
        error = "failed to read pak";
        return false;
    }
    if (!out.parse(out.ownedPak.data(), out.ownedPak.size(), error))
    {
        out.ownedPak.clear();
        return false;
    }
    return true;
}

const PakAssetStore::Entry* PakAssetStore::find(const std::string& logicalPath) const
{
    for (const Entry& entry : entries)
    {
        if (entry.logicalPath == logicalPath)
            return &entry;
    }
    return nullptr;
}

bool PakAssetStore::exists(const std::string& logicalPath) const
{
    return find(logicalPath) != nullptr;
}

bool PakAssetStore::readBytes(const std::string& logicalPath, AssetBytes& out) const
{
    out = AssetBytes{};
    if (pakData == nullptr)
        return false;
    const Entry* entry = find(logicalPath);
    if (entry == nullptr)
        return false;

    const unsigned char* src =
        pakData + blobOffset + static_cast<size_t>(entry->offset);
    const size_t srcSize = static_cast<size_t>(entry->size);
    out.logicalExt = fileExtension(logicalPath);

    if ((entry->flags & 1u) != 0u)
        return decompressXzBytes(src, srcSize, out.data);

    out.data.assign(src, src + srcSize);
    return true;
}

} // namespace timberline_engine
