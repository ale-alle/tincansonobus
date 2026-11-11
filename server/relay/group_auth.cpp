// Signed lesson groups (T-048), see group_auth.hpp. SHA-256 per FIPS 180-4, HMAC per RFC 2104;
// checked against RFC 4231 test vectors in tests/relay_test.py (--self-test).
#include "group_auth.hpp"

#include <cstring>
#include <vector>

namespace tincan {

namespace {

const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256(const uint8_t * data, size_t size, uint8_t out[32])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    std::vector<uint8_t> msg(data, data + size);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    const uint64_t bits = (uint64_t) size * 8;
    for (int i = 7; i >= 0; --i) msg.push_back((uint8_t) (bits >> (i * 8)));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const uint8_t * p = &msg[chunk + i * 4];
            w[i] = ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (uint8_t) (h[i] >> 24);
        out[i * 4 + 1] = (uint8_t) (h[i] >> 16);
        out[i * 4 + 2] = (uint8_t) (h[i] >> 8);
        out[i * 4 + 3] = (uint8_t) h[i];
    }
}

std::string base64url(const uint8_t * data, size_t size)
{
    static const char * abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < size; ++i) {
        buf = (buf << 8) | data[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += abc[(buf >> bits) & 63];
        }
    }
    if (bits > 0) out += abc[(buf << (6 - bits)) & 63];
    return out;
}

bool roomNameOk(const std::string & room)
{
    if (room.empty() || room.size() > 64) return false;
    for (char ch : room) {
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-';
        if (!ok) return false;
    }
    return true;
}

} // namespace

void hmacSha256(const std::string & key, const std::string & message, uint8_t out[32])
{
    uint8_t block[64] = {0};
    if (key.size() > 64) {
        sha256((const uint8_t *) key.data(), key.size(), block);
    } else {
        std::memcpy(block, key.data(), key.size());
    }
    std::vector<uint8_t> inner(64 + message.size());
    std::vector<uint8_t> outer(64 + 32);
    for (int i = 0; i < 64; ++i) {
        inner[i] = block[i] ^ 0x36;
        outer[i] = block[i] ^ 0x5c;
    }
    std::memcpy(inner.data() + 64, message.data(), message.size());
    sha256(inner.data(), inner.size(), outer.data() + 64);
    sha256(outer.data(), outer.size(), out);
}

GroupCheck checkSignedGroup(const std::string & name, const std::string & secret, int64_t nowSeconds,
                            int64_t maxAheadSeconds)
{
    const auto lastDot = name.rfind('.');
    if (lastDot == std::string::npos || lastDot == 0) return GroupCheck::malformed;
    const auto midDot = name.rfind('.', lastDot - 1);
    if (midDot == std::string::npos) return GroupCheck::malformed;

    const std::string room = name.substr(0, midDot);
    const std::string expires = name.substr(midDot + 1, lastDot - midDot - 1);
    const std::string signature = name.substr(lastDot + 1);

    if (!roomNameOk(room) || expires.empty() || expires.size() > 12 || signature.size() != 22) {
        return GroupCheck::malformed;
    }
    int64_t exp = 0;
    for (char ch : expires) {
        if (ch < '0' || ch > '9') return GroupCheck::malformed;
        exp = exp * 10 + (ch - '0');
    }

    uint8_t mac[32];
    hmacSha256(secret, name.substr(0, lastDot), mac);
    const std::string expected = base64url(mac, 16);

    // compare in constant time
    unsigned diff = 0;
    for (size_t i = 0; i < expected.size(); ++i) diff |= (unsigned) (expected[i] ^ signature[i]);
    if (diff != 0) return GroupCheck::badSignature;

    if (exp < nowSeconds) return GroupCheck::expired;
    if (exp - nowSeconds > maxAheadSeconds) return GroupCheck::tooFarAhead;
    return GroupCheck::ok;
}

const char * groupCheckText(GroupCheck result)
{
    switch (result) {
    case GroupCheck::ok: return "ok";
    case GroupCheck::malformed: return "not a lesson group";
    case GroupCheck::badSignature: return "bad signature";
    case GroupCheck::expired: return "lesson over";
    case GroupCheck::tooFarAhead: return "expiry too far ahead";
    }
    return "?";
}

} // namespace tincan
