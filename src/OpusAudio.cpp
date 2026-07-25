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

#include "OpusAudio.h"

#include <opusfile.h>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace timberline_engine
{

namespace
{

struct OpusMemorySource
{
    const unsigned char* data;
    int size;
    int offset;
};

int readOpusCallback(void* stream, unsigned char* buffer, int nbytes)
{
    OpusMemorySource* source = static_cast<OpusMemorySource*>(stream);
    if (source == nullptr || source->data == nullptr || nbytes <= 0)
        return 0;

    const int remaining = source->size - source->offset;
    if (remaining <= 0)
        return 0;

    const int toRead = remaining < nbytes ? remaining : nbytes;
    for (int i = 0; i < toRead; ++i)
        buffer[i] = source->data[source->offset + i];

    source->offset += toRead;
    return toRead;
}

int seekOpusCallback(void* stream, opus_int64 offset, int whence)
{
    OpusMemorySource* source = static_cast<OpusMemorySource*>(stream);
    if (source == nullptr)
        return -1;

    opus_int64 nextOffset = source->offset;
    switch (whence)
    {
        case SEEK_SET:
            nextOffset = offset;
            break;
        case SEEK_CUR:
            nextOffset += offset;
            break;
        case SEEK_END:
            nextOffset = source->size + offset;
            break;
        default:
            return -1;
    }

    if (nextOffset < 0 || nextOffset > source->size)
        return -1;

    source->offset = static_cast<int>(nextOffset);
    return 0;
}

int closeOpusCallback(void* /*stream*/)
{
    return 0;
}

}

bool decodeOpusBytesToWave(const unsigned char* data, int dataSize, Wave& outWave)
{
    outWave = Wave{};

    if (data == nullptr || dataSize <= 0)
        return false;

    OpusMemorySource source{};
    source.data = data;
    source.size = dataSize;
    source.offset = 0;

    OpusFileCallbacks callbacks{};
    callbacks.read = readOpusCallback;
    callbacks.seek = seekOpusCallback;
    callbacks.close = closeOpusCallback;
    callbacks.tell = nullptr;

    int openError = 0;
    OggOpusFile* opusFile = op_open_callbacks(&source, &callbacks, nullptr, 0, &openError);
    if (opusFile == nullptr)
        return false;

    const OpusHead* head = op_head(opusFile, 0);
    if (head == nullptr)
    {
        op_free(opusFile);
        return false;
    }

    std::vector<opus_int16> pcmSamples;
    pcmSamples.reserve(4096);

    const int channelCount = head->channel_count > 0 ? head->channel_count : 1;
    std::vector<opus_int16> readBuffer(4096 * channelCount);

    while (true)
    {
        const int framesRead = op_read(
            opusFile,
            readBuffer.data(),
            static_cast<int>(readBuffer.size()),
            nullptr);

        if (framesRead < 0)
        {
            op_free(opusFile);
            unloadDecodedWave(outWave);
            return false;
        }

        if (framesRead == 0)
            break;

        pcmSamples.insert(
            pcmSamples.end(),
            readBuffer.begin(),
            readBuffer.begin() + framesRead * channelCount);
    }

    op_free(opusFile);

    if (pcmSamples.empty())
        return false;

    const unsigned int frameCount = static_cast<unsigned int>(pcmSamples.size() / channelCount);
    const size_t byteCount = pcmSamples.size() * sizeof(opus_int16);
    void* waveData = std::malloc(byteCount);
    if (waveData == nullptr)
        return false;

    std::memcpy(waveData, pcmSamples.data(), byteCount);

    outWave.frameCount = frameCount;
    outWave.sampleRate = head->input_sample_rate > 0 ? head->input_sample_rate : 48000;
    outWave.sampleSize = 16;
    outWave.channels = channelCount;
    outWave.data = waveData;
    return true;
}

void unloadDecodedWave(Wave& wave)
{
    if (wave.data != nullptr)
    {
        std::free(wave.data);
        wave.data = nullptr;
    }

    wave = Wave{};
}

}