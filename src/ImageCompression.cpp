#include "ImageCompression.h"
#include "PlatformPath.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <jpeglib.h>
#include <lzma.h>

namespace testgame
{

namespace
{

bool readBinaryFile(const std::string& path, std::vector<unsigned char>& outBytes)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open())
        return false;

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0)
        return false;

    file.seekg(0, std::ios::beg);
    outBytes.resize(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(outBytes.data()), size))
        return false;

    return true;
}

std::string fileExtensionFromPath(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "";

    return path.substr(dot);
}

std::string stripXzSuffix(const std::string& path)
{
    static const char kSuffix[] = ".xz";
    if (path.size() >= sizeof(kSuffix) - 1 &&
        path.compare(path.size() - (sizeof(kSuffix) - 1), sizeof(kSuffix) - 1, kSuffix) == 0)
    {
        return path.substr(0, path.size() - (sizeof(kSuffix) - 1));
    }

    return path;
}

bool decompressXzBytes(
    const unsigned char* input,
    size_t inputSize,
    std::vector<unsigned char>& outBytes)
{
    lzma_stream stream = LZMA_STREAM_INIT;
    const lzma_ret initResult = lzma_stream_decoder(
        &stream,
        UINT64_MAX,
        LZMA_CONCATENATED);
    if (initResult != LZMA_OK)
        return false;

    stream.next_in = input;
    stream.avail_in = inputSize;

    outBytes.clear();
    unsigned char buffer[16 * 1024];
    lzma_ret codeResult = LZMA_OK;

    do
    {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        codeResult = lzma_code(&stream, LZMA_FINISH);

        const size_t produced = sizeof(buffer) - stream.avail_out;
        if (produced > 0)
            outBytes.insert(outBytes.end(), buffer, buffer + produced);
    }
    while (codeResult == LZMA_OK);

    lzma_end(&stream);
    return codeResult == LZMA_STREAM_END;
}

bool isJpegExtension(const std::string& extension)
{
    std::string lowered = extension;
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    return lowered == ".jpg" || lowered == ".jpeg";
}

bool loadJpegFromBytes(const std::vector<unsigned char>& bytes, Image& outImage)
{
    if (bytes.empty())
        return false;

    jpeg_decompress_struct info{};
    jpeg_error_mgr errorManager{};
    info.err = jpeg_std_error(&errorManager);
    jpeg_create_decompress(&info);
    jpeg_mem_src(&info, bytes.data(), bytes.size());

    if (jpeg_read_header(&info, TRUE) != JPEG_HEADER_OK)
    {
        jpeg_destroy_decompress(&info);
        return false;
    }

    info.out_color_space = JCS_RGB;
    jpeg_start_decompress(&info);

    const int width = info.output_width;
    const int height = info.output_height;
    const size_t rowStride = static_cast<size_t>(width) * 3U;
    const size_t dataSize = rowStride * static_cast<size_t>(height);
    unsigned char* pixels = static_cast<unsigned char*>(RL_MALLOC(dataSize));
    if (pixels == nullptr)
    {
        jpeg_destroy_decompress(&info);
        return false;
    }

    while (info.output_scanline < info.output_height)
    {
        unsigned char* row = pixels + (info.output_scanline * rowStride);
        if (jpeg_read_scanlines(&info, &row, 1) != 1)
        {
            RL_FREE(pixels);
            jpeg_destroy_decompress(&info);
            return false;
        }
    }

    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);

    outImage.width = width;
    outImage.height = height;
    outImage.mipmaps = 1;
    outImage.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    outImage.data = pixels;
    return true;
}

bool loadImageFromBytes(const std::string& logicalPath, const std::vector<unsigned char>& bytes, Image& outImage)
{
    if (bytes.empty())
        return false;

    const std::string extension = fileExtensionFromPath(stripXzSuffix(logicalPath));
    if (!extension.empty())
    {
        outImage = LoadImageFromMemory(
            extension.c_str(),
            bytes.data(),
            static_cast<int>(bytes.size()));

        if (outImage.data != nullptr)
            return true;

        if (isJpegExtension(extension))
            return loadJpegFromBytes(bytes, outImage);
    }

    return false;
}

}

std::string resolveAssetPath(const std::string& assetRoot, const std::string& path)
{
    if (path.empty())
        return path;

    const std::filesystem::path candidate(path);
    if (candidate.is_absolute())
        return candidate.lexically_normal().string();

    if (assetRoot.empty())
        return path;

    return pathJoin(assetRoot, path);
}

std::vector<std::string> buildAssetSearchPaths(
    const std::string& assetRoot,
    const std::string& relativePath)
{
    std::vector<std::string> paths;
    auto tryAdd = [&](const std::string& candidate)
    {
        if (candidate.empty())
            return;

        if (std::find(paths.begin(), paths.end(), candidate) == paths.end())
            paths.push_back(candidate);
    };

    tryAdd(relativePath);
    tryAdd(resolveAssetPath(assetRoot, relativePath));
    tryAdd(resolveAssetPath(".", relativePath));
    tryAdd(resolveAssetPath("..", relativePath));
    tryAdd(resolveAssetPath("../..", relativePath));

    const char* appDir = GetApplicationDirectory();
    if (appDir != nullptr && appDir[0] != '\0')
    {
        const std::string appDirectory(appDir);
        tryAdd(resolveAssetPath(appDirectory, relativePath));
        tryAdd(resolveAssetPath(appDirectory + "/..", relativePath));
        tryAdd(resolveAssetPath(appDirectory + "/../..", relativePath));
    }

    return paths;
}

std::string compressedAssetPath(const std::string& path)
{
    return path + ".xz";
}

bool decompressXzFile(const std::string& path, std::vector<unsigned char>& outBytes)
{
    std::vector<unsigned char> compressedBytes;
    if (!readBinaryFile(path, compressedBytes))
        return false;

    return decompressXzBytes(compressedBytes.data(), compressedBytes.size(), outBytes);
}

bool loadAssetBytesFromFile(const std::string& path, std::vector<unsigned char>& outBytes)
{
    std::vector<unsigned char> bytes;
    if (!readBinaryFile(path, bytes))
        return false;

    const bool isXzAsset =
        path.size() >= 3 &&
        path.compare(path.size() - 3, 3, ".xz") == 0;

    if (isXzAsset)
        return decompressXzBytes(bytes.data(), bytes.size(), outBytes);

    outBytes.swap(bytes);
    return true;
}

bool loadImageFromAssetFile(const std::string& path, Image& outImage)
{
    std::vector<unsigned char> bytes;
    if (!readBinaryFile(path, bytes))
        return false;

    const bool isXzAsset =
        path.size() >= 3 &&
        path.compare(path.size() - 3, 3, ".xz") == 0;

    if (isXzAsset)
    {
        std::vector<unsigned char> decompressed;
        if (!decompressXzBytes(bytes.data(), bytes.size(), decompressed))
            return false;

        return loadImageFromBytes(path, decompressed, outImage);
    }

    return loadImageFromBytes(path, bytes, outImage);
}

bool loadTextureFromAssetFile(const std::string& path, Texture2D& outTexture)
{
    Image image{};
    if (!loadImageFromAssetFile(path, image))
        return false;

    const Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id == 0)
        return false;

    outTexture = texture;
    return true;
}

bool writeBinaryFile(const std::string& path, const unsigned char* data, size_t size)
{
    if (path.empty() || data == nullptr || size == 0)
        return false;

    std::ofstream file(path.c_str(), std::ios::binary);
    if (!file.is_open())
        return false;

    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return file.good();
}

bool ensureParentDirectoryExists(const std::string& filePath)
{
    return ensureParentDirectories(filePath);
}

bool compressBytesToXzFile(
    const unsigned char* input,
    size_t inputSize,
    const std::string& outputPath)
{
    if (input == nullptr || inputSize == 0 || outputPath.empty())
        return false;

    if (!ensureParentDirectoryExists(outputPath))
        return false;

    lzma_stream stream = LZMA_STREAM_INIT;
    const lzma_ret initResult = lzma_easy_encoder(&stream, LZMA_PRESET_DEFAULT, LZMA_CHECK_CRC64);
    if (initResult != LZMA_OK)
        return false;

    stream.next_in = input;
    stream.avail_in = inputSize;

    std::vector<unsigned char> compressed;
    unsigned char buffer[16 * 1024];
    lzma_ret codeResult = LZMA_OK;

    do
    {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        codeResult = lzma_code(&stream, LZMA_FINISH);

        const size_t produced = sizeof(buffer) - stream.avail_out;
        if (produced > 0)
            compressed.insert(compressed.end(), buffer, buffer + produced);
    }
    while (codeResult == LZMA_OK);

    lzma_end(&stream);
    if (codeResult != LZMA_STREAM_END)
        return false;

    return writeBinaryFile(outputPath, compressed.data(), compressed.size());
}

}