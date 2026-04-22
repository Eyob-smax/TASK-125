#include <gtest/gtest.h>
#include "shelterops/infrastructure/BarcodeHandler.h"

using namespace shelterops::infrastructure;

TEST(BarcodeHandler, StripsCrLf) {
    auto t = BarcodeHandler::ProcessScan("ABC123\r\n");
    EXPECT_EQ("ABC123", t.value);
}

TEST(BarcodeHandler, StripsCrOnly) {
    auto t = BarcodeHandler::ProcessScan("ABC123\r");
    EXPECT_EQ("ABC123", t.value);
}

TEST(BarcodeHandler, StripsLfOnly) {
    auto t = BarcodeHandler::ProcessScan("BC-001\n");
    EXPECT_EQ("BC-001", t.value);
}

TEST(BarcodeHandler, NonPrintableRejected) {
    std::string raw = "ABC";
    raw.push_back('\x01'); // non-printable
    auto t = BarcodeHandler::ProcessScan(raw);
    EXPECT_FALSE(t.is_printable);
}

TEST(BarcodeHandler, ValidBarcodePrintable) {
    // ValidateBarcode accepts alphanumeric with hyphens/underscores of length 3-64.
    auto t = BarcodeHandler::ProcessScan("ITEM-001");
    EXPECT_TRUE(t.is_printable);
    EXPECT_EQ("ITEM-001", t.value);
}

TEST(BarcodeHandler, EmptyInputNotPrintable) {
    auto t = BarcodeHandler::ProcessScan("");
    EXPECT_FALSE(t.is_printable);
}
