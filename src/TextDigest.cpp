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

#include "TextDigest.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

namespace timberline_engine
{

namespace
{

constexpr uint32_t kSha256InitialHash[8] = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
};

constexpr uint32_t kSha256RoundConstants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

uint32_t rotateRight(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32U - bits));
}

uint32_t choose(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}

uint32_t majority(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

uint32_t sigma0(uint32_t x)
{
    return rotateRight(x, 2U) ^ rotateRight(x, 13U) ^ rotateRight(x, 22U);
}

uint32_t sigma1(uint32_t x)
{
    return rotateRight(x, 6U) ^ rotateRight(x, 11U) ^ rotateRight(x, 25U);
}

uint32_t gamma0(uint32_t x)
{
    return rotateRight(x, 7U) ^ rotateRight(x, 18U) ^ (x >> 3U);
}

uint32_t gamma1(uint32_t x)
{
    return rotateRight(x, 17U) ^ rotateRight(x, 19U) ^ (x >> 10U);
}

void sha256Transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t words[64];
    for (int i = 0; i < 16; ++i)
    {
        words[i] = (static_cast<uint32_t>(block[i * 4]) << 24)
            | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
            | (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
            | static_cast<uint32_t>(block[i * 4 + 3]);
    }

    for (int i = 16; i < 64; ++i)
        words[i] = gamma1(words[i - 2]) + words[i - 7] + gamma0(words[i - 15]) + words[i - 16];

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (int i = 0; i < 64; ++i)
    {
        const uint32_t temp1 = h + sigma1(e) + choose(e, f, g) + kSha256RoundConstants[i] + words[i];
        const uint32_t temp2 = sigma0(a) + majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

}

std::string sha256Hex(const std::string& text)
{
    uint32_t state[8];
    for (int i = 0; i < 8; ++i)
        state[i] = kSha256InitialHash[i];

    const uint64_t bitLength = static_cast<uint64_t>(text.size()) * 8ULL;
    std::vector<uint8_t> message(text.begin(), text.end());
    message.push_back(0x80U);

    while ((message.size() % 64U) != 56U)
        message.push_back(0U);

    for (int i = 7; i >= 0; --i)
        message.push_back(static_cast<uint8_t>((bitLength >> (i * 8)) & 0xFFU));

    uint8_t block[64];
    for (size_t offset = 0; offset < message.size(); offset += 64U)
    {
        for (size_t i = 0; i < 64U; ++i)
            block[i] = message[offset + i];
        sha256Transform(state, block);
    }

    std::ostringstream formatted;
    formatted << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i)
        formatted << std::setw(8) << state[i];

    return formatted.str();
}

}