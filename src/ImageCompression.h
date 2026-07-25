/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ******************************************************************************/

#ifndef IMAGE_COMPRESSION_H
#define IMAGE_COMPRESSION_H

#include <raylib.h>
#include <string>
#include <vector>

namespace timberline_engine
{

std::string resolveAssetPath(const std::string& assetRoot, const std::string& path);

std::vector<std::string> buildAssetSearchPaths(
    const std::string& assetRoot,
    const std::string& relativePath);

std::string compressedAssetPath(const std::string& path);

bool decompressXzFile(const std::string& path, std::vector<unsigned char>& outBytes);

bool compressBytesToXzFile(
    const unsigned char* input,
    size_t inputSize,
    const std::string& outputPath);

bool writeBinaryFile(const std::string& path, const unsigned char* data, size_t size);

bool ensureParentDirectoryExists(const std::string& filePath);

bool loadAssetBytesFromFile(const std::string& path, std::vector<unsigned char>& outBytes);

bool loadImageFromAssetFile(const std::string& path, Image& outImage);

bool loadTextureFromAssetFile(const std::string& path, Texture2D& outTexture);

}

#endif /* IMAGE_COMPRESSION_H */