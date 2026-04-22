#include <gtest/gtest.h>
#include "shelterops/common/Validation.h"

using namespace shelterops::common;

TEST(Validation, NonEmpty_EmptyFails) {
    EXPECT_FALSE(IsNonEmpty("").ok);
    EXPECT_FALSE(IsNonEmpty("   ").ok);
}
TEST(Validation, NonEmpty_ValuePasses) {
    EXPECT_TRUE(IsNonEmpty("hello").ok);
}

TEST(Validation, EmailShape_ValidPasses) {
    EXPECT_TRUE(IsEmailShape("user@example.com").ok);
    EXPECT_TRUE(IsEmailShape("a@b.org").ok);
}
TEST(Validation, EmailShape_InvalidFails) {
    EXPECT_FALSE(IsEmailShape("no-at-sign").ok);
    EXPECT_FALSE(IsEmailShape("@nodomain").ok);
    EXPECT_FALSE(IsEmailShape("two@@at.com").ok);
    EXPECT_FALSE(IsEmailShape("user@nodot").ok);
    EXPECT_FALSE(IsEmailShape("user@dot.").ok);
}

TEST(Validation, E164Phone_ValidPasses) {
    EXPECT_TRUE(IsE164PhoneShape("+15555551234").ok);
    EXPECT_TRUE(IsE164PhoneShape("+441234567890").ok);
}
TEST(Validation, E164Phone_InvalidFails) {
    EXPECT_FALSE(IsE164PhoneShape("5555551234").ok);   // no +
    EXPECT_FALSE(IsE164PhoneShape("+1").ok);            // too short
    EXPECT_FALSE(IsE164PhoneShape("+1234567890123456").ok); // too long
    EXPECT_FALSE(IsE164PhoneShape("+1234abc789").ok);   // non-digit
}

TEST(Validation, PrintableAscii_ValidPasses) {
    EXPECT_TRUE(IsPrintableAscii("hello!").ok);
    EXPECT_TRUE(IsPrintableAscii("").ok);
}
TEST(Validation, PrintableAscii_ControlCharFails) {
    EXPECT_FALSE(IsPrintableAscii("a\tb").ok);
    EXPECT_FALSE(IsPrintableAscii("a\nb").ok);
}

TEST(Validation, AlphanumHyphenUnderscore_ValidPasses) {
    EXPECT_TRUE(IsAlphanumericHyphenUnderscore("abc-123_xyz").ok);
}
TEST(Validation, AlphanumHyphenUnderscore_SpaceFails) {
    EXPECT_FALSE(IsAlphanumericHyphenUnderscore("hello world").ok);
    EXPECT_FALSE(IsAlphanumericHyphenUnderscore("a@b").ok);
}

TEST(Validation, WithinLength_BoundaryPasses) {
    EXPECT_TRUE(IsWithinLength("abc", 1, 5).ok);
    EXPECT_TRUE(IsWithinLength("a", 1, 1).ok);
}
TEST(Validation, WithinLength_TooShortFails) {
    EXPECT_FALSE(IsWithinLength("", 1, 5).ok);
}
TEST(Validation, WithinLength_TooLongFails) {
    EXPECT_FALSE(IsWithinLength("toolong", 1, 5).ok);
}
