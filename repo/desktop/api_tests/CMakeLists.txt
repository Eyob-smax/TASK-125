# api_tests: in-process automation-surface integration tests.
#
# These tests exercise the full CommandDispatcher middleware chain
# (session auth → rate limit → RBAC → routing → FieldMasker) and
# multi-step controller/service/repository workflows — all in-process,
# without an HTTP transport layer. HTTP transport is explicitly deferred;
# see docs/api-spec.md §1 and repo/README.md for the rationale.
# When the loopback HTTP adapter is added, these tests will be extended
# to exercise the transport boundary as well.
add_executable(shelterops_api_tests
    test_automation_bootstrap.cpp
    test_automation_contracts.cpp

    # Automation surface security tests
    test_automation_auth_middleware.cpp
    test_error_envelope_contract.cpp

    # Core domain engine API tests
    test_command_kennel_search.cpp
    test_command_booking_flow.cpp
    test_command_inventory_flow.cpp
    test_command_export_throttling.cpp
    test_command_authorization.cpp

    # Primary workflow integration tests
    test_kennel_board_flow.cpp
    test_item_ledger_flow.cpp

    # Secondary module integration tests
    test_reports_studio_flow.cpp
    test_admin_approval_flow.cpp

    # Command surface inventory and multi-step report flow
    test_command_surface_inventory.cpp
    test_command_report_flow.cpp

    # Update import command surface — full flow with UpdateManager
    test_command_update_import_flow.cpp

    # HTTP status code contract coverage for all 12 registered commands
    test_command_http_status_contracts.cpp
)

target_link_libraries(shelterops_api_tests PRIVATE
    shelterops_lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(shelterops_api_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(shelterops_api_tests PRIVATE cxx_std_20)

if (MSVC)
    target_compile_options(shelterops_api_tests PRIVATE /W4 /utf-8)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(shelterops_api_tests PRIVATE -Wall -Wextra)
endif()

include(GoogleTest)
gtest_discover_tests(shelterops_api_tests PROPERTIES LABELS "api")
