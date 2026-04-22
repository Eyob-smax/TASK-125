#include <gtest/gtest.h>
#include "shelterops/ui/primitives/ValidationState.h"

using namespace shelterops::ui::primitives;

TEST(ValidationState, DefaultHasNoErrors) {
    ValidationState vs;
    EXPECT_FALSE(vs.HasErrors());
}

TEST(ValidationState, SetErrorReportsHasErrors) {
    ValidationState vs;
    vs.SetError("name", "Name is required");
    EXPECT_TRUE(vs.HasErrors());
    EXPECT_TRUE(vs.HasError("name"));
    EXPECT_EQ("Name is required", vs.GetError("name"));
}

TEST(ValidationState, GetErrorUnknownFieldReturnsEmpty) {
    ValidationState vs;
    EXPECT_EQ("", vs.GetError("nonexistent"));
    EXPECT_FALSE(vs.HasError("nonexistent"));
}

TEST(ValidationState, ClearFieldRemovesError) {
    ValidationState vs;
    vs.SetError("email", "Invalid email");
    vs.ClearField("email");
    EXPECT_FALSE(vs.HasError("email"));
    EXPECT_FALSE(vs.HasErrors());
}

TEST(ValidationState, ClearFieldUnknownFieldNoOp) {
    ValidationState vs;
    vs.SetError("name", "Required");
    vs.ClearField("missing");
    EXPECT_TRUE(vs.HasErrors());
}

TEST(ValidationState, ClearRemovesAllErrors) {
    ValidationState vs;
    vs.SetError("name", "Required");
    vs.SetError("email", "Invalid");
    vs.Clear();
    EXPECT_FALSE(vs.HasErrors());
    EXPECT_FALSE(vs.HasError("name"));
    EXPECT_FALSE(vs.HasError("email"));
}

TEST(ValidationState, SetErrorOverwritesPrevious) {
    ValidationState vs;
    vs.SetError("phone", "Too short");
    vs.SetError("phone", "Invalid format");
    EXPECT_EQ("Invalid format", vs.GetError("phone"));
}

TEST(ValidationState, AllErrorsJoinsMessages) {
    ValidationState vs;
    vs.SetError("a", "Error A");
    vs.SetError("b", "Error B");
    std::string all = vs.AllErrors();
    EXPECT_NE(std::string::npos, all.find("Error A"));
    EXPECT_NE(std::string::npos, all.find("Error B"));
    EXPECT_NE(std::string::npos, all.find(";"));
}

TEST(ValidationState, AllErrorsEmptyWhenNoErrors) {
    ValidationState vs;
    EXPECT_TRUE(vs.AllErrors().empty());
}

TEST(ValidationState, MultipleFieldsIndependent) {
    ValidationState vs;
    vs.SetError("name", "Required");
    vs.SetError("qty", "Must be positive");
    EXPECT_TRUE(vs.HasError("name"));
    EXPECT_TRUE(vs.HasError("qty"));
    vs.ClearField("name");
    EXPECT_FALSE(vs.HasError("name"));
    EXPECT_TRUE(vs.HasError("qty"));
    EXPECT_TRUE(vs.HasErrors());
}
