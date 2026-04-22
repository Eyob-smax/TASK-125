#include <gtest/gtest.h>
#include "shelterops/infrastructure/DeviceFingerprint.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include <sodium.h>

using namespace shelterops::infrastructure;

class DeviceFingerprintTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { CryptoHelper::Init(); }
};

TEST_F(DeviceFingerprintTest, Deterministic) {
    std::vector<uint8_t> key(32, 0x42);
    auto fp1 = DeviceFingerprint::ComputeFingerprint("machine-1", "alice", key);
    auto fp2 = DeviceFingerprint::ComputeFingerprint("machine-1", "alice", key);
    EXPECT_EQ(fp1, fp2);
}

TEST_F(DeviceFingerprintTest, DifferentMachineIdDiffers) {
    std::vector<uint8_t> key(32, 0x42);
    auto fp1 = DeviceFingerprint::ComputeFingerprint("machine-A", "alice", key);
    auto fp2 = DeviceFingerprint::ComputeFingerprint("machine-B", "alice", key);
    EXPECT_NE(fp1, fp2);
}

TEST_F(DeviceFingerprintTest, DifferentOperatorDiffers) {
    std::vector<uint8_t> key(32, 0x42);
    auto fp1 = DeviceFingerprint::ComputeFingerprint("machine-1", "alice", key);
    auto fp2 = DeviceFingerprint::ComputeFingerprint("machine-1", "bob", key);
    EXPECT_NE(fp1, fp2);
}

TEST_F(DeviceFingerprintTest, DifferentKeyDiffers) {
    std::vector<uint8_t> key1(32, 0x42);
    std::vector<uint8_t> key2(32, 0x43);
    auto fp1 = DeviceFingerprint::ComputeFingerprint("machine-1", "alice", key1);
    auto fp2 = DeviceFingerprint::ComputeFingerprint("machine-1", "alice", key2);
    EXPECT_NE(fp1, fp2);
}

TEST_F(DeviceFingerprintTest, EmptyKeyThrows) {
    std::vector<uint8_t> empty_key;
    EXPECT_THROW(
        DeviceFingerprint::ComputeFingerprint("m", "u", empty_key),
        std::invalid_argument);
}

TEST_F(DeviceFingerprintTest, GetMachineIdNonEmpty) {
    std::string mid = DeviceFingerprint::GetMachineId();
    EXPECT_FALSE(mid.empty());
}

TEST_F(DeviceFingerprintTest, FingerprintIs64HexChars) {
    std::vector<uint8_t> key(32, 0x01);
    auto fp = DeviceFingerprint::ComputeFingerprint("m", "u", key);
    EXPECT_EQ(fp.size(), 64u);
    for (char c : fp) {
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        EXPECT_TRUE(hex);
    }
}

// --- ComputeSessionBindingFingerprint tests ---

TEST_F(DeviceFingerprintTest, SessionBindingFingerprintIs64HexChars) {
    auto fp = DeviceFingerprint::ComputeSessionBindingFingerprint("machine-1", "alice");
    EXPECT_EQ(fp.size(), 64u);
    for (char c : fp) {
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        EXPECT_TRUE(hex) << "Non-hex char: " << c;
    }
}

TEST_F(DeviceFingerprintTest, SessionBindingFingerprintIsDeterministic) {
    auto fp1 = DeviceFingerprint::ComputeSessionBindingFingerprint("machine-1", "alice");
    auto fp2 = DeviceFingerprint::ComputeSessionBindingFingerprint("machine-1", "alice");
    EXPECT_EQ(fp1, fp2);
}

TEST_F(DeviceFingerprintTest, SessionBindingFingerprintDiffersAcrossMachines) {
    auto fp1 = DeviceFingerprint::ComputeSessionBindingFingerprint("machine-A", "alice");
    auto fp2 = DeviceFingerprint::ComputeSessionBindingFingerprint("machine-B", "alice");
    EXPECT_NE(fp1, fp2);
}

TEST_F(DeviceFingerprintTest, SessionBindingFingerprintDiffersAcrossOperators) {
    auto fp1 = DeviceFingerprint::ComputeSessionBindingFingerprint("machine-1", "alice");
    auto fp2 = DeviceFingerprint::ComputeSessionBindingFingerprint("machine-1", "bob");
    EXPECT_NE(fp1, fp2);
}

TEST_F(DeviceFingerprintTest, SessionBindingFingerprintDiffersFromKeyedFingerprint) {
    // ComputeFingerprint (keyed HMAC) and ComputeSessionBindingFingerprint
    // use different key derivation paths — their outputs must not collide.
    std::vector<uint8_t> key(32, 0x42);
    auto keyed = DeviceFingerprint::ComputeFingerprint("machine-1", "alice", key);
    auto binding = DeviceFingerprint::ComputeSessionBindingFingerprint("machine-1", "alice");
    EXPECT_NE(keyed, binding);
}
