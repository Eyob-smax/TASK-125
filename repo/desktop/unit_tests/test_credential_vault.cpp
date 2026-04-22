#include <gtest/gtest.h>
#include "shelterops/infrastructure/CredentialVault.h"

using namespace shelterops::infrastructure;

TEST(InMemoryCredentialVault, StoreAndLoad) {
    InMemoryCredentialVault vault;
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    vault.Store("mykey", data);
    auto result = vault.Load("mykey");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data, data);
}

TEST(InMemoryCredentialVault, MissingKeyReturnsNullopt) {
    InMemoryCredentialVault vault;
    auto result = vault.Load("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST(InMemoryCredentialVault, OverwriteReplaces) {
    InMemoryCredentialVault vault;
    vault.Store("key", {0x01});
    vault.Store("key", {0x02, 0x03});
    auto r = vault.Load("key");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->data, (std::vector<uint8_t>{0x02, 0x03}));
}

TEST(InMemoryCredentialVault, RemoveDeletesEntry) {
    InMemoryCredentialVault vault;
    vault.Store("key", {0xAA});
    vault.Remove("key");
    EXPECT_FALSE(vault.Load("key").has_value());
}

TEST(InMemoryCredentialVault, RemoveNonexistentIsNoOp) {
    InMemoryCredentialVault vault;
    EXPECT_NO_THROW(vault.Remove("does_not_exist"));
}

TEST(InMemoryCredentialVault, MultipleKeysIndependent) {
    InMemoryCredentialVault vault;
    vault.Store("a", {0x01});
    vault.Store("b", {0x02});
    EXPECT_EQ(vault.Load("a")->data, (std::vector<uint8_t>{0x01}));
    EXPECT_EQ(vault.Load("b")->data, (std::vector<uint8_t>{0x02}));
    vault.Remove("a");
    EXPECT_FALSE(vault.Load("a").has_value());
    EXPECT_TRUE(vault.Load("b").has_value());
}
