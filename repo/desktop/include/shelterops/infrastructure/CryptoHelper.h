#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace shelterops::infrastructure {

// ---------------------------------------------------------------------------
// CryptoHelper — password hashing and symmetric field encryption
//
// Password hashing: Argon2id via libsodium crypto_pwhash
//   Parameters: opslimit=3, memlimit=64MB, OPSLIM_INTERACTIVE minimum.
//   The full encoded hash string (salt included) is stored; plaintext is
//   zero-wiped from the call-site buffer after return.
//
// Field encryption: AES-256-GCM via libsodium crypto_aead_aes256gcm
//   Layout: [12-byte nonce][ciphertext+tag]
//   A unique random nonce is generated per Encrypt call.
//   The key must be exactly 32 bytes (256 bits).
// ---------------------------------------------------------------------------

class CryptoHelper {
public:
    // Must be called once at application startup before any crypto operation.
    static void Init();

    // --- Password hashing ---

    // Returns the Argon2id encoded hash string (safe to store verbatim).
    // Throws std::runtime_error on hashing failure.
    static std::string HashPassword(const std::string& plaintext);

    // Returns true if plaintext matches the stored Argon2id hash.
    static bool VerifyPassword(const std::string& plaintext,
                               const std::string& stored_hash);

    // --- Field encryption ---

    // Generates a cryptographically random key of `length` bytes.
    static std::vector<uint8_t> GenerateRandomKey(size_t length = 32);

    // Encrypts plaintext with the given 32-byte key.
    // Returns: [12-byte nonce][ciphertext+tag] as a byte vector.
    static std::vector<uint8_t> Encrypt(const std::string&         plaintext,
                                         const std::vector<uint8_t>& key);

    // Decrypts a blob produced by Encrypt.
    // Throws std::runtime_error on tag mismatch (tampering detected).
    static std::string Decrypt(const std::vector<uint8_t>& ciphertext_blob,
                                const std::vector<uint8_t>& key);

    // Wipes a buffer using sodium_memzero (compiler-barrier safe).
    static void ZeroBuffer(void* buf, size_t len) noexcept;
};

} // namespace shelterops::infrastructure
