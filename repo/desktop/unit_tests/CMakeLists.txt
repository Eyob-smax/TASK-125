add_executable(shelterops_unit_tests
    test_app_config.cpp
    test_booking_rules.cpp
    test_inventory_rules.cpp
    test_report_pipeline.cpp
    test_alert_rules.cpp
    test_retention_policy.cpp
    test_role_permissions.cpp

    # Security foundation tests
    test_crypto_helper.cpp
    test_credential_vault.cpp
    test_device_fingerprint.cpp
    test_rate_limiter.cpp
    test_atomic_file_writer.cpp
    test_bounded_cache.cpp
    test_cancellation_token.cpp
    test_validation.cpp
    test_error_envelope.cpp
    test_secret_safe_logger.cpp
    test_uuid.cpp
    test_database.cpp
    test_migration_runner.cpp
    test_user_repository.cpp
    test_session_repository.cpp
    test_audit_repository.cpp
    test_audit_service.cpp
    test_auth_service.cpp
    test_lockout_policy.cpp
    test_authorization_service.cpp
    test_field_masker.cpp
    test_crash_checkpoint.cpp
    test_shell_controller.cpp
    test_error_display.cpp

    # Core domain engine tests
    test_booking_search_filter.cpp
    test_booking_state_machine.cpp
    test_price_rule_engine.cpp
    test_scheduler_graph.cpp
    test_report_stage_graph.cpp
    test_kennel_repository.cpp
    test_booking_repository.cpp
    test_animal_repository.cpp
    test_inventory_repository.cpp
    test_maintenance_repository.cpp
    test_report_repository.cpp
    test_scheduler_repository.cpp
    test_admin_repository.cpp
    test_barcode_handler.cpp
    test_booking_service.cpp
    test_inventory_service.cpp
    test_alert_service.cpp
    test_maintenance_service.cpp
    test_report_service.cpp
    test_export_service.cpp
    test_scheduler_service.cpp
    test_retention_service.cpp
    test_checkpoint_service.cpp
    test_job_queue.cpp
    test_command_dispatcher.cpp

    # Desktop shell and UI controller tests
    test_table_sort_state.cpp
    test_validation_state.cpp
    test_tray_badge_state.cpp
    test_keyboard_shortcut_handler.cpp
    test_global_search_controller.cpp
    test_app_controller.cpp
    test_kennel_board_controller.cpp
    test_item_ledger_controller.cpp

    # Secondary module tests — reports, admin, audit, scheduler, update, diagnostics
    test_reports_controller.cpp
    test_audit_log_controller.cpp
    test_alerts_panel_controller.cpp
    test_scheduler_panel_controller.cpp
    test_admin_panel_controller.cpp
    test_update_manager.cpp
    test_diagnostics_controller.cpp

    # Coverage hardening — direct service tests and gap closures
    test_admin_service.cpp
    test_clipboard_helper.cpp
    test_export_service_formats.cpp
    test_retention_service_boundaries.cpp
    test_field_masker_pii.cpp
    test_command_dispatcher_surface.cpp
    test_consent_service.cpp
    test_worker_registry.cpp

    # Gap closures — native shell and view contract coverage
    test_tray_manager.cpp
    test_dockspace_shell.cpp
    test_view_render_contracts.cpp
    test_app_bootstrap.cpp
)

target_link_libraries(shelterops_unit_tests PRIVATE
    shelterops_lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(shelterops_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(shelterops_unit_tests PRIVATE cxx_std_20)

if (MSVC)
    target_compile_options(shelterops_unit_tests PRIVATE /W4 /utf-8)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(shelterops_unit_tests PRIVATE -Wall -Wextra)
endif()

include(GoogleTest)
gtest_discover_tests(shelterops_unit_tests PROPERTIES LABELS "unit")
