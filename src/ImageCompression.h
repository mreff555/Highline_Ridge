#ifndef IMAGE_COMPRESSION_H
#define IMAGE_COMPRESSION_H

#include <raylib.h>
#include <string>
#include <vector>

namespace testgame
{

std::string resolveAssetPath(const std::string& assetRoot, const std::string& path);

std::vector<std::string> buildAssetSearchPaths(
    const std::string& assetRoot,
    const std::string& relativePath);

std::string compressedAssetPath(const std::string& path);

bool decompressXzFile(const std::string& path, std::vector<unsigned char>& outBytes);

bool loadImageFromAssetFile(const std::string& path, Image& outImage);

bool loadTextureFromAssetFile(const std::string& path, Texture2D& outTexture);

}

#endif /* IMAGE_COMPRESSION_H */