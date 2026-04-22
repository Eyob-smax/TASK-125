# ShelterOps Desk Console — Requirement-to-Test Traceability

Status legend: **implemented** = test file exists and contains real assertions |
**planned** = test file path identified, not yet authored |
**deferred** = feature intentionally out of scope; boundary recorded in §19.

Migration coverage legend — SQL files that define the schema for each requirement:
`001` = core infra | `002` = facility/kennels | `003` = bookings | `004` = inventory |
`005` = maintenance | `006` = reports | `007` = admin policies | `008` = scheduler |
`009` = update management | `010` = auth lockout | `011` = audit immutability |
`012` = session absolute expiry

---

## 1. Application Bootstrap and Configuration

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Config loads from JSON, falls back to safe defaults | `src/AppConfig.cpp` | `unit_tests/test_app_config.cpp` | **implemented** |
| Config round-trips save/load without data loss | `src/AppConfig.cpp` | `unit_tests/test_app_config.cpp` | **implemented** |
| Malformed config JSON falls back to defaults | `src/AppConfig.cpp` | `unit_tests/test_app_config.cpp` | **implemented** |
| Automation endpoint disabled by default | `src/AppConfig.cpp` | `api_tests/test_automation_bootstrap.cpp` | **implemented** |
| LAN sync disabled by default; peer_host empty; peer_port and pinned_certs_path have safe defaults | `src/AppConfig.cpp` | `unit_tests/test_app_config.cpp` | **implemented** |
| Automation port in unprivileged range | `src/AppConfig.cpp` | `api_tests/test_automation_bootstrap.cpp` | **implemented** |
| Rate limit is a positive integer | `src/AppConfig.cpp` | `api_tests/test_automation_bootstrap.cpp` | **implemented** |
| All storage paths are relative (no absolute paths) | `src/AppConfig.cpp` | `unit_tests/test_app_config.cpp` | **implemented** |
| exports_dir and update_metadata_dir have correct defaults | `src/AppConfig.cpp` | `unit_tests/test_app_config.cpp` | **implemented** |
| Storage path keys round-trip save/load | `src/AppConfig.cpp` | `unit_tests/test_app_config.cpp` | **implemented** |
| Bootstrap: empty DB requires InitialAdminSetup state | `src/shell/ShellController.cpp` | `unit_tests/test_app_bootstrap.cpp` | **implemented** |
| Bootstrap: DB with users transitions to LoginRequired | `src/shell/ShellController.cpp` | `unit_tests/test_app_bootstrap.cpp` | **implemented** |
| Bootstrap: initial admin creation moves to LoginRequired | `src/shell/ShellController.cpp` | `unit_tests/test_app_bootstrap.cpp` | **implemented** |
| Bootstrap: CryptoHelper::Init is idempotent | `src/infrastructure/CryptoHelper.cpp` | `unit_tests/test_app_bootstrap.cpp` | **implemented** |
| Bootstrap: MigrationRunner idempotent on second run | `src/infrastructure/MigrationRunner.cpp` | `unit_tests/test_app_bootstrap.cpp` | **implemented** |
| Full bootstrap login sequence reaches ShellReady | `src/shell/ShellController.cpp`, `src/services/AuthService.cpp` | `unit_tests/test_app_bootstrap.cpp` | **implemented** |

---

## 2. Authentication and Role-Based Access

Migration: `001_initial_schema.sql`, `010_auth_lockout.sql`, `012_session_absolute_expiry.sql`

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Argon2id password hashing; plaintext never stored | `src/infrastructure/CryptoHelper.cpp` | `unit_tests/test_crypto_helper.cpp` | **implemented** |
| Login with correct credentials creates active session | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Login with wrong password returns failure + audit event | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Account lockout after repeated failures | `src/services/LockoutPolicy.cpp`, `src/services/AuthService.cpp` | `unit_tests/test_lockout_policy.cpp`, `unit_tests/test_auth_service.cpp` | **implemented** |
| Session expiry enforced; expired session rejected | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Session rejected after 12h absolute cap even with recent activity | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Session expiry never exceeds 12h hard cap regardless of refresh | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Role assignment: administrator, operations\_manager, inventory\_clerk, auditor | `src/domain/RolePermissions.cpp` | `unit_tests/test_role_permissions.cpp` | **implemented** |
| Auditor role receives only masked PII fields | `src/services/FieldMasker.cpp` | `unit_tests/test_field_masker.cpp` | **implemented** |
| Privilege boundary: auditor cannot write any record | `src/services/AuthorizationService.cpp` | `unit_tests/test_authorization_service.cpp` | **implemented** |
| Privilege boundary: inventory clerk cannot access admin panel | `src/services/AuthorizationService.cpp` | `unit_tests/test_authorization_service.cpp` | **implemented** |
| Login state machine: login required → shell ready | `src/shell/ShellController.cpp` | `unit_tests/test_shell_controller.cpp` | **implemented** |
| Password change persists new hash; old password rejected | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Password change requires correct old password | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Password change rejects new password shorter than 12 chars | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |

---

## 3. Multi-Window Shell and UI State

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Intake & Kennel Board window can open and close | `src/ui/controllers/AppController.cpp`, `src/shell/DockspaceShell.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| Item Ledger window can open and close | `src/ui/controllers/AppController.cpp`, `src/shell/DockspaceShell.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| Reports Studio window can open and close | `src/ui/controllers/AppController.cpp`, `src/shell/DockspaceShell.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| Admin/Audit/Alerts/Scheduler windows render when opened | `src/shell/DockspaceShell.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| Window layout state is saved on exit | `src/ui/controllers/AppController.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| Window layout is restored from checkpoint on startup | `src/infrastructure/CrashCheckpoint.cpp` | `unit_tests/test_crash_checkpoint.cpp` | **implemented** |
| Crash checkpoint stores unsaved form state (no PII) | `src/infrastructure/CrashCheckpoint.cpp` | `unit_tests/test_crash_checkpoint.cpp` | **implemented** |
| DockspaceShell: business windows gated on ShellState::ShellReady | `src/shell/DockspaceShell.cpp`, `src/shell/ShellController.cpp` | `unit_tests/test_dockspace_shell.cpp` | **implemented** |
| DockspaceShell: RoleBadge string is non-empty for all four roles | `src/shell/ShellController.cpp` | `unit_tests/test_dockspace_shell.cpp` | **implemented** |
| DockspaceShell: SessionContext role reflects authenticated user | `src/shell/SessionContext.cpp` | `unit_tests/test_dockspace_shell.cpp` | **implemented** |
| DockspaceShell: SessionContext cleared on logout | `src/shell/SessionContext.cpp`, `src/shell/ShellController.cpp` | `unit_tests/test_dockspace_shell.cpp` | **implemented** |
| View state contracts: controller initial state is Idle/empty | `src/ui/controllers/*.cpp` | `unit_tests/test_view_render_contracts.cpp` | **implemented** |
| View state contracts: empty DB produces renderable empty results | `src/ui/controllers/*.cpp` | `unit_tests/test_view_render_contracts.cpp` | **implemented** |
| View state contracts: TrayBadge zero when no alerts | `src/ui/controllers/AlertsPanelController.cpp`, `src/shell/TrayBadgeState.cpp` | `unit_tests/test_view_render_contracts.cpp` | **implemented** |
| View state contracts: Auditor cannot dismiss alerts | `src/ui/controllers/AlertsPanelController.cpp` | `unit_tests/test_view_render_contracts.cpp` | **implemented** |

---

## 4. Keyboard Shortcuts

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Ctrl+F triggers global search | `src/shell/KeyboardShortcutHandler.cpp` | `unit_tests/test_keyboard_shortcut_handler.cpp` | **implemented** |
| Ctrl+N opens new-record dialog for active window | `src/shell/KeyboardShortcutHandler.cpp` | `unit_tests/test_keyboard_shortcut_handler.cpp` | **implemented** |
| F2 enters edit mode on selected row | `src/shell/KeyboardShortcutHandler.cpp` | `unit_tests/test_keyboard_shortcut_handler.cpp` | **implemented** |
| Ctrl+Shift+E triggers export for active window | `src/shell/KeyboardShortcutHandler.cpp` | `unit_tests/test_keyboard_shortcut_handler.cpp` | **implemented** |

---

## 5. System Tray

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Tray icon shows badge count for low-stock alerts | `src/shell/TrayBadgeState.cpp` | `unit_tests/test_tray_badge_state.cpp`, `unit_tests/test_tray_manager.cpp` | **implemented** |
| Tray icon shows badge count for expiration alerts | `src/shell/TrayBadgeState.cpp` | `unit_tests/test_tray_badge_state.cpp`, `unit_tests/test_tray_manager.cpp` | **implemented** |
| Tray right-click menu opens low-stock alert list | `src/shell/TrayManager.cpp`, `src/main.cpp` | `unit_tests/test_tray_manager.cpp` | **implemented** |
| Tray right-click menu opens expiration alert list | `src/shell/TrayManager.cpp`, `src/main.cpp` | `unit_tests/test_tray_manager.cpp` | **implemented** |
| Closing main window minimizes to tray (does not exit) | `src/main.cpp`, `src/shell/TrayManager.cpp` | `unit_tests/test_tray_manager.cpp` | **implemented** |
| Badge count zero clears the tray alert variant | `src/shell/TrayBadgeState.cpp`, `src/shell/TrayManager.cpp` | `unit_tests/test_tray_manager.cpp` | **implemented** |
| Multi-category alerts sum to total badge count | `src/shell/TrayBadgeState.cpp` | `unit_tests/test_tray_manager.cpp` | **implemented** |
| Update replaces previous counts (no accumulation across frames) | `src/shell/TrayBadgeState.cpp` | `unit_tests/test_tray_manager.cpp` | **implemented** |

---

## 6. Kennel Board and Bookability

Migration: `002_facility_and_kennels.sql`, `003_animals_and_bookings.sql`

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Kennel search: filter by zone | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| Kennel search: filter by pet type and restrictions | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| Kennel search: filter by nightly price range | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| Kennel search: filter by available date range | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| Kennel search: sort by facility distance (feet) | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| DateRange overlap detection (adjacent, partial, contained) | `src/domain/BookingRules.cpp` | `unit_tests/test_booking_rules.cpp` | **implemented** |
| Bookability: capacity check rejects over-capacity booking | `src/domain/BookingRules.cpp` | `unit_tests/test_booking_rules.cpp` | **implemented** |
| Bookability: restriction conflict (no cats, no dogs, etc.) | `src/domain/BookingRules.cpp` | `unit_tests/test_booking_rules.cpp` | **implemented** |
| Bookability: inactive/adoption kennel not bookable | `src/domain/BookingRules.cpp` | `unit_tests/test_booking_rules.cpp` | **implemented** |
| Capacity=2 allows second simultaneous booking | `src/domain/BookingRules.cpp` | `unit_tests/test_booking_rules.cpp` | **implemented** |
| Ranked result list: only bookable kennels; sorted by score | `src/domain/BookingRules.cpp` | `unit_tests/test_booking_rules.cpp` | **implemented** |
| Closer kennel ranks higher (distance scoring) | `src/domain/BookingRules.cpp` | `unit_tests/test_booking_rules.cpp` | **implemented** |
| Cancelled/NoShow bookings excluded from overlap count | `src/domain/BookingRules.cpp` | `unit_tests/test_booking_rules.cpp` | **implemented** |
| Adoptable listing linked to kennel; kennel blocked for boarding | `src/repositories/KennelRepository.cpp` | `unit_tests/test_kennel_repository.cpp` | **implemented** |

---

## 7. Inventory Ledger

Migration: `004_inventory.sql`

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Serial: valid alphanumeric format accepted | `src/domain/InventoryRules.cpp` | `unit_tests/test_inventory_rules.cpp` | **implemented** |
| Serial: empty/too long/invalid char rejected | `src/domain/InventoryRules.cpp` | `unit_tests/test_inventory_rules.cpp` | **implemented** |
| Serial: globally duplicate rejected; returns owning item id | `src/domain/InventoryRules.cpp` | `unit_tests/test_inventory_rules.cpp` | **implemented** |
| Barcode: valid printable ASCII accepted | `src/domain/InventoryRules.cpp` | `unit_tests/test_inventory_rules.cpp` | **implemented** |
| Barcode: empty/non-printable/too-long rejected | `src/domain/InventoryRules.cpp` | `unit_tests/test_inventory_rules.cpp` | **implemented** |
| Average daily usage computed over 30-day window | `src/domain/InventoryRules.cpp` | `unit_tests/test_inventory_rules.cpp` | **implemented** |
| Days of stock: zero usage → infinity (never low) | `src/domain/InventoryRules.cpp` | `unit_tests/test_inventory_rules.cpp` | **implemented** |
| IsLowStock: below threshold detected | `src/domain/InventoryRules.cpp` | `unit_tests/test_inventory_rules.cpp` | **implemented** |
| IsExpiringSoon within 14 days | `src/domain/InventoryRules.cpp` | `unit_tests/test_inventory_rules.cpp` | **implemented** |
| IsExpired when past expiration date | `src/domain/InventoryRules.cpp` | `unit_tests/test_inventory_rules.cpp` | **implemented** |
| Item creation with all required fields | `src/services/InventoryService.cpp` | `unit_tests/test_inventory_service.cpp` | **implemented** |
| Inbound record with immutable timestamp | `src/repositories/InventoryRepository.cpp` | `unit_tests/test_inventory_repository.cpp` | **implemented** |
| Outbound record with immutable timestamp | `src/repositories/InventoryRepository.cpp` | `unit_tests/test_inventory_repository.cpp` | **implemented** |
| Barcode lookup returns correct item | `src/infrastructure/BarcodeHandler.cpp` | `unit_tests/test_barcode_handler.cpp` | **implemented** |
| Duplicate serial rejected + audit event written | `src/services/InventoryService.cpp` | `unit_tests/test_inventory_service.cpp` | **implemented** |
| Low-stock alert fires when quantity < threshold | `src/services/AlertService.cpp` | `unit_tests/test_alert_service.cpp` | **implemented** |
| Expiration alert fires 14 days before expiry | `src/services/AlertService.cpp` | `unit_tests/test_alert_service.cpp` | **implemented** |
| Low-stock alert not repeated when already active | `src/domain/AlertRules.cpp` | `unit_tests/test_alert_rules.cpp` | **implemented** |
| Expiration alert: both low-stock and expiring fire together | `src/domain/AlertRules.cpp` | `unit_tests/test_alert_rules.cpp` | **implemented** |
| Right-click: Issue N units reduces quantity | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |
| Right-click: Mark expired sets expiration status | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |
| Right-click: Open related outbound record | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |
| Clipboard: copy table rows as TSV | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |

---

## 8. Reporting and Scheduler

Migration: `006_reports_and_exports.sql`, `008_scheduler_and_jobs.sql`

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Occupancy rate: correct formula (0–100%) | `src/domain/ReportPipeline.cpp` | `unit_tests/test_report_pipeline.cpp` | **implemented** |
| Average occupancy across series | `src/domain/ReportPipeline.cpp` | `unit_tests/test_report_pipeline.cpp` | **implemented** |
| Kennel turnover rate formula | `src/domain/ReportPipeline.cpp` | `unit_tests/test_report_pipeline.cpp` | **implemented** |
| Maintenance response time: hours to first action (NaN if none) | `src/domain/ReportPipeline.cpp` | `unit_tests/test_report_pipeline.cpp` | **implemented** |
| Average maintenance response: NaN entries excluded; unack count | `src/domain/ReportPipeline.cpp` | `unit_tests/test_report_pipeline.cpp` | **implemented** |
| Overdue-fee distribution by age bucket (0–30,31–60,61–90,91–180,181+) | `src/domain/ReportPipeline.cpp` | `unit_tests/test_report_pipeline.cpp` | **implemented** |
| Future fees excluded from overdue distribution | `src/domain/ReportPipeline.cpp` | `unit_tests/test_report_pipeline.cpp` | **implemented** |
| Version delta: absolute and pct change per metric | `src/domain/ReportPipeline.cpp` | `unit_tests/test_report_pipeline.cpp` | **implemented** |
| Version delta: zero before-value → NaN pct | `src/domain/ReportPipeline.cpp` | `unit_tests/test_report_pipeline.cpp` | **implemented** |
| Multi-dimensional filter: date range | `src/services/ReportService.cpp` | `unit_tests/test_report_service.cpp` | **implemented** |
| Multi-dimensional filter: zone, pet type, staff (occupancy + turnover) | `src/services/ReportService.cpp` | `unit_tests/test_report_service.cpp` | **implemented** |
| Multi-dimensional filter: zone + staff on maintenance_response report | `src/services/ReportService.cpp` | `unit_tests/test_report_service.cpp` | **implemented** |
| Multi-dimensional filter: staff + pet type on overdue_fees report | `src/services/ReportService.cpp` | `unit_tests/test_report_service.cpp` | **implemented** |
| Multi-dimensional filter: pet type on inventory_summary report | `src/services/ReportService.cpp` | `unit_tests/test_report_service.cpp` | **implemented** |
| CSV export produces valid UTF-8 file | `src/services/ExportService.cpp` | `unit_tests/test_export_service.cpp`, `unit_tests/test_export_service_formats.cpp` | **implemented** |
| PDF export produces non-empty file | `src/services/ExportService.cpp` | `unit_tests/test_export_service.cpp`, `unit_tests/test_export_service_formats.cpp` | **implemented** |
| Export blocked for unauthorized role | `src/services/ExportService.cpp` | `unit_tests/test_export_service.cpp`, `unit_tests/test_export_service_formats.cpp` | **implemented** |
| Scheduled pipeline: stages execute in dependency order | `src/services/SchedulerService.cpp` | `unit_tests/test_scheduler_service.cpp` | **implemented** |
| Scheduler: failed stage surfaces in-app notification | `src/services/SchedulerService.cpp` | `unit_tests/test_scheduler_service.cpp` | **implemented** |
| Heavy export throttled to max_concurrency=1 | `src/workers/JobQueue.cpp` | `unit_tests/test_job_queue.cpp` | **implemented** |
| Circular job dependency rejected | `src/services/SchedulerService.cpp` | `unit_tests/test_scheduler_service.cpp` | **implemented** |

---

## 9. Security, Encryption, Masking, and Audit

| Requirement | Module | Test File | Status |
|---|---|---|---|
| AES-256-GCM encrypt/decrypt sensitive field round-trip | `src/infrastructure/CryptoHelper.cpp` | `unit_tests/test_crypto_helper.cpp` | **implemented** |
| DPAPI key wrapping stores and retrieves key (user-scoped, not machine-scoped) | `src/infrastructure/CredentialVault.cpp` | `unit_tests/test_credential_vault.cpp` | **implemented** |
| PII masking: phone shows last 4 digits only | `src/services/FieldMasker.cpp` | `unit_tests/test_field_masker.cpp`, `unit_tests/test_field_masker_pii.cpp` | **implemented** |
| Audit event written on login | `src/repositories/AuditRepository.cpp` | `unit_tests/test_audit_repository.cpp` | **implemented** |
| Audit event written on item issuance | `src/repositories/AuditRepository.cpp` | `unit_tests/test_audit_repository.cpp` | **implemented** |
| Audit event written on duplicate serial rejection | `src/repositories/AuditRepository.cpp` | `unit_tests/test_audit_repository.cpp` | **implemented** |
| Audit table: no UPDATE or DELETE ever issued | `src/repositories/AuditRepository.cpp` | `unit_tests/test_audit_repository.cpp` | **implemented** |
| Audit log searchable by date range and event type | `src/repositories/AuditRepository.cpp` | `unit_tests/test_audit_repository.cpp` | **implemented** |
| Audit log exportable (Auditor role, masked fields) | `src/services/ExportService.cpp` | `unit_tests/test_export_service.cpp` | **implemented** |
| Retention: anonymize PII fields after threshold | `src/services/RetentionService.cpp` | `unit_tests/test_retention_service.cpp`, `unit_tests/test_retention_service_boundaries.cpp` | **implemented** |
| Retention: delete action with anonymize fallback on delete failure | `src/services/RetentionService.cpp`, `src/repositories/BookingRepository.cpp`, `src/repositories/UserRepository.cpp`, `src/repositories/InventoryRepository.cpp`, `src/repositories/AnimalRepository.cpp` | `unit_tests/test_retention_service.cpp` | **implemented** |
| Retention: audit_events excluded from all retention operations | `src/services/RetentionService.cpp` | `unit_tests/test_retention_service.cpp`, `unit_tests/test_retention_service_boundaries.cpp` | **implemented** |
| Consent flag stored per record | `src/repositories/UserRepository.cpp` | `unit_tests/test_user_repository.cpp` | **implemented** |

---

## 10. Local Automation Endpoint

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Endpoint disabled by default | `src/AppConfig.cpp` | `api_tests/test_automation_bootstrap.cpp` | **implemented** |
| Request without token returns 401 (in-process middleware) | `src/services/AutomationAuthMiddleware.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| Request with invalid token returns 401 (in-process middleware) | `src/services/AutomationAuthMiddleware.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| Request exceeding rate limit returns 429 (in-process middleware) | `src/services/AutomationAuthMiddleware.cpp`, `src/infrastructure/RateLimiter.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| Device fingerprint mismatch returns 401 (in-process middleware) | `src/services/AutomationAuthMiddleware.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| In-process command dispatcher rejects missing/mismatched device fingerprint | `src/services/CommandDispatcher.cpp` | `unit_tests/test_command_dispatcher.cpp` | **implemented** |
| Command surface returns status envelopes for authenticated health-like checks | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_surface_inventory.cpp` | **implemented** |
| report.trigger returns run\_id and blocks inventory\_clerk | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_report_flow.cpp` | **implemented** |
| Insufficient role returns 403 | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_authorization.cpp` | **implemented** |
| update.import registered in command surface (non-404 for admin) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_surface_inventory.cpp`, `api_tests/test_command_update_import_flow.cpp` | **implemented** |
| update.import requires administrator role (403 for others) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_update_import_flow.cpp` | **implemented** |
| update.import validates msi_path field (400 when missing or empty) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_update_import_flow.cpp` | **implemented** |
| update.import translates signature failure to SIGNATURE_INVALID (400) | `src/services/CommandDispatcher.cpp`, `src/infrastructure/UpdateManager.cpp` | `api_tests/test_command_update_import_flow.cpp` | **implemented** |
| update.import is registered as 12th command in dispatch surface | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_surface_inventory.cpp` | **implemented** |
| All 12 commands return 401 for missing session (api-spec §3) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |
| All 12 commands return 401 for missing fingerprint (api-spec §3) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |
| Unauthorized response never reveals missing/expired/mismatched distinction (api-spec §6) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |
| Unknown command returns 404 NOT_FOUND (api-spec §3) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |
| Rate limit exceeded returns 429 with retry_after field (api-spec §2.3) | `src/infrastructure/RateLimiter.cpp`, `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |
| Each command returns 400 for malformed/missing request fields (api-spec §3) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |
| kennel.search with valid dates returns 200 (api-spec §2.5) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |
| alerts.list returns 200 with data envelope (api-spec §2.5.2) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |
| report.trigger with valid body returns 200 or 202 (api-spec §2.5.3) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |
| Error envelopes never contain exception/stack/trace strings (api-spec §6) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |
| Every command response carries the "ok" field (api-spec §2.4) | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_http_status_contracts.cpp` | **implemented** |

---

## 11. Update Manager

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Valid signed MSI passes verification | `src/infrastructure/UpdateManager.cpp` | `unit_tests/test_update_manager.cpp` | **implemented** |
| MSI with unknown signer thumbprint is rejected | `src/infrastructure/UpdateManager.cpp` | `unit_tests/test_update_manager.cpp` | **implemented** |
| Rollback metadata stored after successful import | `src/infrastructure/UpdateManager.cpp` | `unit_tests/test_update_manager.cpp` | **implemented** |
| Rollback triggers re-install of previous version | `src/infrastructure/UpdateManager.cpp` | `unit_tests/test_update_manager.cpp` | **implemented** |
| Version extracted from filename during import | `src/infrastructure/UpdateManager.cpp` | `unit_tests/test_update_manager.cpp`, `api_tests/test_command_update_import_flow.cpp` | **implemented** |
| UpdateManager state is SignatureFailed after unsigned import | `src/infrastructure/UpdateManager.cpp` | `api_tests/test_command_update_import_flow.cpp` | **implemented** |

---

## 12. Database and Migrations

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Migration runner applies scripts in numeric order | `src/infrastructure/MigrationRunner.cpp` | `unit_tests/test_migration_runner.cpp` | **implemented** |
| Migration runner skips already-applied scripts | `src/infrastructure/MigrationRunner.cpp` | `unit_tests/test_migration_runner.cpp` | **implemented** |
| WAL mode enabled on database open | `src/infrastructure/Database.cpp` | `unit_tests/test_database.cpp` | **implemented** |
| Foreign keys enabled on database open | `src/infrastructure/Database.cpp` | `unit_tests/test_database.cpp` | **implemented** |

---

## 13. Admin Panel

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Administrator can create/edit product catalog entries | `src/services/AdminService.cpp` | `unit_tests/test_admin_service.cpp` | **implemented** |
| Administrator can set inventory policy thresholds | `src/services/AdminService.cpp` | `unit_tests/test_admin_service.cpp` | **implemented** |
| Administrator can create/edit price rules and promos | `src/services/AdminService.cpp` | `unit_tests/test_admin_service.cpp` | **implemented** |
| Administrator can manage user roles | `src/services/AdminService.cpp` | `unit_tests/test_admin_service.cpp` | **implemented** |
| Non-administrator role cannot reach admin panel | `src/domain/RolePermissions.cpp` | `unit_tests/test_role_permissions.cpp` | **implemented** |
| After-sales adjustment creates audit event | `src/services/AdminService.cpp` | `unit_tests/test_admin_service.cpp` | **implemented** |

---

## 14. Security and Shared Infrastructure

### 14.1 Cryptographic Primitives

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Argon2id password hash/verify round-trip | `src/infrastructure/CryptoHelper.cpp` | `unit_tests/test_crypto_helper.cpp` | **implemented** |
| Wrong password rejected by verify | `src/infrastructure/CryptoHelper.cpp` | `unit_tests/test_crypto_helper.cpp` | **implemented** |
| AES-256-GCM encrypt/decrypt round-trip | `src/infrastructure/CryptoHelper.cpp` | `unit_tests/test_crypto_helper.cpp` | **implemented** |
| Tampered ciphertext fails decryption | `src/infrastructure/CryptoHelper.cpp` | `unit_tests/test_crypto_helper.cpp` | **implemented** |
| Random nonce unique across runs | `src/infrastructure/CryptoHelper.cpp` | `unit_tests/test_crypto_helper.cpp` | **implemented** |
| `GenerateRandomKey` returns 32 bytes | `src/infrastructure/CryptoHelper.cpp` | `unit_tests/test_crypto_helper.cpp` | **implemented** |

### 14.2 Credential Vault

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Store/load round-trip via in-memory vault | `src/infrastructure/CredentialVault.cpp` | `unit_tests/test_credential_vault.cpp` | **implemented** |
| Missing key returns not-found | `src/infrastructure/CredentialVault.cpp` | `unit_tests/test_credential_vault.cpp` | **implemented** |
| Overwrite replaces stored value | `src/infrastructure/CredentialVault.cpp` | `unit_tests/test_credential_vault.cpp` | **implemented** |
| Non-Windows path logs warning at startup | `src/infrastructure/CredentialVault.cpp` | `unit_tests/test_credential_vault.cpp` | **implemented** |

### 14.3 Device Fingerprinting

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Fingerprint is deterministic for same inputs | `src/infrastructure/DeviceFingerprint.cpp` | `unit_tests/test_device_fingerprint.cpp` | **implemented** |
| Fingerprint differs on different machine\_id | `src/infrastructure/DeviceFingerprint.cpp` | `unit_tests/test_device_fingerprint.cpp` | **implemented** |
| Fingerprint differs on different operator | `src/infrastructure/DeviceFingerprint.cpp` | `unit_tests/test_device_fingerprint.cpp` | **implemented** |
| Fingerprint changes when HMAC key rotates | `src/infrastructure/DeviceFingerprint.cpp` | `unit_tests/test_device_fingerprint.cpp` | **implemented** |

### 14.4 Rate Limiting

| Requirement | Module | Test File | Status |
|---|---|---|---|
| First N requests allowed (token-bucket) | `src/infrastructure/RateLimiter.cpp` | `unit_tests/test_rate_limiter.cpp` | **implemented** |
| N+1 request returns retry\_after > 0 | `src/infrastructure/RateLimiter.cpp` | `unit_tests/test_rate_limiter.cpp` | **implemented** |
| Different session tokens have independent buckets | `src/infrastructure/RateLimiter.cpp` | `unit_tests/test_rate_limiter.cpp` | **implemented** |
| Tokens refill over time (continuous refill) | `src/infrastructure/RateLimiter.cpp` | `unit_tests/test_rate_limiter.cpp` | **implemented** |

### 14.5 Crash-Safe Infrastructure

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Atomic write creates target file | `src/infrastructure/AtomicFileWriter.cpp` | `unit_tests/test_atomic_file_writer.cpp` | **implemented** |
| Interrupted write does not replace target | `src/infrastructure/AtomicFileWriter.cpp` | `unit_tests/test_atomic_file_writer.cpp` | **implemented** |
| Consecutive writes replace atomically | `src/infrastructure/AtomicFileWriter.cpp` | `unit_tests/test_atomic_file_writer.cpp` | **implemented** |
| Checkpoint save/load round-trip | `src/infrastructure/CrashCheckpoint.cpp` | `unit_tests/test_crash_checkpoint.cpp` | **implemented** |
| Checkpoint payload with `"password":` rejected | `src/infrastructure/CrashCheckpoint.cpp` | `unit_tests/test_crash_checkpoint.cpp` | **implemented** |
| Trim keeps only N most recent checkpoints | `src/infrastructure/CrashCheckpoint.cpp` | `unit_tests/test_crash_checkpoint.cpp` | **implemented** |

### 14.6 Worker Cancellation

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Cancelled token reports `IsCancelled() == true` | `include/shelterops/infrastructure/CancellationToken.h` | `unit_tests/test_cancellation_token.cpp` | **implemented** |
| `ThrowIfCancelled` throws `OperationCancelledError` | `include/shelterops/infrastructure/CancellationToken.h` | `unit_tests/test_cancellation_token.cpp` | **implemented** |
| Multiple tokens from one source share state | `include/shelterops/infrastructure/CancellationToken.h` | `unit_tests/test_cancellation_token.cpp` | **implemented** |

### 14.7 Bounded LRU Cache

| Requirement | Module | Test File | Status |
|---|---|---|---|
| LRU eviction at capacity | `include/shelterops/infrastructure/BoundedCache.h` | `unit_tests/test_bounded_cache.cpp` | **implemented** |
| TTL expiry removes stale entries | `include/shelterops/infrastructure/BoundedCache.h` | `unit_tests/test_bounded_cache.cpp` | **implemented** |
| Thread-safe concurrent access | `include/shelterops/infrastructure/BoundedCache.h` | `unit_tests/test_bounded_cache.cpp` | **implemented** |

### 14.8 Common Utilities

| Requirement | Module | Test File | Status |
|---|---|---|---|
| `ErrorCode` maps to correct HTTP status from api-spec.md §3 | `src/common/ErrorEnvelope.cpp` | `unit_tests/test_error_envelope.cpp` | **implemented** |
| Error JSON shape matches spec (ok/error/code/message) | `src/common/ErrorEnvelope.cpp` | `unit_tests/test_error_envelope.cpp` | **implemented** |
| Error JSON never contains stack traces or secret fields | `src/common/ErrorEnvelope.cpp` | `unit_tests/test_error_envelope.cpp` | **implemented** |
| `IsEmailShape` accepts valid, rejects invalid | `src/common/Validation.cpp` | `unit_tests/test_validation.cpp` | **implemented** |
| `IsE164PhoneShape` accepts valid, rejects invalid | `src/common/Validation.cpp` | `unit_tests/test_validation.cpp` | **implemented** |
| Length bound and non-empty checks | `src/common/Validation.cpp` | `unit_tests/test_validation.cpp` | **implemented** |
| SecretSafeLogger redacts password/token/api\_key fields | `src/common/SecretSafeLogger.cpp` | `unit_tests/test_secret_safe_logger.cpp` | **implemented** |
| SecretSafeLogger strips `Bearer …` from plain strings | `src/common/SecretSafeLogger.cpp` | `unit_tests/test_secret_safe_logger.cpp` | **implemented** |
| UUIDv4 generation: 36 chars, correct format, unique | `src/common/Uuid.cpp` | `unit_tests/test_uuid.cpp` | **implemented** |

### 14.9 Repository Layer

| Requirement | Module | Test File | Status |
|---|---|---|---|
| User insert/find round-trip; case-insensitive username | `src/repositories/UserRepository.cpp` | `unit_tests/test_user_repository.cpp` | **implemented** |
| Failed-login counter increments and resets | `src/repositories/UserRepository.cpp` | `unit_tests/test_user_repository.cpp` | **implemented** |
| Anonymize nulls PII and sets `anonymized_at` | `src/repositories/UserRepository.cpp` | `unit_tests/test_user_repository.cpp` | **implemented** |
| Session insert/find; expire on logout | `src/repositories/SessionRepository.cpp` | `unit_tests/test_session_repository.cpp` | **implemented** |
| `PurgeExpired` removes only expired sessions | `src/repositories/SessionRepository.cpp` | `unit_tests/test_session_repository.cpp` | **implemented** |
| Audit append succeeds; no Update/Delete method exists | `src/repositories/AuditRepository.cpp` | `unit_tests/test_audit_repository.cpp` | **implemented** |
| Audit query filters on date/actor/entity/type | `src/repositories/AuditRepository.cpp` | `unit_tests/test_audit_repository.cpp` | **implemented** |
| Audit CSV export applies masker function | `src/repositories/AuditRepository.cpp` | `unit_tests/test_audit_repository.cpp` | **implemented** |

### 14.10 Auth and Authorization Services

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Successful login issues session and audit event | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Wrong password: failure audit written, no session | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| 5 failures trigger account lock | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Locked account rejected even with correct password | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Logout invalidates session | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Expired session rejected by ValidateSession | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| `CreateInitialAdmin` fails when users table non-empty | `src/services/AuthService.cpp` | `unit_tests/test_auth_service.cpp` | **implemented** |
| Empty users table routes shell to initial administrator setup | `src/shell/ShellController.cpp` | `unit_tests/test_shell_controller.cpp` | **implemented** |
| Under threshold: no lock | `src/services/LockoutPolicy.cpp` | `unit_tests/test_lockout_policy.cpp` | **implemented** |
| Exactly at threshold: lock 15 min | `src/services/LockoutPolicy.cpp` | `unit_tests/test_lockout_policy.cpp` | **implemented** |
| Continued failures during lock: escalate to 1h | `src/services/LockoutPolicy.cpp` | `unit_tests/test_lockout_policy.cpp` | **implemented** |
| Each role vs each gated action matches design.md §5 | `src/services/AuthorizationService.cpp` | `unit_tests/test_authorization_service.cpp` | **implemented** |
| Auditor denied `CanDecryptField` for all entities | `src/services/AuthorizationService.cpp` | `unit_tests/test_authorization_service.cpp` | **implemented** |
| Inventory Clerk denied admin panel access | `src/services/AuthorizationService.cpp` | `unit_tests/test_authorization_service.cpp` | **implemented** |

### 14.11 Field Masking and Audit Service

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Phone masked to last 4 for Auditor | `src/services/FieldMasker.cpp` | `unit_tests/test_field_masker.cpp` | **implemented** |
| Email masked to domain-only for Auditor | `src/services/FieldMasker.cpp` | `unit_tests/test_field_masker.cpp` | **implemented** |
| Display name masked to initials for Auditor | `src/services/FieldMasker.cpp` | `unit_tests/test_field_masker.cpp` | **implemented** |
| Unknown (entity, field) for Auditor → Redact | `src/services/FieldMasker.cpp` | `unit_tests/test_field_masker.cpp` | **implemented** |
| Non-Auditor roles see full non-PII fields | `src/services/FieldMasker.cpp` | `unit_tests/test_field_masker.cpp` | **implemented** |
| `RecordMutation` stores diff with PII masked before write | `src/services/AuditService.cpp` | `unit_tests/test_audit_service.cpp` | **implemented** |
| CSV export honors role masking | `src/services/AuditService.cpp` | `unit_tests/test_audit_service.cpp` | **implemented** |
| No decrypted PII ever written to `audit_events` | `src/services/AuditService.cpp` | `unit_tests/test_audit_service.cpp` | **implemented** |
| `RecordSystemEvent` redacts email and phone patterns from free-text description | `src/services/AuditService.cpp` | `unit_tests/test_audit_service.cpp` | **implemented** |

### 14.12 Automation Endpoint Security Middleware

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Missing headers → UNAUTHORIZED 401 JSON envelope | `src/services/AutomationAuthMiddleware.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| Invalid fingerprint → UNAUTHORIZED 401 | `src/services/AutomationAuthMiddleware.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| Expired session token → UNAUTHORIZED 401 | `src/services/AutomationAuthMiddleware.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| Insufficient role → FORBIDDEN 403 | `src/services/AutomationAuthMiddleware.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| Rate-limit exceeded → RATE\_LIMITED 429 with retry\_after | `src/services/AutomationAuthMiddleware.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| Malformed body → INVALID\_INPUT 400 | `src/services/AutomationAuthMiddleware.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| Error envelope never contains token/password/stack | `src/services/AutomationAuthMiddleware.cpp` | `api_tests/test_automation_auth_middleware.cpp` | **implemented** |
| Every ErrorCode in code matches api-spec.md §3 | `src/common/ErrorEnvelope.cpp` | `api_tests/test_error_envelope_contract.cpp` | **implemented** |

### 14.13 Login Shell

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Initial state is `LoginRequired` | `src/shell/ShellController.cpp` | `unit_tests/test_shell_controller.cpp` | **implemented** |
| Failed login stays `LoginRequired` with last\_error set | `src/shell/ShellController.cpp` | `unit_tests/test_shell_controller.cpp` | **implemented** |
| Successful login transitions to `ShellReady` | `src/shell/ShellController.cpp` | `unit_tests/test_shell_controller.cpp` | **implemented** |
| Logout returns to `LoginRequired` | `src/shell/ShellController.cpp` | `unit_tests/test_shell_controller.cpp` | **implemented** |
| Role badge reflects authenticated role | `src/shell/ShellController.cpp` | `unit_tests/test_shell_controller.cpp` | **implemented** |
| `ErrorEnvelope` → friendly user message | `src/shell/ErrorDisplay.cpp` | `unit_tests/test_error_display.cpp` | **implemented** |
| INTERNAL error never leaks stack trace | `src/shell/ErrorDisplay.cpp` | `unit_tests/test_error_display.cpp` | **implemented** |
| UNAUTHORIZED shows "Please sign in again" | `src/shell/ErrorDisplay.cpp` | `unit_tests/test_error_display.cpp` | **implemented** |
| RATE\_LIMITED shows wait seconds | `src/shell/ErrorDisplay.cpp` | `unit_tests/test_error_display.cpp` | **implemented** |

---

## §15. Core Domain Engine

### 15.1 Kennel Search and Ranking

| Requirement | Module | Test File | Status |
|---|---|---|---|
| `ComputeQueryHash` deterministic, order-insensitive for zone_ids | `src/domain/BookingSearchFilter.cpp` | `unit_tests/test_booking_search_filter.cpp` | **implemented** |
| Different filters produce different hashes | `src/domain/BookingSearchFilter.cpp` | `unit_tests/test_booking_search_filter.cpp` | **implemented** |
| `FilterKennelsByHardConstraints` drops wrong zone | `src/domain/BookingSearchFilter.cpp` | `unit_tests/test_booking_search_filter.cpp` | **implemented** |
| Filter drops below min_rating | `src/domain/BookingSearchFilter.cpp` | `unit_tests/test_booking_search_filter.cpp` | **implemented** |
| Filter drops above max_price | `src/domain/BookingSearchFilter.cpp` | `unit_tests/test_booking_search_filter.cpp` | **implemented** |
| Filter drops non-boarding when only_bookable | `src/domain/BookingSearchFilter.cpp` | `unit_tests/test_booking_search_filter.cpp` | **implemented** |
| SearchAndRank returns ranked bookable kennels | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| SearchAndRank persists recommendation_results | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| RankedKennel carries full KennelInfo (name, zone_id, price, rating) and non-empty explainable reasons | `src/domain/BookingRules.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| kennel.search API command returns ranked list | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_kennel_search.cpp` | **implemented** |
| kennel.search API result entries include kennel_name, zone_id, nightly_cents, and non-empty reasons with code+detail | `src/services/CommandDispatcher.cpp` | `api_tests/test_command_kennel_search.cpp` | **implemented** |

### 15.2 Booking Lifecycle

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Legal transitions: Pending→Approved, Approved→Active, Active→Completed | `src/domain/BookingStateMachine.cpp` | `unit_tests/test_booking_state_machine.cpp` | **implemented** |
| Pending→Cancelled, Approved→NoShow | `src/domain/BookingStateMachine.cpp` | `unit_tests/test_booking_state_machine.cpp` | **implemented** |
| Completed/Cancelled/NoShow reject all events (ILLEGAL_TRANSITION) | `src/domain/BookingStateMachine.cpp` | `unit_tests/test_booking_state_machine.cpp` | **implemented** |
| Approve/Reject require approval role | `src/domain/BookingStateMachine.cpp` | `unit_tests/test_booking_state_machine.cpp` | **implemented** |
| CreateBooking happy path | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| Overlap → BOOKING_CONFLICT, no row inserted | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| ApproveBooking by OperationsManager succeeds | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| ApproveBooking by InventoryClerk rejected FORBIDDEN | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| Audit rows written on create | `src/services/BookingService.cpp` | `unit_tests/test_booking_service.cpp` | **implemented** |
| ListOverlapping excludes cancelled/no_show | `src/repositories/BookingRepository.cpp` | `unit_tests/test_booking_repository.cpp` | **implemented** |
| Approval round-trip | `src/repositories/BookingRepository.cpp` | `unit_tests/test_booking_repository.cpp` | **implemented** |

### 15.3 Price Rule Engine

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Fixed/percent discount and surcharge | `src/domain/PriceRuleEngine.cpp` | `unit_tests/test_price_rule_engine.cpp` | **implemented** |
| Discounts applied before surcharges | `src/domain/PriceRuleEngine.cpp` | `unit_tests/test_price_rule_engine.cpp` | **implemented** |
| Price floored at 0 | `src/domain/PriceRuleEngine.cpp` | `unit_tests/test_price_rule_engine.cpp` | **implemented** |
| Inactive rules skipped | `src/domain/PriceRuleEngine.cpp` | `unit_tests/test_price_rule_engine.cpp` | **implemented** |
| Outside validity window skipped | `src/domain/PriceRuleEngine.cpp` | `unit_tests/test_price_rule_engine.cpp` | **implemented** |

### 15.4 Inventory Ledger

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Duplicate serial → DUPLICATE_SERIAL_REJECTED, error names existing item | `src/services/InventoryService.cpp` | `unit_tests/test_inventory_service.cpp` | **implemented** |
| ReceiveStock increments qty, immutable received_at | `src/services/InventoryService.cpp` | `unit_tests/test_inventory_service.cpp` | **implemented** |
| IssueStock decrements qty, writes usage history | `src/services/InventoryService.cpp` | `unit_tests/test_inventory_service.cpp` | **implemented** |
| Issue qty > current → error | `src/services/InventoryService.cpp` | `unit_tests/test_inventory_service.cpp` | **implemented** |
| LookupByBarcode happy + not-found | `src/services/InventoryService.cpp` | `unit_tests/test_inventory_service.cpp` | **implemented** |
| DecrementQuantity below 0 throws | `src/repositories/InventoryRepository.cpp` | `unit_tests/test_inventory_repository.cpp` | **implemented** |
| InsertOutbound does not modify quantity | `src/repositories/InventoryRepository.cpp` | `unit_tests/test_inventory_repository.cpp` | **implemented** |
| UsageHistory upsert aggregates per-date | `src/repositories/InventoryRepository.cpp` | `unit_tests/test_inventory_repository.cpp` | **implemented** |
| BarcodeHandler strips CR/LF from wedge input | `src/infrastructure/BarcodeHandler.cpp` | `unit_tests/test_barcode_handler.cpp` | **implemented** |
| Non-printable input rejected | `src/infrastructure/BarcodeHandler.cpp` | `unit_tests/test_barcode_handler.cpp` | **implemented** |

### 15.5 Maintenance Response-Time

| Requirement | Module | Test File | Status |
|---|---|---|---|
| CreateTicket immutable created_at | `src/services/MaintenanceService.cpp` | `unit_tests/test_maintenance_service.cpp` | **implemented** |
| RecordEvent sets first_action_at only when NULL | `src/services/MaintenanceService.cpp` | `unit_tests/test_maintenance_service.cpp` | **implemented** |
| Second event does not overwrite first_action_at | `src/services/MaintenanceService.cpp` | `unit_tests/test_maintenance_service.cpp` | **implemented** |
| SetFirstActionAt conditional UPDATE (idempotent) | `src/repositories/MaintenanceRepository.cpp` | `unit_tests/test_maintenance_repository.cpp` | **implemented** |
| GetResponsePoints returns first_action + resolved | `src/repositories/MaintenanceRepository.cpp` | `unit_tests/test_maintenance_repository.cpp` | **implemented** |

### 15.6 Report Pipeline

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Stage sequence: collect, cleanse, analyze, visualize | `src/domain/ReportStageGraph.cpp` | `unit_tests/test_report_stage_graph.cpp` | **implemented** |
| NextStage(Visualize) returns nullopt | `src/domain/ReportStageGraph.cpp` | `unit_tests/test_report_stage_graph.cpp` | **implemented** |
| RunPipeline produces completed run | `src/services/ReportService.cpp` | `unit_tests/test_report_service.cpp` | **implemented** |
| RunPipeline inserts per-metric snapshots | `src/services/ReportService.cpp` | `unit_tests/test_report_service.cpp` | **implemented** |
| Version label format: type-YYYYMMDD-NNN | `src/services/ReportService.cpp` | `unit_tests/test_report_service.cpp` | **implemented** |

### 15.7 Throttled Exports

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Permitted role queues export | `src/services/ExportService.cpp` | `unit_tests/test_export_service.cpp` | **implemented** |
| Unauthorized role → EXPORT_UNAUTHORIZED + audit | `src/services/ExportService.cpp` | `unit_tests/test_export_service.cpp` | **implemented** |
| PDF export max_concurrency=1, CSV max_concurrency=2 | `src/services/ExportService.cpp` | `unit_tests/test_export_service.cpp` | **implemented** |
| RunExportJob writes file and sets completed | `src/services/ExportService.cpp` | `unit_tests/test_export_service.cpp` | **implemented** |
| 5 ExportPdf jobs run serially (max 1 concurrent) | `src/workers/JobQueue.cpp` | `unit_tests/test_job_queue.cpp` | **implemented** |
| stop_token causes handler to exit cleanly | `src/workers/JobQueue.cpp` | `unit_tests/test_job_queue.cpp` | **implemented** |
| Backup job writes timestamped .db file to backup_dir | `src/workers/WorkerRegistry.cpp` | `unit_tests/test_worker_registry.cpp` | **implemented** |
| Backup job fails cleanly when source db missing | `src/workers/WorkerRegistry.cpp` | `unit_tests/test_worker_registry.cpp` | **implemented** |
| LAN sync TLS: missing pinned_certs_path → failure | `src/workers/WorkerRegistry.cpp` | `unit_tests/test_worker_registry.cpp` | **implemented** |
| LAN sync TLS: untrusted server cert thumbprint → failure | `src/workers/WorkerRegistry.cpp` | `unit_tests/test_worker_registry.cpp` | **implemented** |
| LAN sync submission uses AppConfig peer_host/peer_port/pinned_certs_path; incomplete config → -1 returned | `src/services/SchedulerService.cpp` | `unit_tests/test_scheduler_service.cpp` | **planned** |
| LAN sync snapshot created from db_path before enqueue | `src/services/SchedulerService.cpp` | `unit_tests/test_scheduler_service.cpp` | **planned** |
| SchedulerPanelController::TriggerLanSync rejects non-Administrator | `src/ui/controllers/SchedulerPanelController.cpp` | `unit_tests/test_scheduler_panel_controller.cpp` | **planned** |
| Periodic LAN sync auto-submits when lan_sync_enabled and session authenticated | `src/main.cpp` | (integration, no unit test) | **planned** |

### 15.8 Scheduler

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Self-edge → CIRCULAR_JOB_DEPENDENCY | `src/services/SchedulerService.cpp` | `unit_tests/test_scheduler_service.cpp` | **implemented** |
| A→B + B→A → CIRCULAR_JOB_DEPENDENCY | `src/services/SchedulerService.cpp` | `unit_tests/test_scheduler_service.cpp` | **implemented** |
| EnqueueOnDemand inserts job + queued run | `src/services/SchedulerService.cpp` | `unit_tests/test_scheduler_service.cpp` | **implemented** |
| HasCircularDependency acyclic → false | `src/domain/SchedulerGraph.cpp` | `unit_tests/test_scheduler_graph.cpp` | **implemented** |
| HasCircularDependency cycle → true | `src/domain/SchedulerGraph.cpp` | `unit_tests/test_scheduler_graph.cpp` | **implemented** |
| NextReadyJobs only jobs with prereqs completed | `src/domain/SchedulerGraph.cpp` | `unit_tests/test_scheduler_graph.cpp` | **implemented** |

### 15.9 Retention

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Old animal anonymized per policy | `src/services/RetentionService.cpp` | `unit_tests/test_retention_service.cpp` | **implemented** |
| Recent animal not anonymized | `src/services/RetentionService.cpp` | `unit_tests/test_retention_service.cpp` | **implemented** |
| audit_events never touched | `src/services/RetentionService.cpp` | `unit_tests/test_retention_service.cpp` | **implemented** |

### 15.10 Checkpoint

| Requirement | Module | Test File | Status |
|---|---|---|---|
| CaptureState and RestoreState round-trip | `src/services/CheckpointService.cpp` | `unit_tests/test_checkpoint_service.cpp` | **implemented** |
| PII in draft_text rejected | `src/services/CheckpointService.cpp` | `unit_tests/test_checkpoint_service.cpp` | **implemented** |
| Empty state restores nullopt | `src/services/CheckpointService.cpp` | `unit_tests/test_checkpoint_service.cpp` | **implemented** |

### 15.11 Command Dispatcher

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Unknown command → NOT_FOUND 404 | `src/services/CommandDispatcher.cpp` | `unit_tests/test_command_dispatcher.cpp` | **implemented** |
| Missing/expired session → UNAUTHORIZED 401 | `src/services/CommandDispatcher.cpp` | `unit_tests/test_command_dispatcher.cpp` | **implemented** |
| Rate-limit exceeded → 429 + retry_after | `src/services/CommandDispatcher.cpp` | `unit_tests/test_command_dispatcher.cpp` | **implemented** |
| kennel.search happy path returns 200 | `src/services/CommandDispatcher.cpp` | `unit_tests/test_command_dispatcher.cpp` | **implemented** |
| Inventory Clerk cannot trigger reports — report.trigger returns 403 | `src/services/CommandDispatcher.cpp` | `unit_tests/test_command_dispatcher_surface.cpp` | **implemented** |

---

## §16. Desktop Shell and Operator Workflows

### 16.1 Shared UI Primitives

| Requirement | Module | Test File | Status |
|---|---|---|---|
| TableSortState default unsorted | `src/ui/primitives/TableSortState.cpp` | `unit_tests/test_table_sort_state.cpp` | **implemented** |
| SetSort / ToggleSort same column | `src/ui/primitives/TableSortState.cpp` | `unit_tests/test_table_sort_state.cpp` | **implemented** |
| ToggleSort different column resets to ascending | `src/ui/primitives/TableSortState.cpp` | `unit_tests/test_table_sort_state.cpp` | **implemented** |
| ComputeIndices no filter no sort | `src/ui/primitives/TableSortState.cpp` | `unit_tests/test_table_sort_state.cpp` | **implemented** |
| ComputeIndices filter removes rows | `src/ui/primitives/TableSortState.cpp` | `unit_tests/test_table_sort_state.cpp` | **implemented** |
| ComputeIndices sort ascending / descending | `src/ui/primitives/TableSortState.cpp` | `unit_tests/test_table_sort_state.cpp` | **implemented** |
| FormatTsv header + data rows | `src/ui/primitives/TableSortState.cpp` | `unit_tests/test_table_sort_state.cpp` | **implemented** |
| FormatTsv empty rows | `src/ui/primitives/TableSortState.cpp` | `unit_tests/test_table_sort_state.cpp` | **implemented** |
| ValidationState default no errors | `src/ui/primitives/ValidationState.cpp` | `unit_tests/test_validation_state.cpp` | **implemented** |
| SetError / HasErrors / GetError | `src/ui/primitives/ValidationState.cpp` | `unit_tests/test_validation_state.cpp` | **implemented** |
| ClearField removes individual error | `src/ui/primitives/ValidationState.cpp` | `unit_tests/test_validation_state.cpp` | **implemented** |
| Clear removes all errors | `src/ui/primitives/ValidationState.cpp` | `unit_tests/test_validation_state.cpp` | **implemented** |
| AllErrors joins with semicolons | `src/ui/primitives/ValidationState.cpp` | `unit_tests/test_validation_state.cpp` | **implemented** |

### 16.2 Tray Badge State

| Requirement | Module | Test File | Status |
|---|---|---|---|
| TrayBadgeState default zero counts | `src/shell/TrayBadgeState.cpp` | `unit_tests/test_tray_badge_state.cpp` | **implemented** |
| Update counts low_stock alerts | `src/shell/TrayBadgeState.cpp` | `unit_tests/test_tray_badge_state.cpp` | **implemented** |
| Acknowledged alerts not counted | `src/shell/TrayBadgeState.cpp` | `unit_tests/test_tray_badge_state.cpp` | **implemented** |
| Mixed alert types counted separately | `src/shell/TrayBadgeState.cpp` | `unit_tests/test_tray_badge_state.cpp` | **implemented** |
| Empty list resets counts to zero | `src/shell/TrayBadgeState.cpp` | `unit_tests/test_tray_badge_state.cpp` | **implemented** |

### 16.3 Keyboard Shortcut Handler

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Ctrl+F → BeginGlobalSearch | `src/shell/KeyboardShortcutHandler.cpp` | `unit_tests/test_keyboard_shortcut_handler.cpp` | **implemented** |
| Ctrl+N → NewRecord (non-Auditor) | `src/shell/KeyboardShortcutHandler.cpp` | `unit_tests/test_keyboard_shortcut_handler.cpp` | **implemented** |
| Auditor cannot NewRecord / EditRecord / ExportTable | `src/shell/KeyboardShortcutHandler.cpp` | `unit_tests/test_keyboard_shortcut_handler.cpp` | **implemented** |
| No active window disables CloseWindow | `src/shell/KeyboardShortcutHandler.cpp` | `unit_tests/test_keyboard_shortcut_handler.cpp` | **implemented** |
| Ctrl+Shift+L → BeginLogout (all roles) | `src/shell/KeyboardShortcutHandler.cpp` | `unit_tests/test_keyboard_shortcut_handler.cpp` | **implemented** |
| Case-sensitive VK mapping (no false fires) | `src/shell/KeyboardShortcutHandler.cpp` | `unit_tests/test_keyboard_shortcut_handler.cpp` | **implemented** |

### 16.4 Global Search Controller

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Empty query returns no results | `src/ui/controllers/GlobalSearchController.cpp` | `unit_tests/test_global_search_controller.cpp` | **implemented** |
| Kennel name match returns result with KennelBoard target | `src/ui/controllers/GlobalSearchController.cpp` | `unit_tests/test_global_search_controller.cpp` | **implemented** |
| Non-matching kennel excluded | `src/ui/controllers/GlobalSearchController.cpp` | `unit_tests/test_global_search_controller.cpp` | **implemented** |
| Booking audit event match | `src/ui/controllers/GlobalSearchController.cpp` | `unit_tests/test_global_search_controller.cpp` | **implemented** |
| Auditor booking result is masked | `src/ui/controllers/GlobalSearchController.cpp` | `unit_tests/test_global_search_controller.cpp` | **implemented** |
| Inventory low-stock item match | `src/ui/controllers/GlobalSearchController.cpp` | `unit_tests/test_global_search_controller.cpp` | **implemented** |
| Report audit event match | `src/ui/controllers/GlobalSearchController.cpp` | `unit_tests/test_global_search_controller.cpp` | **implemented** |
| Case-insensitive search | `src/ui/controllers/GlobalSearchController.cpp` | `unit_tests/test_global_search_controller.cpp` | **implemented** |

### 16.5 App Controller

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Open/close/is-open window management | `src/ui/controllers/AppController.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| SetActiveWindow / GetActiveWindow | `src/ui/controllers/AppController.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| Multiple windows open simultaneously | `src/ui/controllers/AppController.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| ProcessKeyEvent Ctrl+F → BeginGlobalSearch | `src/ui/controllers/AppController.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| ProcessKeyEvent Ctrl+Shift+L → BeginLogout | `src/ui/controllers/AppController.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| Cross-window refresh flag set/clear | `src/ui/controllers/AppController.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |
| Checkpoint capture + restore round-trip | `src/ui/controllers/AppController.cpp` | `unit_tests/test_app_controller.cpp` | **implemented** |

### 16.6 Kennel Board Controller (Unit)

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Initial state is Idle | `src/ui/controllers/KennelBoardController.cpp` | `unit_tests/test_kennel_board_controller.cpp` | **implemented** |
| Refresh transitions to Loaded, populates results | `src/ui/controllers/KennelBoardController.cpp` | `unit_tests/test_kennel_board_controller.cpp` | **implemented** |
| BeginCreateBooking sets CreatingBooking state | `src/ui/controllers/KennelBoardController.cpp` | `unit_tests/test_kennel_board_controller.cpp` | **implemented** |
| SubmitBooking happy path → BookingSuccess | `src/ui/controllers/KennelBoardController.cpp` | `unit_tests/test_kennel_board_controller.cpp` | **implemented** |
| SubmitBooking overlap → BookingConflict | `src/ui/controllers/KennelBoardController.cpp` | `unit_tests/test_kennel_board_controller.cpp` | **implemented** |
| SubmitBooking missing name → validation error | `src/ui/controllers/KennelBoardController.cpp` | `unit_tests/test_kennel_board_controller.cpp` | **implemented** |
| ClipboardTsv contains header row | `src/ui/controllers/KennelBoardController.cpp` | `unit_tests/test_kennel_board_controller.cpp` | **implemented** |
| IsDirty after refresh; ClearDirty resets | `src/ui/controllers/KennelBoardController.cpp` | `unit_tests/test_kennel_board_controller.cpp` | **implemented** |

### 16.7 Item Ledger Controller (Unit)

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Initial state is Idle | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |
| Refresh transitions to Loaded | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |
| SubmitAddItem happy path | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |
| SubmitAddItem duplicate serial → DuplicateSerial state | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |
| SubmitReceiveStock increases quantity | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |
| SubmitIssueStock decreases quantity | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |
| FilterShowLowStockOnly — only low-stock items shown | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |
| ClipboardTsv contains header row | `src/ui/controllers/ItemLedgerController.cpp` | `unit_tests/test_item_ledger_controller.cpp` | **implemented** |

### 16.8 Kennel Board Flow (API / Integration)

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Full search → book flow via controller | `KennelBoardController` + `BookingService` | `api_tests/test_kennel_board_flow.cpp` | **implemented** |
| Conflict on double-book same kennel | `KennelBoardController` + `BookingService` | `api_tests/test_kennel_board_flow.cpp` | **implemented** |
| Capacity=2 kennel accepts two concurrent bookings | `KennelBoardController` + `BookingService` | `api_tests/test_kennel_board_flow.cpp` | **implemented** |
| OperationsManager can approve booking | `KennelBoardController` + `BookingService` | `api_tests/test_kennel_board_flow.cpp` | **implemented** |
| Recommendation results persisted after search | `KennelBoardController` + `BookingService` | `api_tests/test_kennel_board_flow.cpp` | **implemented** |
| CancelBooking transitions booking to cancelled | `KennelBoardController` + `BookingService` | `api_tests/test_kennel_board_flow.cpp` | **implemented** |

### 16.9 Item Ledger Flow (API / Integration)

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Add → receive → issue full cycle | `ItemLedgerController` + `InventoryService` | `api_tests/test_item_ledger_flow.cpp` | **implemented** |
| inbound received_at is immutable | `InventoryRepository` | `api_tests/test_item_ledger_flow.cpp` | **implemented** |
| Duplicate serial rejected with audit event | `ItemLedgerController` + `InventoryService` | `api_tests/test_item_ledger_flow.cpp` | **implemented** |
| Barcode input locates item (BarcodeFound state) | `ItemLedgerController` + `BarcodeHandler` | `api_tests/test_item_ledger_flow.cpp` | **implemented** |
| Barcode not found → BarcodeNotFound state | `ItemLedgerController` | `api_tests/test_item_ledger_flow.cpp` | **implemented** |
| Issue beyond quantity fails, qty unchanged | `ItemLedgerController` + `InventoryService` | `api_tests/test_item_ledger_flow.cpp` | **implemented** |
| Usage history updated after issue | `ItemLedgerController` + `InventoryRepository` | `api_tests/test_item_ledger_flow.cpp` | **implemented** |


---

## 17 Secondary Operational Modules

### 17.1 Reports Studio Controller

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Initial state is Idle | ReportsController | unit_tests/test_reports_controller.cpp | **implemented** |
| TriggerReport returns run_id | ReportsController + ReportService | unit_tests/test_reports_controller.cpp | **implemented** |
| LoadRunsForReport populates run list | ReportsController + ReportRepository | unit_tests/test_reports_controller.cpp | **implemented** |
| RefreshRunStatus updates cached status | ReportsController + ReportRepository | unit_tests/test_reports_controller.cpp | **implemented** |
| RequestExport unauthorized returns 0 | ReportsController + ExportService | unit_tests/test_reports_controller.cpp | **implemented** |
| IsDirty after trigger | ReportsController | unit_tests/test_reports_controller.cpp | **implemented** |
| GenerateVersionLabel format correct | ReportService | unit_tests/test_reports_controller.cpp | **implemented** |

### 17.2 Audit Log Controller

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Initial state is Idle | AuditLogController | unit_tests/test_audit_log_controller.cpp | **implemented** |
| Manager and Auditor can refresh log | AuditLogController + AuditRepository | unit_tests/test_audit_log_controller.cpp | **implemented** |
| Filter by event_type narrows results | AuditLogController + AuditRepository | unit_tests/test_audit_log_controller.cpp | **implemented** |
| Manager export CSV returns content with header | AuditLogController | unit_tests/test_audit_log_controller.cpp | **implemented** |
| Auditor export CSV is masked | AuditLogController + AuditRepository::ExportCsv | unit_tests/test_audit_log_controller.cpp | **implemented** |
| InventoryClerk cannot export audit log | AuditLogController + AuthorizationService | unit_tests/test_audit_log_controller.cpp | **implemented** |
| Limit respected on query | AuditLogController + AuditRepository | unit_tests/test_audit_log_controller.cpp | **implemented** |

### 17.3 Alerts Panel Controller

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Initial state is Idle | AlertsPanelController | unit_tests/test_alerts_panel_controller.cpp | **implemented** |
| Low-stock item triggers alert | AlertsPanelController + AlertService | unit_tests/test_alerts_panel_controller.cpp | **implemented** |
| Expiring item triggers alert | AlertsPanelController + AlertService | unit_tests/test_alerts_panel_controller.cpp | **implemented** |
| Manager can acknowledge alert | AlertsPanelController + AlertService | unit_tests/test_alerts_panel_controller.cpp | **implemented** |
| Auditor cannot acknowledge alert | AlertsPanelController | unit_tests/test_alerts_panel_controller.cpp | **implemented** |
| Tray badge updated after refresh | AlertsPanelController + TrayBadgeState | unit_tests/test_alerts_panel_controller.cpp | **implemented** |

### 17.4 Scheduler Panel Controller

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Initial state is Idle | SchedulerPanelController | unit_tests/test_scheduler_panel_controller.cpp | **implemented** |
| Refresh loads active jobs | SchedulerPanelController + SchedulerRepository | unit_tests/test_scheduler_panel_controller.cpp | **implemented** |
| ViewJobDetail populates detail view | SchedulerPanelController | unit_tests/test_scheduler_panel_controller.cpp | **implemented** |
| Detail includes pipeline stage string | SchedulerPanelController + ReportStageGraph | unit_tests/test_scheduler_panel_controller.cpp | **implemented** |
| Manager can enqueue on-demand job | SchedulerPanelController + SchedulerService | unit_tests/test_scheduler_panel_controller.cpp | **implemented** |
| Auditor cannot enqueue job | SchedulerPanelController | unit_tests/test_scheduler_panel_controller.cpp | **implemented** |
| Job detail includes dependency edges | SchedulerPanelController + SchedulerRepository | unit_tests/test_scheduler_panel_controller.cpp | **implemented** |

### 17.5 Admin Panel Controller

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Admin can update catalog entry | AdminPanelController + AdminService | unit_tests/test_admin_panel_controller.cpp | **implemented** |
| Clerk cannot update catalog entry | AdminPanelController + AuthorizationService | unit_tests/test_admin_panel_controller.cpp | **implemented** |
| Admin can create price rule | AdminPanelController + AdminService | unit_tests/test_admin_panel_controller.cpp | **implemented** |
| Admin can set retention policy | AdminPanelController + AdminService | unit_tests/test_admin_panel_controller.cpp | **implemented** |
| Admin can set export permission | AdminPanelController + AdminService | unit_tests/test_admin_panel_controller.cpp | **implemented** |

### 17.6 Update Manager

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Import non-existent path fails | UpdateManager | unit_tests/test_update_manager.cpp | **implemented** |
| Import non-.msi extension fails | UpdateManager | unit_tests/test_update_manager.cpp | **implemented** |
| Signature check fails on unsigned test file | UpdateManager::DoVerify | unit_tests/test_update_manager.cpp | **implemented** |
| Apply without verified package fails | UpdateManager | unit_tests/test_update_manager.cpp | **implemented** |
| Rollback metadata persisted and reloaded | UpdateManager::SaveRollbackMetadata | unit_tests/test_update_manager.cpp | **implemented** |
| Rollback without metadata fails | UpdateManager | unit_tests/test_update_manager.cpp | **implemented** |
| Version inferred from filename (`ShelterOpsDesk-N.N.N.msi`) | UpdateManager::InferVersionFromPath | unit_tests/test_update_manager.cpp | **implemented** |
| Rollback available after Apply with version extracted from filename | UpdateManager | unit_tests/test_update_manager.cpp | **implemented** |
| Corrupt rollback.json does not crash on construction | UpdateManager | unit_tests/test_update_manager.cpp | **implemented** |
| Rollback metadata stores previous_msi_path, not exe path | UpdateManager::SaveRollbackMetadata | unit_tests/test_update_manager.cpp | **implemented** |
| Reset clears in-memory rollback state; rollback.json preserved on disk | UpdateManager | unit_tests/test_update_manager.cpp | **implemented** |
| NormalizeThumbprint removes separators and uppercases | UpdateManager::NormalizeThumbprint | unit_tests/test_update_manager.cpp | **implemented** |
| LoadPinnedThumbprints parses trusted_publishers array (deduplicates) | UpdateManager::LoadPinnedThumbprints | unit_tests/test_update_manager.cpp | **implemented** |
| IsThumbprintPinned uses normalized comparison | UpdateManager::IsThumbprintPinned | unit_tests/test_update_manager.cpp | **implemented** |

### 17.7 Diagnostics Controller

| Requirement | Module | Test File | Status |
|---|---|---|---|
| DB stats page_size > 0 | DiagnosticsController + Database | unit_tests/test_diagnostics_controller.cpp | **implemented** |
| Registered cache appears in stats | DiagnosticsController | unit_tests/test_diagnostics_controller.cpp | **implemented** |
| Refresh updates cache stats dynamically | DiagnosticsController | unit_tests/test_diagnostics_controller.cpp | **implemented** |
| Worker idle when queue not started | DiagnosticsController + JobQueue | unit_tests/test_diagnostics_controller.cpp | **implemented** |

### 17.8 Reports Studio Flow (API Integration)

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Trigger occupancy report produces run | ReportsController + ReportService | api_tests/test_reports_studio_flow.cpp | **implemented** |
| Version label unique per run | ReportService::GenerateVersionLabel | api_tests/test_reports_studio_flow.cpp | **implemented** |
| CSV export permitted for Manager | ExportService + AdminRepository | api_tests/test_reports_studio_flow.cpp | **implemented** |
| PDF export unauthorized for Auditor | ExportService + AuthorizationService | api_tests/test_reports_studio_flow.cpp | **implemented** |
| Version comparison returns delta list | ReportService::CompareVersions | api_tests/test_reports_studio_flow.cpp | **implemented** |

### 17.9 Admin Approval Flow (API Integration)

| Requirement | Module | Test File | Status |
|---|---|---|---|
| Manager can approve booking via controller | AdminPanelController + BookingService | api_tests/test_admin_approval_flow.cpp | **implemented** |
| Clerk cannot approve booking | AdminPanelController + AuthorizationService | api_tests/test_admin_approval_flow.cpp | **implemented** |
| Approve writes BOOKING_APPROVED audit event | BookingService + AuditService | api_tests/test_admin_approval_flow.cpp | **implemented** |
| Retention policy set and persisted | AdminPanelController + AdminService | api_tests/test_admin_approval_flow.cpp | **implemented** |
| Clerk forbidden from after-sales adjustment | BookingService + AuthorizationService | api_tests/test_admin_approval_flow.cpp | **implemented** |

---

## §18. Automation Surface Test Inventory

Every file under `repo/desktop/api_tests/` is classified below by transport type.
**No test in this suite uses HTTP transport.** All tests exercise the in-process
`CommandDispatcher` or cross-platform controller/service layer directly.

| File | Classification | Commands / Contracts Covered |
|---|---|---|
| `test_automation_bootstrap.cpp` | config/bootstrap (no HTTP) | `automation_endpoint_enabled=false`, `lan_sync_enabled=false`, port range, rate_limit |
| `test_automation_contracts.cpp` | in-process command contract behavior (no HTTP) | unauthorized session/fingerprint → 401, unknown command → 404, rate-limit → 429 + retry_after, config defaults |
| `test_automation_auth_middleware.cpp` | in-process middleware (no HTTP) | missing token → 401, bad fingerprint → 401, expired session → 401, wrong role → 403, rate-limit → 429, malformed body → 400 |
| `test_error_envelope_contract.cpp` | error-code contract (no HTTP) | every `ErrorCode` value maps to expected HTTP status per api-spec.md §3 |
| `test_command_kennel_search.cpp` | in-process command (no HTTP) | `kennel.search` happy path; every result entry asserts kennel_name, zone_id, nightly_cents, non-empty reasons with code+detail; invalid filter no crash |
| `test_command_booking_flow.cpp` | in-process command (no HTTP) | `booking.create`, `booking.approve`, `booking.cancel` lifecycle |
| `test_command_inventory_flow.cpp` | in-process command (no HTTP) | `inventory.issue`, `inventory.receive` |
| `test_command_export_throttling.cpp` | in-process command (no HTTP) | `export.request` concurrency cap, `RATE_LIMITED` on second PDF request |
| `test_command_authorization.cpp` | in-process command (no HTTP) | role matrix per command — 403 on wrong role, 401 on missing session |
| `test_kennel_board_flow.cpp` | cross-platform integration (no HTTP) | search → create → approve → cancel full cycle via `KennelBoardController` |
| `test_item_ledger_flow.cpp` | cross-platform integration (no HTTP) | add → receive → issue → barcode scan full cycle via `ItemLedgerController` |
| `test_reports_studio_flow.cpp` | cross-platform integration (no HTTP) | trigger → status → export → version-compare via `ReportsController` |
| `test_admin_approval_flow.cpp` | cross-platform integration (no HTTP) | booking approve, retention policy set, clerk auth boundaries |
| `test_command_surface_inventory.cpp` | in-process command inventory (no HTTP) | all 11 registered commands — name non-empty, `ErrorEnvelope` on wrong role, valid JSON response shape |
| `test_command_report_flow.cpp` | in-process command sequence (no HTTP) | `report.trigger` → `report.status` → `export.request` sequence; Auditor/Clerk trigger → 403; auditor export rejection; PDF concurrency |

**Regression guard:** `test_command_surface_inventory.cpp::CommandInventoryHasElevenEntries`
asserts `kCommandTable.size() == 11u`. Adding a new command without updating this table
causes a compile-time or assertion failure.

**Role lookup:** As of the security audit, `CommandDispatcher::Dispatch()` now looks up the
real user role from `UserRepository::FindById(session->user_id)` rather than hardcoding
`InventoryClerk`. All `api_tests/` files that construct `CommandDispatcher` now pass a
`UserRepository&` argument. The role matrix assertions in `test_command_authorization.cpp`
and `test_command_dispatcher_surface.cpp` exercise real role enforcement.

---

## §19. Coverage Boundary Declaration

The following production modules are **intentionally not tested** in the current test suite.
Each deferral is explained by a concrete technical constraint, not omission.

### 19.1 Dockspace Window Composition (Win32)
**Module:** `src/shell/DockspaceShell.cpp`  
**Deferred because:** Dear ImGui render calls require a live Win32/DX11 UI context in order
to validate actual pixel rendering and docking behavior.  
**Boundary:** Window open/close routing and logical state are tested via `AppController`
(`test_app_controller.cpp`), while shell render behavior is statically verified in source.

### 19.2 `TrayManager` (Win32)
**Module:** `src/shell/TrayManager.cpp`  
**Deferred because:** `TrayManager` calls `Shell_NotifyIconW`, `LoadIconW`, and
`CreatePopupMenu` — all require a Win32 message loop on a physical or virtual desktop.  
**Boundary:** Alert badge counts (the cross-platform state driving the tray tooltip) are
fully tested via `TrayBadgeState` (`test_tray_badge_state.cpp` — §16.2 rows).

### 19.3 Deferred Loopback HTTP Transport
**Module:** future HTTP adapter over `CommandDispatcher`  
**Deferred because:** The HTTP listen loop (`httplib::Server::listen`) requires a live TCP
socket; binding and request/response round-trips cannot run without a network stack in the
test process.  
**Boundary:** The entire security middleware chain (session verify, fingerprint check, rate
limiting, role gate, error envelope formatting) is tested in-process via
`AutomationAuthMiddleware` (`test_automation_auth_middleware.cpp` — §14.12 rows).
The `CommandDispatcher` in-process contract is fully tested via `api_tests/`.
When the HTTP adapter is added, route tests will simply verify that it forwards headers
correctly to the already-tested dispatcher.

### 19.4 ImPlot Chart Rendering
**Module:** `src/ui/views/ReportsView.cpp` (chart panels)  
**Deferred because:** `ImPlot::PlotLine`, `ImPlot::PlotBars` etc. require an active ImGui
draw-list context backed by a GPU render target. No software rasterizer is linked in CI.  
**Boundary:** The data pipelines feeding the charts (report snapshots, metric computations)
are fully tested in `test_report_service.cpp`, `test_report_pipeline.cpp`.

### 19.5 WiX Signing and Installer Verification
**Module:** `repo/desktop/packaging/product.wxs`, `WinVerifyTrust` in `UpdateManager`  
**Deferred because:** Authenticode signing requires a code-signing certificate and runs only
in the Windows packaging pipeline. `WinVerifyTrust` always returns an error on non-Windows
hosts (tested as the expected failure path in `test_update_manager.cpp`).  
**Boundary:** UpdateManager import validation, rollback metadata persistence, and Apply
guard logic are tested on Linux CI (with the expected Win32 failure behavior asserted).

---

## §20. Per-Test-File Assertion Focus Index

One row per test file. The "Key Assertions" column names 3–6 highest-value assertions
a reviewer should verify first. All files are under `repo/desktop/`.

### Unit Tests (`unit_tests/`)

| File | Key Assertions |
|---|---|
| `test_app_config.cpp` | defaults on missing file; round-trip save/load; malformed JSON falls back; storage path defaults relative; exports_dir/update_metadata_dir round-trip; automation port/rpm defaults |
| `test_booking_rules.cpp` | overlap detection edge cases; capacity=2 dual booking; restriction conflict |
| `test_inventory_rules.cpp` | duplicate serial returns owning id; days-of-stock zero-usage; IsExpiringSoon 14-day |
| `test_report_pipeline.cpp` | occupancy formula; maintenance NaN exclusion; overdue bucket boundaries |
| `test_alert_rules.cpp` | low-stock not repeated when active; both alert types fire together |
| `test_retention_policy.cpp` | anonymize vs delete decision; threshold boundary; consent-withdrawn included |
| `test_role_permissions.cpp` | each role vs each gated action; Auditor cannot write; Clerk denied admin |
| `test_crypto_helper.cpp` | Argon2id wrong-password rejected; AES-GCM tampered ciphertext fails; nonce unique |
| `test_credential_vault.cpp` | store/load round-trip; missing key not-found; overwrite replaces |
| `test_device_fingerprint.cpp` | deterministic; differs on machine_id change; HMAC key rotation changes result |
| `test_rate_limiter.cpp` | N+1 returns retry_after; independent buckets per session; token refill |
| `test_atomic_file_writer.cpp` | interrupted write does not replace target; consecutive writes atomic |
| `test_bounded_cache.cpp` | LRU eviction at capacity; TTL expiry; concurrent access safe |
| `test_cancellation_token.cpp` | cancelled → IsCancelled true; ThrowIfCancelled throws; shared state |
| `test_validation.cpp` | email/phone valid/invalid; length bounds; non-empty check |
| `test_error_envelope.cpp` | JSON shape matches spec; no stack trace; code → HTTP status |
| `test_secret_safe_logger.cpp` | redacts password/token/api_key; strips Bearer token |
| `test_uuid.cpp` | 36 chars; correct format; unique across calls |
| `test_database.cpp` | WAL mode enabled; foreign keys enabled |
| `test_migration_runner.cpp` | numeric order; skips already-applied; idempotent |
| `test_user_repository.cpp` | case-insensitive lookup; failed-login counter; anonymize nulls PII |
| `test_session_repository.cpp` | insert/find; expire on logout; PurgeExpired leaves active |
| `test_audit_repository.cpp` | Append only — no Update/Delete method; date-range query filters; CSV masker |
| `test_audit_service.cpp` | RecordMutation masks PII before write; CSV honors role; no plaintext PII; RecordSystemEvent redacts email and phone from free-text |
| `test_auth_service.cpp` | successful login issues session + audit; 5 failures lock account; logout invalidates; ChangePassword persists new hash; old password rejected after change; wrong old password → INVALID_CREDENTIALS; short new password → INVALID_INPUT; session rejected at 12h+1s absolute cap; repeated refresh never extends beyond 12h |
| `test_lockout_policy.cpp` | under threshold no lock; exactly at threshold 15 min; escalation to 1h |
| `test_authorization_service.cpp` | each role vs each action matrix; Auditor denied CanDecryptField |
| `test_field_masker.cpp` | phone last-4; email domain-only; initials; unknown → Redact |
| `test_field_masker_pii.cpp` | phone → `***-***-1234`; email domain preserved; initials format; Auditor masks actor_user_id |
| `test_crash_checkpoint.cpp` | save/load round-trip; `"password":` rejected; `"token":` rejected; email pattern rejected |
| `test_shell_controller.cpp` | initial → LoginRequired; failed login stays LoginRequired; success → ShellReady; logout resets |
| `test_error_display.cpp` | INTERNAL never leaks stack; UNAUTHORIZED → sign-in message; RATE_LIMITED shows seconds |
| `test_booking_search_filter.cpp` | hash deterministic; wrong-zone dropped; max-price dropped; only_bookable filter |
| `test_booking_state_machine.cpp` | legal transitions; terminal states reject all events; approval role gate |
| `test_price_rule_engine.cpp` | discounts before surcharges; floor at 0; inactive skipped; validity window |
| `test_booking_service.cpp` | SearchAndRank persists recommendation_results; overlap → BOOKING_CONFLICT; approval role gate; RankedKennel carries full KennelInfo (name/zone/price/rating) and non-empty reasons with code+detail; contact fields encrypted before persistence |
| `test_kennel_repository.cpp` | zone filter; adoptable blocks boarding; distance sort |
| `test_booking_repository.cpp` | ListOverlapping excludes cancelled/no_show; approval round-trip |
| `test_inventory_service.cpp` | duplicate serial error names existing id; ReceiveStock immutable received_at; IssueStock guards qty |
| `test_inventory_repository.cpp` | DecrementQuantity below 0 throws; InsertOutbound immutable; UsageHistory aggregates |
| `test_barcode_handler.cpp` | strips CR/LF; non-printable rejected |
| `test_alert_service.cpp` | low-stock alert fires; expiration alert fires; not repeated when active |
| `test_maintenance_service.cpp` | CreateTicket immutable created_at; RecordEvent sets first_action_at only when NULL |
| `test_maintenance_repository.cpp` | SetFirstActionAt conditional (idempotent); GetResponsePoints returns both |
| `test_report_service.cpp` | RunPipeline completed run; per-metric snapshots; version label format; zone/staff/pet-type filters across occupancy, turnover, maintenance_response, overdue_fees, inventory_summary |
| `test_export_service.cpp` | permitted role queues; EXPORT_UNAUTHORIZED + audit on denial; PDF max_concurrency=1 |
| `test_export_service_formats.cpp` | CSV file non-empty; PDF output_path set; Auditor/Clerk denied; Manager pdf-not-granted |
| `test_retention_service.cpp` | old animal anonymized; recent skipped; audit_events count unchanged |
| `test_retention_service_boundaries.cpp` | SENTINEL audit row description unchanged; at-threshold processed; already-anonymized no new audit row |
| `test_scheduler_service.cpp` | self-edge circular; A→B+B→A circular; EnqueueOnDemand inserts job |
| `test_scheduler_graph.cpp` | acyclic → false; cycle → true; NextReadyJobs prereqs-completed only |
| `test_checkpoint_service.cpp` | round-trip; `"password":` rejected; email pattern rejected; safe payload full field verify |
| `test_job_queue.cpp` | 5 pdf jobs serial (cap=1); report jobs concurrent (cap=2); stop_token cancels cleanly |
| `test_command_dispatcher.cpp` | unknown → 404; missing session → 401; rate-limit → 429+retry_after; kennel.search 200; real role looked up from UserRepository |
| `test_table_sort_state.cpp` | default unsorted; ToggleSort resets on column change; ComputeIndices filter; FormatTsv |
| `test_validation_state.cpp` | SetError/HasErrors; ClearField individual; Clear all; AllErrors semicolons |
| `test_tray_badge_state.cpp` | 0 → hidden; 99 → "99"; 100+ → "99+"; Clear resets; mixed types sum correctly |
| `test_keyboard_shortcut_handler.cpp` | Ctrl+F; Ctrl+N disabled for Auditor; F2 disabled in edit mode; full action matrix IsEnabled |
| `test_global_search_controller.cpp` | kennel name match; Auditor result masked; case-insensitive |
| `test_app_controller.cpp` | open/close/is-open; Ctrl+F → BeginGlobalSearch; checkpoint round-trip |
| `test_kennel_board_controller.cpp` | Refresh → Loaded; SubmitBooking overlap → BookingConflict; ClipboardTsv has header |
| `test_item_ledger_controller.cpp` | SubmitAddItem happy path; duplicate serial state; IssueStock qty decreases; FilterShowLowStockOnly |
| `test_reports_controller.cpp` | TriggerReport returns run_id; RequestExport unauthorized returns 0; IsDirty after trigger |
| `test_audit_log_controller.cpp` | Manager export CSV; Auditor export masked; Clerk export denied; filter narrows results |
| `test_alerts_panel_controller.cpp` | low-stock trigger; Auditor cannot acknowledge; tray badge updated after refresh |
| `test_scheduler_panel_controller.cpp` | job list loaded; detail includes pipeline stages; Auditor cannot enqueue |
| `test_admin_panel_controller.cpp` | Clerk cannot update catalog; Admin creates price rule; Admin sets retention policy |
| `test_admin_service.cpp` | UpdateCatalogEntry success + audit row; CreatePriceRule Clerk forbidden; SetRetentionPolicy persisted |
| `test_consent_service.cpp` | RecordConsent returns id; list returns recorded; WithdrawConsent sets timestamp; empty list for unknown entity; multiple consents same entity; isolation across entities; non-withdrawn withdrawn_at=0; RecordConsent writes CONSENT_RECORDED audit event; WithdrawConsent writes CONSENT_WITHDRAWN audit event; entity_type scoping |
| `test_worker_registry.cpp` | RegisterAll no-throw; queue can start; backup missing params → failed + "required"; backup non-existent db → failed; backup with real file → success + output_json; LAN sync missing params → failed + "required"; LAN sync non-Windows → unavailable error; ReportGenerate invalid JSON → failed; AlertScan with valid params → success |
| `test_clipboard_helper.cpp` | non-Win32 returns false; empty string; large string (100k chars) |
| `test_update_manager.cpp` | Apply before verify refused; version inferred from ShelterOpsDesk-N.N.N.msi filename; rollback.json survives Reset; corrupt JSON no crash; NormalizeThumbprint deduplication; rollback available after versioned apply |
| `test_diagnostics_controller.cpp` | DB stats page_size > 0; registered cache in stats; worker idle when not started |
| `test_report_stage_graph.cpp` | stage sequence; NextStage(Visualize) → nullopt |
| `test_report_repository.cpp` | run insert/find; snapshot insert/query |
| `test_scheduler_repository.cpp` | job insert; dependency edge insert; GetByStatus filters |
| `test_admin_repository.cpp` | catalog CRUD; price rule CRUD; CanExport permission matrix |
| `test_animal_repository.cpp` | insert/find; anonymize; date filter |
| `test_command_dispatcher_surface.cpp` | all 11 commands known (no 404); role matrix (Clerk→403 on approve); rate-limit → 429; Auditor response masked |

### API / Integration Tests (`api_tests/`)

| File | Key Assertions |
|---|---|
| `test_automation_bootstrap.cpp` | endpoint disabled default; LAN sync disabled; port unprivileged; rate_limit positive |
| `test_automation_contracts.cpp` | error envelope shape; no stack trace; code → status |
| `test_automation_auth_middleware.cpp` | missing token → 401; bad fingerprint → 401; expired session → 401; rate-limit → 429 |
| `test_error_envelope_contract.cpp` | every ErrorCode maps to expected HTTP status |
| `test_command_kennel_search.cpp` | kennel.search 200 ok; every entry has kennel_name/zone_id/nightly_cents; reasons non-empty; each reason has code+detail |
| `test_command_booking_flow.cpp` | booking.create → 201 with booking_id; booking.cancel → 200; missing kennel_id → 400 |
| `test_command_inventory_flow.cpp` | inventory.receive → 200; inventory.issue → 200; Auditor issue → 403; missing fields → 400 |
| `test_command_export_throttling.cpp` | second rapid request on 1-RPM limiter → 429; export.request missing run_id → 400 |
| `test_command_authorization.cpp` | invalid session → 401; Auditor booking.create → 403; Auditor inventory.receive → 403; Admin booking.create → 201 |
| `test_kennel_board_flow.cpp` | full search → book → approve → cancel; capacity=2 dual booking; recommendation persisted |
| `test_item_ledger_flow.cpp` | add → receive → issue cycle; barcode scan; duplicate serial audit; over-issue fails |
| `test_reports_studio_flow.cpp` | trigger → version label unique; CSV permitted; PDF forbidden for Auditor; version delta |
| `test_admin_approval_flow.cpp` | Manager approves; Clerk denied; BOOKING_APPROVED audit; Clerk forbidden from adjustment |
| `test_command_surface_inventory.cpp` | all 11 commands registered; wrong-role → 403; unknown → 404; invalid session → 401; count == 11 |
| `test_command_report_flow.cpp` | report.trigger → 202 with run_id; report.status body contract (run_id, status, version_label, started_at); unknown run_id → 404 with error field; export.request → 202 with job_id; Auditor export → 403 with error field; Auditor/Clerk trigger → 403; two sequential runs have distinct ids; invalid session → 401 |
