// crypto.cpp — SHA-256 + PBKDF2 via Windows CNG (bcrypt.h).
#include "crypto.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace crypto {

static std::string ToHex(const std::uint8_t* data, std::size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0xF]);
    }
    return out;
}

static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string Sha256Hex(const std::uint8_t* data, std::size_t len) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM,
                                                nullptr, 0)))
        return {};

    std::uint8_t hash[32];
    NTSTATUS st = BCryptHash(alg, nullptr, 0, (PUCHAR)data, (ULONG)len, hash, 32);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!NT_SUCCESS(st)) return {};
    return ToHex(hash, 32);
}

std::string Sha256Hex(const std::string& data) {
    return Sha256Hex(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

std::string RandomHex(std::size_t bytes) {
    std::vector<std::uint8_t> buf(bytes);
    std::random_device rd;
    for (std::size_t i = 0; i < bytes; i += 4) {
        std::uint32_t v = rd();
        std::memcpy(buf.data() + i, &v, std::min<std::size_t>(4, bytes - i));
    }
    return ToHex(buf.data(), buf.size());
}

std::string HashPassword(const std::string& password) {
    const std::uint32_t iterations = 120000;
    std::string saltHex = RandomHex(16);
    if (saltHex.empty()) return {};

    std::vector<std::uint8_t> salt(16);
    for (std::size_t i = 0; i < 16; ++i)
        salt[i] = (std::uint8_t)((HexVal(saltHex[i * 2]) << 4) | HexVal(saltHex[i * 2 + 1]));

    std::uint8_t derived[32] = {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM,
                                                nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
        return {};

    NTSTATUS st = BCryptDeriveKeyPBKDF2(alg,
                                        (PUCHAR)password.data(),
                                        (ULONG)password.size(),
                                        salt.data(), (ULONG)salt.size(),
                                        iterations,
                                        derived, sizeof(derived), 0);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!NT_SUCCESS(st)) return {};

    return "pbkdf2$120000$" + saltHex + "$" + ToHex(derived, 32);
}

bool VerifyPassword(const std::string& password, const std::string& stored) {
    // Format: pbkdf2$<iter>$<saltHex>$<hashHex>
    if (stored.rfind("pbkdf2$", 0) != 0) return false;

    size_t p1 = stored.find('$', 7);
    size_t p2 = stored.find('$', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) return false;

    std::uint32_t iterations = 0;
    try {
        iterations = std::stoul(stored.substr(7, p1 - 7));
    } catch (...) {
        return false;
    }
    if (iterations == 0 || iterations > 5000000) return false;

    std::string saltHex = stored.substr(p1 + 1, p2 - p1 - 1);
    std::string hashHex = stored.substr(p2 + 1);
    if (saltHex.size() < 16 || hashHex.size() != 64) return false;

    std::size_t saltLen = saltHex.size() / 2;
    std::vector<std::uint8_t> salt(saltLen);
    for (std::size_t i = 0; i < saltLen; ++i) {
        int hi = HexVal(saltHex[i * 2]);
        int lo = HexVal(saltHex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        salt[i] = (std::uint8_t)((hi << 4) | lo);
    }

    std::uint8_t derived[32] = {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM,
                                                nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
        return false;

    NTSTATUS st = BCryptDeriveKeyPBKDF2(alg,
                                        (PUCHAR)password.data(),
                                        (ULONG)password.size(),
                                        salt.data(), (ULONG)salt.size(),
                                        iterations,
                                        derived, sizeof(derived), 0);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!NT_SUCCESS(st)) return false;

    std::string calc = ToHex(derived, 32);

    // Constant-time-ish comparison.
    volatile std::uint8_t diff = 0;
    for (std::size_t i = 0; i < 32; ++i)
        diff |= (std::uint8_t)(calc[i] ^ hashHex[i]);
    return diff == 0;
}

} // namespace crypto
