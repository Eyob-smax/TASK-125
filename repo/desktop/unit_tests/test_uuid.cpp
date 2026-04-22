#include <gtest/gtest.h>
#include "shelterops/common/Uuid.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include <set>
#include <regex>

using namespace shelterops::common;
using namespace shelterops::infrastructure;

class UuidTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { CryptoHelper::Init(); }
};

TEST_F(UuidTest, LengthIs36) {
    std::string uuid = GenerateUuidV4();
    EXPECT_EQ(uuid.size(), 36u);
}

TEST_F(UuidTest, MatchesV4Pattern) {
    // Format: xxxxxxxx-xxxx-4xxx-[89ab]xxx-xxxxxxxxxxxx
    std::regex pattern(
        "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
    std::string uuid = GenerateUuidV4();
    EXPECT_TRUE(std::regex_match(uuid, pattern)) << "UUID: " << uuid;
}

TEST_F(UuidTest, UniqueOverManyDraws) {
    std::set<std::string> seen;
    for (int i = 0; i < 10000; ++i) {
        seen.insert(GenerateUuidV4());
    }
    EXPECT_EQ(seen.size(), 10000u);
}
