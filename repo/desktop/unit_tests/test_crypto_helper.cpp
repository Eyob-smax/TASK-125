#include <gtest/gtest.h>
#include "shelterops/infrastructure/CryptoHelper.h"
#include <sodium.h>

using namespace shelterops::infrastructure;

class CryptoHelperTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { CryptoHelper::Init(); }
};

TEST_F(CryptoHelperTest, HashNotEqualPlaintext) {
    std::string hash = CryptoHelper::HashPassword("mysecret");
    EXPECT_NE(hash, "mysecret");
    EXPECT_FALSE(hash.empty());
}

TEST_F(CryptoHelperTest, VerifyAcceptsCorrectPassword) {
    std::string hash = CryptoHelper::HashPassword("correct_horse");
    EXPECT_TRUE(CryptoHelper::VerifyPassword("correct_horse", hash));
}

TEST_F(CryptoHelperTest, VerifyRejectsWrongPassword) {
    std::string hash = CryptoHelper::HashPassword("correct_horse");
    EXPECT_FALSE(CryptoHelper::VerifyPassword("wrong_password", hash));
}

TEST_F(CryptoHelperTest, HashesAreDifferentEachTime) {
    std::string h1 = CryptoHelper::HashPassword("same");
    std::string h2 = CryptoHelper::HashPassword("same");
    // Argon2id includes a random salt; hashes must differ.
    EXPECT_NE(h1, h2);
}

TEST_F(CryptoHelperTest, EncryptDecryptRoundTrip) {
    auto key = CryptoHelper::GenerateRandomKey(32);
    std::string plaintext = "sensitive field value";
    auto cipher = CryptoHelper::Encrypt(plaintext, key);
    auto recovered = CryptoHelper::Decrypt(cipher, key);
    EXPECT_EQ(recovered, plaintext);
}

TEST_F(CryptoHelperTest, TamperedCiphertextFailsDecrypt) {
    auto key = CryptoHelper::GenerateRandomKey(32);
    auto cipher = CryptoHelper::Encrypt("hello", key);
    // Flip a byte in the ciphertext area (after 12-byte nonce).
    if (cipher.size() > 15) cipher[15] ^= 0xFF;
    EXPECT_THROW(CryptoHelper::Decrypt(cipher, key), std::runtime_error);
}

TEST_F(CryptoHelperTest, NoncePrependedAndUnique) {
    auto key = CryptoHelper::GenerateRandomKey(32);
    auto c1 = CryptoHelper::Encrypt("data", key);
    auto c2 = CryptoHelper::Encrypt("data", key);
    // Both blobs start with a 12-byte nonce; those nonces must differ.
    ASSERT_GE(c1.size(), 12u);
    ASSERT_GE(c2.size(), 12u);
    bool same_nonce = true;
    for (int i = 0; i < 12; ++i)
        if (c1[i] != c2[i]) { same_nonce = false; break; }
    EXPECT_FALSE(same_nonce);
}

TEST_F(CryptoHelperTest, GenerateRandomKeyLength) {
    auto key = CryptoHelper::GenerateRandomKey(32);
    EXPECT_EQ(key.size(), 32u);
    auto key16 = CryptoHelper::GenerateRandomKey(16);
    EXPECT_EQ(key16.size(), 16u);
}

TEST_F(CryptoHelperTest, ZeroBufferWipes) {
    char buf[17] = "secret_data_xxxx";
    CryptoHelper::ZeroBuffer(buf, sizeof(buf));
    for (char c : buf) EXPECT_EQ(c, '\0');
}
