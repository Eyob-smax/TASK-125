#include "shelterops/infrastructure/CryptoHelper.h"
#include <sodium.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>

namespace shelterops::infrastructure {

void CryptoHelper::Init() {
    // sodium_init() is thread-safe and idempotent - safe to call multiple times
    if (sodium_init() == -1) {
        throw std::runtime_error("CryptoHelper: libsodium initialization failed");
    }
    // Only log on first successful init (sodium_init returns 0 on first call, 1 on subsequent)
    static bool logged = false;
    if (!logged) {
        spdlog::info("CryptoHelper: libsodium initialized");
        logged = true;
    }
}

std::string CryptoHelper::HashPassword(const std::string& plaintext) {
    char hash_buf[crypto_pwhash_STRBYTES];
    int rc = crypto_pwhash_str(
        hash_buf,
        plaintext.c_str(),
        plaintext.size(),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,  // 3 iterations
        crypto_pwhash_MEMLIMIT_INTERACTIVE); // 64 MB
    if (rc != 0) {
        throw std::runtime_error("CryptoHelper::HashPassword: out of memory or failure");
    }
    return std::string(hash_buf);
}

bool CryptoHelper::VerifyPassword(const std::string& plaintext,
                                   const std::string& stored_hash) {
    return crypto_pwhash_str_verify(
               stored_hash.c_str(),
               plaintext.c_str(),
               plaintext.size()) == 0;
}

std::vector<uint8_t> CryptoHelper::GenerateRandomKey(size_t length) {
    std::vector<uint8_t> key(length);
    randombytes_buf(key.data(), length);
    return key;
}

std::vector<uint8_t> CryptoHelper::Encrypt(const std::string&         plaintext,
                                             const std::vector<uint8_t>& key) {
    if (key.size() != crypto_aead_aes256gcm_KEYBYTES) {
        throw std::runtime_error("CryptoHelper::Encrypt: key must be 32 bytes");
    }
    if (crypto_aead_aes256gcm_is_available() == 0) {
        throw std::runtime_error("CryptoHelper::Encrypt: AES-256-GCM not available on this CPU");
    }

    constexpr size_t NONCE_LEN = crypto_aead_aes256gcm_NPUBBYTES; // 12 bytes

    std::vector<uint8_t> nonce(NONCE_LEN);
    randombytes_buf(nonce.data(), NONCE_LEN);

    size_t ciphertext_len = plaintext.size() + crypto_aead_aes256gcm_ABYTES;
    std::vector<uint8_t> result(NONCE_LEN + ciphertext_len);

    // Prepend nonce
    std::memcpy(result.data(), nonce.data(), NONCE_LEN);

    unsigned long long actual_len = 0;
    int rc = crypto_aead_aes256gcm_encrypt(
        result.data() + NONCE_LEN,
        &actual_len,
        reinterpret_cast<const uint8_t*>(plaintext.data()),
        plaintext.size(),
        nullptr, 0,       // no additional data
        nullptr,          // no nsec
        nonce.data(),
        key.data());

    if (rc != 0) {
        throw std::runtime_error("CryptoHelper::Encrypt: encryption failed");
    }
    result.resize(NONCE_LEN + actual_len);
    return result;
}

std::string CryptoHelper::Decrypt(const std::vector<uint8_t>& blob,
                                   const std::vector<uint8_t>& key) {
    if (key.size() != crypto_aead_aes256gcm_KEYBYTES) {
        throw std::runtime_error("CryptoHelper::Decrypt: key must be 32 bytes");
    }
    if (crypto_aead_aes256gcm_is_available() == 0) {
        throw std::runtime_error("CryptoHelper::Decrypt: AES-256-GCM not available on this CPU");
    }

    constexpr size_t NONCE_LEN = crypto_aead_aes256gcm_NPUBBYTES;
    constexpr size_t TAG_LEN   = crypto_aead_aes256gcm_ABYTES;

    if (blob.size() < NONCE_LEN + TAG_LEN) {
        throw std::runtime_error("CryptoHelper::Decrypt: blob too short");
    }

    const uint8_t* nonce        = blob.data();
    const uint8_t* ciphertext   = blob.data() + NONCE_LEN;
    size_t         ciphertext_len = blob.size() - NONCE_LEN;

    std::string plaintext(ciphertext_len - TAG_LEN, '\0');
    unsigned long long plain_len = 0;

    int rc = crypto_aead_aes256gcm_decrypt(
        reinterpret_cast<uint8_t*>(&plaintext[0]),
        &plain_len,
        nullptr,          // no nsec
        ciphertext,
        ciphertext_len,
        nullptr, 0,       // no additional data
        nonce,
        key.data());

    if (rc != 0) {
        throw std::runtime_error(
            "CryptoHelper::Decrypt: authentication tag mismatch — data tampered or wrong key");
    }
    plaintext.resize(plain_len);
    return plaintext;
}

void CryptoHelper::ZeroBuffer(void* buf, size_t len) noexcept {
    sodium_memzero(buf, len);
}

} // namespace shelterops::infrastructure
