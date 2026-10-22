// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanPacketCipher.h"

#include <cstring>
#include <vector>

extern "C" {
#include "../deps/monocypher/monocypher.h"
}

#if defined(_WIN32)
  #include <windows.h>
  #include <bcrypt.h>
  #pragma comment(lib, "bcrypt.lib")
#elif defined(__APPLE__)
  #include <Security/SecRandom.h>
#else
  #include <sys/random.h>
#endif

namespace TinCan {

static const char Magic[4] = { 'T', 'C', 'E', '1' };

struct PacketCipher::KeyState
{
    uint8_t key[KeySize];
    uint8_t noncePrefix[16];

    ~KeyState() {
        crypto_wipe(key, sizeof(key));
    }
};

PacketCipher::PacketCipher() {}

PacketCipher::~PacketCipher()
{
    clearKey();
}

bool PacketCipher::randomBytes(uint8_t * buf, size_t len)
{
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, buf, (ULONG) len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    return SecRandomCopyBytes(kSecRandomDefault, len, buf) == errSecSuccess;
#else
    size_t got = 0;
    while (got < len) {
        ssize_t n = getrandom(buf + got, len - got, 0);
        if (n <= 0) return false;
        got += (size_t) n;
    }
    return true;
#endif
}

void PacketCipher::wipe(void * secret, size_t len)
{
    crypto_wipe(secret, len);
}

bool PacketCipher::setKey(const uint8_t * key)
{
    auto state = std::make_shared<KeyState>();
    memcpy(state->key, key, KeySize);
    if (!randomBytes(state->noncePrefix, sizeof(state->noncePrefix))) {
        return false;
    }
    std::atomic_store(&keyState, std::shared_ptr<const KeyState>(std::move(state)));
    return true;
}

void PacketCipher::clearKey()
{
    std::atomic_store(&keyState, std::shared_ptr<const KeyState>());
}

std::shared_ptr<const PacketCipher::KeyState> PacketCipher::loadKey() const
{
    return std::atomic_load(&keyState);
}

bool PacketCipher::hasKey() const
{
    return loadKey() != nullptr;
}

bool PacketCipher::isSealed(const char * data, int len)
{
    return len >= Overhead && memcmp(data, Magic, sizeof(Magic)) == 0;
}

int PacketCipher::seal(const char * in, int len, char * out, int outcap)
{
    auto state = loadKey();
    if (!state || len < 0 || outcap < len + Overhead) return -1;

    uint8_t * o = reinterpret_cast<uint8_t*>(out);
    uint8_t * nonce = o + 4;
    uint8_t * mac = o + 28;

    memcpy(o, Magic, sizeof(Magic));
    memcpy(nonce, state->noncePrefix, 16);
    uint64_t n = counter.fetch_add(1, std::memory_order_relaxed);
    for (int i = 0; i < 8; ++i) {
        nonce[16 + i] = (uint8_t) (n >> (56 - 8 * i));
    }

    crypto_aead_lock(o + Overhead, mac, state->key, nonce,
                     o, sizeof(Magic),
                     reinterpret_cast<const uint8_t*>(in), (size_t) len);

    return len + Overhead;
}

int PacketCipher::open(const char * in, int len, char * out, int outcap) const
{
    auto state = loadKey();
    if (!state || !isSealed(in, len)) return -1;

    int plainlen = len - Overhead;
    if (outcap < plainlen) return -1;

    const uint8_t * i = reinterpret_cast<const uint8_t*>(in);

    if (crypto_aead_unlock(reinterpret_cast<uint8_t*>(out), i + 28, state->key, i + 4,
                           i, sizeof(Magic),
                           i + Overhead, (size_t) plainlen) != 0) {
        return -1;
    }

    return plainlen;
}

bool PacketCipher::decodeKey(const char * base64, uint8_t * keyOut)
{
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };

    std::vector<uint8_t> bytes;
    uint32_t acc = 0;
    int bits = 0;

    for (const char * p = base64; *p && *p != '='; ++p) {
        int v = value(*p);
        if (v < 0) return false;
        acc = (acc << 6) | (uint32_t) v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back((uint8_t) (acc >> bits));
        }
    }

    bool ok = bytes.size() == KeySize;
    if (ok) {
        memcpy(keyOut, bytes.data(), KeySize);
    }
    crypto_wipe(bytes.data(), bytes.size());
    return ok;
}

}
