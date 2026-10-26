// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanTest.h"
#include "../Source/TinCanPacketCipher.h"

#include <cstring>
#include <set>
#include <string>

using TinCan::PacketCipher;

static void makeKey(uint8_t * key, uint8_t seed)
{
    for (int i = 0; i < PacketCipher::KeySize; ++i) key[i] = (uint8_t) (seed + i);
}

void runPacketCipherTests()
{
    uint8_t keyA[32], keyB[32];
    makeKey(keyA, 1);
    makeKey(keyB, 2);

    const char msg[] = "/aoo/sink/1/data plus some audio bytes";
    const int msglen = (int) sizeof(msg);
    char sealed[256], plain[256];

    // no key: nothing is sealed or opened
    {
        PacketCipher c;
        CHECK(!c.hasKey());
        CHECK(c.seal(msg, msglen, sealed, sizeof(sealed)) == -1);
    }

    // round trip between two peers with the same key
    {
        PacketCipher a, b;
        CHECK(a.setKey(keyA));
        CHECK(b.setKey(keyA));
        int n = a.seal(msg, msglen, sealed, sizeof(sealed));
        CHECK(n == msglen + PacketCipher::Overhead);
        CHECK(PacketCipher::isSealed(sealed, n));
        CHECK(memcmp(sealed + PacketCipher::Overhead, msg, msglen) != 0);
        int m = b.open(sealed, n, plain, sizeof(plain));
        CHECK(m == msglen);
        CHECK(memcmp(plain, msg, msglen) == 0);

        // empty payload
        n = a.seal(msg, 0, sealed, sizeof(sealed));
        CHECK(n == PacketCipher::Overhead);
        CHECK(b.open(sealed, n, plain, sizeof(plain)) == 0);
    }

    // tampering with any byte is rejected
    {
        PacketCipher a;
        CHECK(a.setKey(keyA));
        int n = a.seal(msg, msglen, sealed, sizeof(sealed));
        for (int i = 0; i < n; ++i) {
            char copy[256];
            memcpy(copy, sealed, n);
            copy[i] ^= 0x01;
            CHECK(a.open(copy, n, plain, sizeof(plain)) == -1);
        }
        // truncated
        CHECK(a.open(sealed, n - 1, plain, sizeof(plain)) == -1);
        CHECK(a.open(sealed, 10, plain, sizeof(plain)) == -1);
    }

    // a different lesson key cannot open it
    {
        PacketCipher a, b;
        CHECK(a.setKey(keyA));
        CHECK(b.setKey(keyB));
        int n = a.seal(msg, msglen, sealed, sizeof(sealed));
        CHECK(b.open(sealed, n, plain, sizeof(plain)) == -1);
    }

    // plaintext is not mistaken for a sealed packet
    {
        PacketCipher a;
        CHECK(a.setKey(keyA));
        CHECK(!PacketCipher::isSealed(msg, msglen));
        CHECK(a.open(msg, msglen, plain, sizeof(plain)) == -1);
    }

    // output buffers that are too small
    {
        PacketCipher a;
        CHECK(a.setKey(keyA));
        CHECK(a.seal(msg, msglen, sealed, msglen + PacketCipher::Overhead - 1) == -1);
        int n = a.seal(msg, msglen, sealed, sizeof(sealed));
        CHECK(a.open(sealed, n, plain, msglen - 1) == -1);
    }

    // nonces never repeat, even across a key reset
    {
        PacketCipher a;
        CHECK(a.setKey(keyA));
        std::set<std::string> nonces;
        for (int i = 0; i < 10000; ++i) {
            if (i == 5000) CHECK(a.setKey(keyA));
            int n = a.seal(msg, msglen, sealed, sizeof(sealed));
            CHECK(n > 0);
            nonces.insert(std::string(sealed + 4, 24));
        }
        CHECK(nonces.size() == 10000);
    }

    // clearKey stops encryption
    {
        PacketCipher a;
        CHECK(a.setKey(keyA));
        a.clearKey();
        CHECK(!a.hasKey());
        CHECK(a.seal(msg, msglen, sealed, sizeof(sealed)) == -1);
    }

    // key decoding: standard and url-safe base64, with or without padding
    {
        // 32 bytes 0x01..0x20
        uint8_t expect[32], out[32];
        makeKey(expect, 1);
        CHECK(PacketCipher::decodeKey("AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA=", out));
        CHECK(memcmp(out, expect, 32) == 0);
        CHECK(PacketCipher::decodeKey("AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA", out));
        CHECK(memcmp(out, expect, 32) == 0);

        // url-safe alphabet: 32 bytes of 0xfb encode to "-_v7..." style characters
        uint8_t fb[32];
        memset(fb, 0xfb, 32);
        CHECK(PacketCipher::decodeKey("-_v7-_v7-_v7-_v7-_v7-_v7-_v7-_v7-_v7-_v7-_s", out));
        CHECK(memcmp(out, fb, 32) == 0);
        CHECK(PacketCipher::decodeKey("+/v7+/v7+/v7+/v7+/v7+/v7+/v7+/v7+/v7+/v7+/s=", out));
        CHECK(memcmp(out, fb, 32) == 0);

        CHECK(!PacketCipher::decodeKey("AQID", out));            // too short
        CHECK(!PacketCipher::decodeKey("AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyAh", out)); // 33 bytes
        CHECK(!PacketCipher::decodeKey("not base64 at all!!", out));
    }
}
