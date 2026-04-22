#include <gtest/gtest.h>
#include "shelterops/domain/InventoryRules.h"
#include <cmath>
#include <limits>

using namespace shelterops::domain;

// ---------------------------------------------------------------------------
// Serial number validation
// ---------------------------------------------------------------------------
TEST(SerialValidation, ValidAlphanumericSerial) {
    auto r = ValidateSerial("SN12345", false, 0);
    EXPECT_TRUE(r.is_valid);
    EXPECT_TRUE(r.error_message.empty());
}

TEST(SerialValidation, ValidSerialWithHyphen) {
    auto r = ValidateSerial("SN-123-456", false, 0);
    EXPECT_TRUE(r.is_valid);
}

TEST(SerialValidation, ValidSerialWithUnderscore) {
    auto r = ValidateSerial("ITEM_001", false, 0);
    EXPECT_TRUE(r.is_valid);
}

TEST(SerialValidation, EmptySerialInvalid) {
    auto r = ValidateSerial("", false, 0);
    EXPECT_FALSE(r.is_valid);
    EXPECT_FALSE(r.error_message.empty());
}

TEST(SerialValidation, TooLongSerialInvalid) {
    std::string long_serial(65, 'A');
    auto r = ValidateSerial(long_serial, false, 0);
    EXPECT_FALSE(r.is_valid);
}

TEST(SerialValidation, InvalidCharacterRejected) {
    auto r = ValidateSerial("SN@123", false, 0);
    EXPECT_FALSE(r.is_valid);
    EXPECT_NE(r.error_message.find("invalid character"), std::string::npos);
}

TEST(SerialValidation, DuplicateSerialRejected) {
    auto r = ValidateSerial("SN999", true, 42);
    EXPECT_FALSE(r.is_valid);
    EXPECT_EQ(r.existing_item_id, 42);
    EXPECT_FALSE(r.error_message.empty());
}

TEST(SerialValidation, MaxLengthSerial) {
    std::string max_serial(64, 'X');
    auto r = ValidateSerial(max_serial, false, 0);
    EXPECT_TRUE(r.is_valid);
}

// ---------------------------------------------------------------------------
// Barcode validation
// ---------------------------------------------------------------------------
TEST(BarcodeValidation, ValidAsciiBarcode) {
    auto r = ValidateBarcode("1234567890128");
    EXPECT_TRUE(r.is_valid);
}

TEST(BarcodeValidation, EmptyBarcodeInvalid) {
    auto r = ValidateBarcode("");
    EXPECT_FALSE(r.is_valid);
}

TEST(BarcodeValidation, NonPrintableRejected) {
    auto r = ValidateBarcode(std::string("ABC\x01XYZ"));
    EXPECT_FALSE(r.is_valid);
}

TEST(BarcodeValidation, TooLongBarcodeInvalid) {
    std::string long_barcode(129, '9');
    auto r = ValidateBarcode(long_barcode);
    EXPECT_FALSE(r.is_valid);
}

// ---------------------------------------------------------------------------
// Average daily usage
// ---------------------------------------------------------------------------
TEST(AvgDailyUsage, EmptyHistoryReturnsZero) {
    EXPECT_EQ(ComputeAverageDailyUsage({}, 1000000LL, 30), 0.0);
}

TEST(AvgDailyUsage, SingleDayFullWindow) {
    DailyUsage d{1000000LL - 86400, 7};
    EXPECT_NEAR(ComputeAverageDailyUsage({d}, 1000000LL, 30),
                7.0 / 30.0, 1e-9);
}

TEST(AvgDailyUsage, EntriesOutsideWindowExcluded) {
    constexpr int64_t now = 1000000000LL;          // far enough from epoch
    DailyUsage old_entry{1000LL, 100};             // ~11574 days ago; outside 30-day window
    DailyUsage recent{now - 86400, 5};             // 1 day ago; inside window
    double avg = ComputeAverageDailyUsage({old_entry, recent}, now, 30);
    EXPECT_NEAR(avg, 5.0 / 30.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Days of stock
// ---------------------------------------------------------------------------
TEST(DaysOfStock, ZeroUsageReturnsInfinity) {
    EXPECT_TRUE(std::isinf(ComputeDaysOfStock(100, 0.0)));
}

TEST(DaysOfStock, CorrectCalculation) {
    EXPECT_NEAR(ComputeDaysOfStock(30, 3.0), 10.0, 1e-9);
}

TEST(DaysOfStock, ZeroQuantityZeroDays) {
    EXPECT_NEAR(ComputeDaysOfStock(0, 2.0), 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// IsLowStock
// ---------------------------------------------------------------------------
TEST(LowStock, BelowThresholdIsLow) {
    EXPECT_TRUE(IsLowStock(10, 2.0, 7));   // 5 days < 7 threshold
}

TEST(LowStock, AboveThresholdIsNotLow) {
    EXPECT_FALSE(IsLowStock(50, 2.0, 7));  // 25 days >= 7 threshold
}

TEST(LowStock, ExactlyAtThresholdIsNotLow) {
    EXPECT_FALSE(IsLowStock(14, 2.0, 7));  // exactly 7 days
}

TEST(LowStock, ZeroUsageNeverLow) {
    EXPECT_FALSE(IsLowStock(0, 0.0, 7));
}

// ---------------------------------------------------------------------------
// IsExpiringSoon / IsExpired
// ---------------------------------------------------------------------------
TEST(Expiry, ExpiredItemDetected) {
    EXPECT_TRUE(IsExpired(1000, 1001));
    EXPECT_TRUE(IsExpired(1000, 1000));  // exact boundary = expired
}

TEST(Expiry, NotExpiredYet) {
    EXPECT_FALSE(IsExpired(2000, 1000));
}

TEST(Expiry, NoExpiryDateNeverExpires) {
    EXPECT_FALSE(IsExpired(0, 9999999));
}

TEST(Expiry, ExpiringSoonWithin14Days) {
    int64_t now    = 1000000;
    int64_t expiry = now + 10 * 86400;  // 10 days away, threshold 14
    EXPECT_TRUE(IsExpiringSoon(expiry, now, 14));
}

TEST(Expiry, NotExpiringSoonBeyondThreshold) {
    int64_t now    = 1000000;
    int64_t expiry = now + 20 * 86400;  // 20 days away, threshold 14
    EXPECT_FALSE(IsExpiringSoon(expiry, now, 14));
}

TEST(Expiry, ExpiredItemNotExpiringSoon) {
    int64_t now    = 1000000;
    int64_t expiry = now - 86400;  // already expired
    EXPECT_FALSE(IsExpiringSoon(expiry, now, 14));
}
