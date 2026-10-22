// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace TinCan {

// Per-lesson encryption of UDP packets between peers (D-009, phase 1).
//
// Sealed packet layout:
//   "TCE1" (4) | nonce (24) | mac (16) | ciphertext (n)
// XChaCha20-Poly1305 (Monocypher), the magic is authenticated as associated data.
// Nonce = 16 random bytes chosen once per key + 64-bit big-endian counter,
// so nonces never repeat for one sender and collide across senders with negligible probability.
//
// Not covered yet: replay protection (AOO sequence numbers drop most duplicates),
// key rotation within a lesson, traffic to the connection server (stays plaintext).
class PacketCipher
{
public:
    static constexpr int KeySize = 32;
    static constexpr int Overhead = 4 + 24 + 16;

    PacketCipher();
    ~PacketCipher();

    // key must be KeySize bytes; returns false if the OS random generator failed
    bool setKey(const uint8_t * key);
    void clearKey();
    bool hasKey() const;

    // returns the sealed size, or -1 if outcap is too small or no key is set
    int seal(const char * in, int len, char * out, int outcap);

    // returns the plaintext size, or -1 if the packet is not sealed, forged, or for another key
    int open(const char * in, int len, char * out, int outcap) const;

    static bool isSealed(const char * data, int len);

    // decode standard or url-safe base64 (padding optional) into exactly KeySize bytes
    static bool decodeKey(const char * base64, uint8_t * keyOut);

    // fill buf with bytes from the OS cryptographic random generator
    static bool randomBytes(uint8_t * buf, size_t len);

    // erase key material so the compiler can't optimise it away
    static void wipe(void * secret, size_t len);

private:
    struct KeyState;
    std::shared_ptr<const KeyState> loadKey() const;

    std::shared_ptr<const KeyState> keyState;
    std::atomic<uint64_t> counter { 0 };
};

}
