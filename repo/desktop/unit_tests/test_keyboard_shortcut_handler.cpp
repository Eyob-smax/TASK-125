#include <gtest/gtest.h>
#include "shelterops/shell/KeyboardShortcutHandler.h"

using namespace shelterops::shell;
using namespace shelterops::domain;

// VK constants matching KeyboardShortcutHandler.cpp
static constexpr int VK_F    = 0x46;
static constexpr int VK_N    = 0x4E;
static constexpr int VK_W    = 0x57;
static constexpr int VK_E    = 0x45;
static constexpr int VK_L    = 0x4C;
static constexpr int VK_F2   = 0x71;

TEST(KeyboardShortcutHandler, CtrlFGlobalSearch) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::BeginGlobalSearch,
              ksh.Evaluate(true, false, false, VK_F));
}

TEST(KeyboardShortcutHandler, CtrlNNewRecord) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::NewRecord,
              ksh.Evaluate(true, false, false, VK_N));
}

TEST(KeyboardShortcutHandler, F2EditRecord) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::EditRecord,
              ksh.Evaluate(false, false, false, VK_F2));
}

TEST(KeyboardShortcutHandler, CtrlShiftEExportTable) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::ExportTable,
              ksh.Evaluate(true, true, false, VK_E));
}

TEST(KeyboardShortcutHandler, CtrlWCloseWindow) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::CloseActiveWindow,
              ksh.Evaluate(true, false, false, VK_W));
}

TEST(KeyboardShortcutHandler, CtrlShiftLLogout) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::BeginLogout,
              ksh.Evaluate(true, true, false, VK_L));
}

TEST(KeyboardShortcutHandler, AuditorCanSearch) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::Auditor, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::BeginGlobalSearch,
              ksh.Evaluate(true, false, false, VK_F));
}

TEST(KeyboardShortcutHandler, AuditorCannotNewRecord) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::Auditor, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::None,
              ksh.Evaluate(true, false, false, VK_N));
    EXPECT_FALSE(ksh.IsEnabled(ShortcutAction::NewRecord));
}

TEST(KeyboardShortcutHandler, AuditorCannotEditRecord) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::Auditor, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::None,
              ksh.Evaluate(false, false, false, VK_F2));
    EXPECT_FALSE(ksh.IsEnabled(ShortcutAction::EditRecord));
}

TEST(KeyboardShortcutHandler, AuditorCannotExportTable) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::Auditor, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::None,
              ksh.Evaluate(true, true, false, VK_E));
    EXPECT_FALSE(ksh.IsEnabled(ShortcutAction::ExportTable));
}

TEST(KeyboardShortcutHandler, NoActiveWindowDisablesWindowShortcuts) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::None, false);
    // Close window should be disabled with no active window
    EXPECT_EQ(ShortcutAction::None,
              ksh.Evaluate(true, false, false, VK_W));
}

TEST(KeyboardShortcutHandler, UnknownKeyReturnsNone) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::None,
              ksh.Evaluate(true, false, false, 0x41)); // VK_A — no binding
}

TEST(KeyboardShortcutHandler, CtrlFWithoutCtrlNotMatched) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, false);
    EXPECT_EQ(ShortcutAction::None,
              ksh.Evaluate(false, false, false, VK_F));
}

TEST(KeyboardShortcutHandler, IsEnabledSearchAlwaysTrue) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::Auditor, WindowId::None, false);
    EXPECT_TRUE(ksh.IsEnabled(ShortcutAction::BeginGlobalSearch));
}

TEST(KeyboardShortcutHandler, IsEnabledLogoutAlwaysTrue) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::Auditor, WindowId::None, false);
    EXPECT_TRUE(ksh.IsEnabled(ShortcutAction::BeginLogout));
}

// ─── In-edit-mode constraints ──────────────────────────────────────────────

TEST(KeyboardShortcutHandler, InEditModeCtrlNDisabled) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, true /*in_edit_mode*/);
    EXPECT_EQ(ShortcutAction::None, ksh.Evaluate(true, false, false, VK_N));
    EXPECT_FALSE(ksh.IsEnabled(ShortcutAction::NewRecord));
}

TEST(KeyboardShortcutHandler, InEditModeF2Disabled) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, true);
    EXPECT_EQ(ShortcutAction::None, ksh.Evaluate(false, false, false, VK_F2));
    EXPECT_FALSE(ksh.IsEnabled(ShortcutAction::EditRecord));
}

TEST(KeyboardShortcutHandler, InEditModeCtrlFStillWorks) {
    // Global search is not disabled by edit mode.
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, true);
    EXPECT_EQ(ShortcutAction::BeginGlobalSearch, ksh.Evaluate(true, false, false, VK_F));
}

TEST(KeyboardShortcutHandler, InEditModeCtrlShiftEStillWorks) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, true);
    EXPECT_EQ(ShortcutAction::ExportTable, ksh.Evaluate(true, true, false, VK_E));
}

// ─── Window switching shortcuts ────────────────────────────────────────────

TEST(KeyboardShortcutHandler, CloseActiveWindowEnabled) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::OperationsManager, WindowId::KennelBoard, false);
    EXPECT_TRUE(ksh.IsEnabled(ShortcutAction::CloseActiveWindow));
}

TEST(KeyboardShortcutHandler, AllActionsEnumeratedExhaustively) {
    KeyboardShortcutHandler ksh;
    ksh.SetContext(UserRole::Administrator, WindowId::KennelBoard, false);

    // IsEnabled(None) must be false.
    EXPECT_FALSE(ksh.IsEnabled(ShortcutAction::None));

    // All non-None actions have a defined enabled state.
    EXPECT_TRUE(ksh.IsEnabled(ShortcutAction::BeginGlobalSearch));
    EXPECT_TRUE(ksh.IsEnabled(ShortcutAction::NewRecord));
    EXPECT_TRUE(ksh.IsEnabled(ShortcutAction::EditRecord));
    EXPECT_TRUE(ksh.IsEnabled(ShortcutAction::ExportTable));
    EXPECT_TRUE(ksh.IsEnabled(ShortcutAction::CloseActiveWindow));
    EXPECT_TRUE(ksh.IsEnabled(ShortcutAction::BeginLogout));
}
