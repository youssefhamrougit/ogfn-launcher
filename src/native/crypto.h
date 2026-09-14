// crypto.h — SHA-256 + PBKDF2 password hashing (BCrypt / CNG).
#pragma once

#include <cstdint>
#include <string>

namespace crypto {

// SHA-256 of a byte buffer/string, hex-encoded (lowercase).
std::string Sha256Hex(const std::uint8_t* data, std::size_t len);
std::string Sha256Hex(const std::string& data);

// Hash a password for storage: "pbkdf2$<iterations>$<saltHex>$<hashHex>".
// iterations = 120000 by default.
std::string HashPassword(const std::string& password);

// Verify a password against a stored hash (constant-time-ish compare).
bool VerifyPassword(const std::string& password, const std::string& stored);

// Random hex string (16 bytes = 32 chars) for session tokens/salt.
std::string RandomHex(std::size_t bytes);

} // namespace crypto
