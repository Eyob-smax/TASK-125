#include <gtest/gtest.h>
#include "shelterops/domain/BookingStateMachine.h"

using namespace shelterops::domain;

static auto AlwaysCanApprove = []() { return true; };
static auto NeverCanApprove  = []() { return false; };

TEST(BookingStateMachine, PendingToApproved) {
    auto result = Transition(BookingStatus::Pending, BookingEvent::Approve, AlwaysCanApprove);
    ASSERT_TRUE(std::holds_alternative<BookingStatus>(result));
    EXPECT_EQ(BookingStatus::Approved, std::get<BookingStatus>(result));
}

TEST(BookingStateMachine, ApprovedToActive) {
    auto result = Transition(BookingStatus::Approved, BookingEvent::Activate, AlwaysCanApprove);
    ASSERT_TRUE(std::holds_alternative<BookingStatus>(result));
    EXPECT_EQ(BookingStatus::Active, std::get<BookingStatus>(result));
}

TEST(BookingStateMachine, ActiveToCompleted) {
    auto result = Transition(BookingStatus::Active, BookingEvent::Complete, AlwaysCanApprove);
    ASSERT_TRUE(std::holds_alternative<BookingStatus>(result));
    EXPECT_EQ(BookingStatus::Completed, std::get<BookingStatus>(result));
}

TEST(BookingStateMachine, PendingToCancelled) {
    auto result = Transition(BookingStatus::Pending, BookingEvent::Cancel, AlwaysCanApprove);
    ASSERT_TRUE(std::holds_alternative<BookingStatus>(result));
    EXPECT_EQ(BookingStatus::Cancelled, std::get<BookingStatus>(result));
}

TEST(BookingStateMachine, ApprovedToNoShow) {
    auto result = Transition(BookingStatus::Approved, BookingEvent::NoShow, AlwaysCanApprove);
    ASSERT_TRUE(std::holds_alternative<BookingStatus>(result));
    EXPECT_EQ(BookingStatus::NoShow, std::get<BookingStatus>(result));
}

TEST(BookingStateMachine, CompletedRejectsAllEvents) {
    for (auto ev : {BookingEvent::Approve, BookingEvent::Cancel, BookingEvent::Complete,
                    BookingEvent::Activate, BookingEvent::NoShow, BookingEvent::Reject}) {
        auto result = Transition(BookingStatus::Completed, ev, AlwaysCanApprove);
        ASSERT_TRUE(std::holds_alternative<StateTransitionError>(result));
        EXPECT_EQ(std::string{"ILLEGAL_TRANSITION"}, std::get<StateTransitionError>(result).code);
    }
}

TEST(BookingStateMachine, CancelledRejectsAllEvents) {
    auto result = Transition(BookingStatus::Cancelled, BookingEvent::Approve, AlwaysCanApprove);
    ASSERT_TRUE(std::holds_alternative<StateTransitionError>(result));
    EXPECT_EQ(std::string{"ILLEGAL_TRANSITION"}, std::get<StateTransitionError>(result).code);
}

TEST(BookingStateMachine, ApproveRequiresRole) {
    auto result = Transition(BookingStatus::Pending, BookingEvent::Approve, NeverCanApprove);
    ASSERT_TRUE(std::holds_alternative<StateTransitionError>(result));
    EXPECT_EQ(std::string{"APPROVAL_ROLE_REQUIRED"}, std::get<StateTransitionError>(result).code);
}

TEST(BookingStateMachine, RejectRequiresRole) {
    auto result = Transition(BookingStatus::Pending, BookingEvent::Reject, NeverCanApprove);
    ASSERT_TRUE(std::holds_alternative<StateTransitionError>(result));
    EXPECT_EQ(std::string{"APPROVAL_ROLE_REQUIRED"}, std::get<StateTransitionError>(result).code);
}

TEST(BookingStateMachine, IllegalTransitionPendingToComplete) {
    auto result = Transition(BookingStatus::Pending, BookingEvent::Complete, AlwaysCanApprove);
    ASSERT_TRUE(std::holds_alternative<StateTransitionError>(result));
}
