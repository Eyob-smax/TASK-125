# ShelterOps Desk Console — Architecture Design

## 1. Classification and Scope

**Application type:** Windows 11 native desktop application  
**Classification:** Desktop_app  
**Target OS:** Windows 11 (build 22000+)  
**Display:** 1920×1080 minimum, PerMonitorV2 DPI awareness  
**Locale:** en-US  
**Network model:** Offline-first; no network dependency for any core function  
**Optional features requiring network:** LAN sync (off by default), local automation endpoint (off by default, loopback only)

---

## 2. Technology Stack

| Concern | Choice | Version |
|---|---|---|
| Language | C++20 | ISO C++20 |
| Build system | CMake + Ninja | CMake 3.28+, Ninja 1.11+ |
| Dependency manager | vcpkg (manifest mode) | latest stable |
| UI framework | Dear ImGui (docking branch) | 1.90+ |
| Renderer / windowing | DirectX 11 + Win32 | Windows SDK 10.0+ |
| Charts | ImPlot | 0.17+ |
| Persistence | SQLite | 3.45+ (WAL mode) |
| Logging | spdlog | 1.13+ |
| Serialization | nlohmann/json | 3.11+ |
| Password hashing | Argon2id via libsodium | 1.0.18+ |
| Field encryption | AES-256-GCM via libsodium | 1.0.18+ |
| Key wrapping | Windows DPAPI / Credential Manager | Win32 API |
| Testing | GoogleTest + CTest | 1.14+ |
| Packaging | WiX Toolset 4.x | 4.x |
| Automation loopback | cpp-httplib (header-only) | 0.15+ (deferred — HTTP transport not yet started; `CommandDispatcher` in-process contract is frozen) |

---

## 3. Desktop Shell Topology

```
WinMain (src/main.cpp)
  │
  ├── Logging bootstrap (spdlog → cfg.log_dir/shelterops.log, level from cfg.log_level)
  ├── AppConfig::LoadOrDefault("shelterops.json")
  ├── Directory bootstrap: log_dir, exports_dir, update_metadata_dir
  ├── Win32 window creation (WNDCLASSEXW, CreateWindowExW)
  ├── D3D11 device + swap chain
  ├── ImGui context init (docking + multi-viewport)
  │
  └── ShellController (src/shell/ShellController.cpp)
        │
        ├── MenuBar — global menu (File, Windows, Tools, Help)
        ├── KeyboardShortcutHandler — Ctrl+F, Ctrl+N, F2, Ctrl+Shift+E, Ctrl+1..5
        ├── TrayManager — Win32 NOTIFYICONDATA, badge count updates [Win32-only]
        ├── AppController — tracks open window state (docked/detached)
        │     ├── KennelBoardController + KennelBoardView
        │     ├── ItemLedgerController + ItemLedgerView
        │     ├── ReportsController + ReportsView
      │     ├── SchedulerPanelController + SchedulerPanelView
      │     ├── AdminPanelController + AdminPanelView (administrator role only)
      │     ├── AuditLogController + AuditLogView
      │     └── AlertsPanelController + AlertsPanelView
        ├── CheckpointService — saves layout + form state to crash_checkpoints table
        └── AlertWorker — background thread, polls AlertService every 60 s
```

The shell owns the ImGui docking space, the main menu bar, and the tray icon. Window
open/close state is managed by `AppController`. The Win32-specific `WindowManager`
concept is implemented as part of `AppController` logic (no separate class).

---

## 4. Offline-Only Operating Model

The application has no mandatory network dependency. All data is persisted locally in
SQLite. The following features exist in optional/gated form:

| Feature | Default | Requirement to enable |
|---|---|---|
| LAN sync | Off | Admin enables + configures peer host/port and `pinned_certs_path`; Schannel TLS with pinned certificate verification is enforced before any data transfer |
| Local automation endpoint | Off | Admin enables in config; loopback only |
| Update import | Manual | Admin imports signed .msi via file dialog |

No feature degrades when the machine is offline. Alert polling, scheduled report
generation, and barcode lookup all operate without network access.

---

## 5. Role Map

| Role | Capabilities |
|---|---|
| **Administrator** | All access: user management, catalog/policy/config edits, price rules, permission matrix, update import, rollback, retention configuration, audit log export |
| **Operations Manager** | Kennel board, item ledger (read), reports studio, scheduled report management, alert management, booking approvals, after-sales adjustments |
| **Inventory Clerk** | Item ledger (full read/write), barcode lookup, inbound/outbound recording, item issuance, expiration marking |
| **Auditor** | Read-only access to audit log (masked PII only) and masked inventory/booking summaries; cannot write any record; all views show masked fields |

Role is set by an Administrator and stored in the `users` table. Role checks are
enforced at both the controller layer (UI gating) and the service layer (authorization
checks before any mutation).

---

## 6. Layered Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  UI Layer (src/ui/views/, src/shell/)  [Win32/ImGui only]    │
│  KennelBoardView, ItemLedgerView, ReportsView,               │
│  AdminPanelView, AuditLogView, AlertsPanelView,              │
│  SchedulerPanelView, LoginView, DockspaceShell, TrayManager  │
│  — Dear ImGui render functions only; no SQLite calls —       │
├─────────────────────────────────────────────────────────────┤
│  Controller/Presenter Layer (src/ui/controllers/, src/shell/)│
│  KennelBoardController, ItemLedgerController,                │
│  ReportsController, AdminPanelController, AuditLogController,│
│  AlertsPanelController, SchedulerPanelController,            │
│  DiagnosticsController, AppController, ShellController,      │
│  GlobalSearchController                                      │
│  — Cross-platform; translates UI events to service calls —   │
├─────────────────────────────────────────────────────────────┤
│  Application Service Layer (src/services/)                   │
│  BookingService, InventoryService, ReportService,            │
│  AlertService, SchedulerService, ExportService,              │
│  AuditService, AuthService, RetentionService,                │
│  AdminService, ConsentService, CheckpointService,            │
│  CommandDispatcher, FieldMasker, AuthorizationService        │
│  [Lan sync worker/export path] — Business logic, orchestration — │
├─────────────────────────────────────────────────────────────┤
│  Domain Rules Layer (src/domain/)                            │
│  BookingRules, InventoryRules, ReportPipeline,               │
│  AlertRules, RetentionPolicy, RolePermissions,               │
│  BookingStateMachine, PriceRuleEngine, SchedulerGraph        │
│  — Pure domain logic; no I/O dependencies —                  │
├─────────────────────────────────────────────────────────────┤
│  Repository Layer (src/repositories/)                        │
│  KennelRepository, BookingRepository, InventoryRepository,   │
│  ReportRepository, AuditRepository, UserRepository,          │
│  SessionRepository, MaintenanceRepository, AdminRepository,  │
│  SchedulerRepository, AnimalRepository, ConfigRepository     │
│  — All SQLite access; parameterized queries only —           │
├─────────────────────────────────────────────────────────────┤
│  Infrastructure Layer (src/infrastructure/)                  │
│  Database, MigrationRunner, CryptoHelper, CredentialVault,   │
│  BarcodeHandler, UpdateManager, CrashCheckpoint,             │
│  AtomicFileWriter, RateLimiter, DeviceFingerprint,           │
│  BoundedCache, CancellationToken                             │
│  [AutomationEndpoint HTTP transport deferred]                │
│  — OS integration, crypto, SQLite connection management —    │
├─────────────────────────────────────────────────────────────┤
│  Worker Layer (src/workers/)                                 │
│  JobQueue, WorkerRegistry                                    │
│  — Bounded jthread pool; std::stop_token cancellation —      │
└─────────────────────────────────────────────────────────────┘
```

**Hard constraint:** No `sqlite3_*` calls inside any Dear ImGui render function.
All data access flows through repositories called by services called by controllers.
ImGui render functions read only from in-memory view-models populated by controllers.

---

## 7. Multi-Window Model

The application uses ImGui's docking system as the primary layout manager. Each major
workflow runs in a named ImGui window that can be:
- Docked into the main viewport's docking space
- Detached as a floating window
- Moved to a secondary monitor as an independent ImGui viewport

**Window inventory:**

| Window ID | Controller | View | Min Role |
|---|---|---|---|
| `KennelBoard` | `KennelBoardController` | `KennelBoardView` | inventory\_clerk |
| `ItemLedger` | `ItemLedgerController` | `ItemLedgerView` | inventory\_clerk |
| `ReportsStudio` | `ReportsController` | `ReportsView` | operations\_manager |
| `AdminPanel` | `AdminPanelController` | `AdminPanelView` | administrator |
| `AuditLog` | `AuditLogController` | `AuditLogView` | auditor |
| `AlertsPanel` | `AlertsPanelController` | `AlertsPanelView` | any |

`AppController` tracks the open-window set; ImGui writes the docking layout to
`shelterops_layout.ini` on clean exit and restores it on startup.
`CheckpointService` stores a JSON snapshot of open windows to the `crash_checkpoints`
SQLite table to allow restoration after abnormal termination.

---

## 8. Tray Mode and Background Workers

### Tray mode

`TrayManager` registers a `NOTIFYICONDATA` icon in the Win32 system tray. On `WM_CLOSE`,
the application hides its window instead of exiting (tray-resident mode). The tray icon
shows a badge overlay with the count of active low-stock + expiring-soon alerts.

Tray right-click menu items:
- Open ShelterOps (shows main window)
- Low-Stock Alerts (opens AlertsPanel filtered to low-stock)
- Expiration Alerts (opens AlertsPanel filtered to expiring-soon)
- Exit (clean shutdown after checkpoint write)

### Background workers

| Worker | Thread | Function |
|---|---|---|
| `AlertWorker` | Dedicated thread | Polls `AlertService` every 60s; updates tray badge count |
| `JobQueue` | Thread pool (2–4 workers) | Runs scheduled and on-demand jobs |
| `RetentionWorker` | Daily timer | Runs `RetentionService` at configurable local time |

All workers are cancellable via a shared `std::stop_token` (C++20 `std::jthread`).
Workers do not hold database connections across sleep intervals; they acquire and release
connections per operation using the connection pool in `Database`.

---

## 9. SQLite and Migration Strategy

### Connection model

`Database` owns a single WAL-mode SQLite connection with the following pragmas set at open:
```sql
PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;
PRAGMA foreign_keys = ON;
PRAGMA busy_timeout = 5000;
```

A bounded connection pool (max 4 connections) allows concurrent reads by background
workers without blocking the UI thread.

### Migration strategy

`MigrationRunner` reads all `*.sql` files from `database/` in ascending numeric order
(e.g., `001_initial_schema.sql`, `002_kennels_and_bookings.sql`). It compares the file
list against the `db_migrations` table and applies only scripts not yet recorded there.
Each script runs in a transaction; failure rolls back the script and halts startup with
a logged error.

### Append-only audit enforcement

The `AuditRepository` class is the only code path that inserts into `audit_events`.
It never issues `UPDATE` or `DELETE` against that table. The repository's interface
exposes `Insert(...)` only; no `Delete` or `Update` methods exist. This invariant is
verified by the audit repository unit test.

---

## 10. Automation Endpoint and Plugin Boundary

**HTTP transport status: deferred.** The in-process command contract is frozen and
fully implemented and tested. The HTTP adapter that would bind `127.0.0.1:<port>` is
not yet started; no port is opened in the current build.

The in-process layer is `CommandDispatcher` — the authoritative contract that the
future HTTP adapter will implement. See `docs/api-spec.md §8` for the full envelope
and command inventory.

`CommandDispatcher` constructor dependencies (as of security audit):
`BookingService`, `InventoryService`, `ReportService`, `ExportService`, `AlertService`,
`SessionRepository`, `UserRepository`, `RateLimiter`, `AuditService`.
`UserRepository` is required so `Dispatch()` can look up the real role from
`user_sessions.user_id → users.role`; hardcoded role was removed.

Security controls enforced by `AutomationAuthMiddleware` (in-process, tested):
- Session token validation (from active `user_sessions` row)
- Device fingerprint header validation (HMAC-SHA256 of machine GUID + operator username,
  keyed by `ShelterOps/AutomationKey` from Windows Credential Manager)
- Rate limiting (60 req/min default, configurable via `automation_rate_limit_rpm`,
  per session token, in-memory token bucket)
- Role lookup from `UserRepository::FindById(session->user_id)` — real role, not hardcoded
- Role check before any mutating operation

When the HTTP adapter is added: it will call `CommandDispatcher::Dispatch` directly;
no business logic will live in the HTTP layer. Default port 27315 (configurable via
`automation_endpoint_port`). The endpoint binds to `127.0.0.1` only — never
`0.0.0.0`.

---

## 11. Scheduler and Job Queue

`SchedulerService` manages a list of `ScheduledJob` descriptors stored in SQLite.
On startup, it compares next-run timestamps against the current time and enqueues
overdue jobs. On subsequent ticks (every 60s), it repeats the check.

`JobQueue` owns a thread pool of 2–4 workers. Each `JobDescriptor` has a `max_concurrency`
field; heavy export jobs use `max_concurrency = 1` to prevent UI freeze.

**Report pipeline job sequence:**
1. `COLLECT` — queries raw data from repositories
2. `CLEANSE` — applies date/zone/staff filters, removes nulls
3. `ANALYZE` — computes aggregated metrics
4. `VISUALIZE` — renders ImPlot data structures or generates file output

Each stage produces an intermediate result; failure at any stage writes an in-app
notification via `NotificationBus` and an audit event of type `JOB_FAILED`. No email
or SMS is used; all alerts surface inside the application only.

---

## 12. Security, Encryption, and Masking

### Password hashing

`CryptoHelper::HashPassword(plaintext)` hashes with Argon2id via `libsodium::crypto_pwhash`.
Parameters: `OPSLIMIT_INTERACTIVE` (3 iterations), `MEMLIMIT_INTERACTIVE` (64 MB),
algorithm = `crypto_pwhash_ALG_ARGON2ID13`. Hash stored in `users.password_hash`;
plaintext is zero-wiped via `sodium_memzero` after hashing. `CryptoHelper::VerifyPassword`
performs the constant-time comparison.

### Field encryption

`CryptoHelper::Encrypt(plaintext, key)` and `Decrypt(blob, key)` use AES-256-GCM
(`libsodium::crypto_aead_aes256gcm`). A random 12-byte nonce is prepended to each
ciphertext blob. The 32-byte application data key is stored in Windows Credential Manager
under `ShelterOps/DataKey`, DPAPI-wrapped with user-scoped protection (`CRYPTPROTECT_UI_FORBIDDEN`, no `CRYPTPROTECT_LOCAL_MACHINE`) so only the specific Windows user account running the application can decrypt it (`CredentialVault::Store` / `Load`).
On non-Windows (CI only), a file-based fallback with 0600 permissions is used with an
explicit warning. See Q-015 in `docs/questions.md` for provisioning details.

Sensitive fields encrypted at rest: owner phone number, owner email, owner address,
any PII marked as sensitive in the catalog.

### Field masking

`FieldMasker::MaskViewModel(role, entity_type, row)` applies policies from
`domain::RolePermissions::GetMaskingRule`:
- Phone numbers → last 4 digits only (`***-***-1234`)
- Email addresses → domain only (`****@shelter.org`)
- Display names → initials (`J.D.`)
- Unknown PII fields for Auditor → `Redact`

Masking is applied in every controller/presenter before building the view-model.
Decrypted values never appear in logs (`SecretSafeLogger`) or error envelopes (`ErrorEnvelope`).

### Session lifecycle and dual-expiry model

`AuthService::Login()` creates a `user_sessions` row with two expiry timestamps:
- `expires_at` — sliding inactivity window, reset to `now + 3600s` on each validated request
- `absolute_expires_at` — hard ceiling set to `now + 43200s` (12 hours) at login, never extended

`AuthService::ValidateSession()` rejects the session if either timestamp is exceeded,
enforcing: a 1-hour inactivity timeout AND a hard 12-hour session cap regardless of activity.
Migration `012_session_absolute_expiry.sql` adds the `absolute_expires_at` column to
`user_sessions` with `DEFAULT 0` (treated as no cap for pre-migration rows).

### Audit log

`AuditRepository` is append-only. Every authentication event, data mutation, export,
job failure, duplicate serial rejection, and retention action writes an `audit_events`
row. The row contains the actor role, event type, entity reference, and a description
string with no PII, no decrypted values, and no passwords or tokens.

`AuditService::RecordSystemEvent()` applies `SanitizeFreeText()` to the caller-supplied
description before storage. `SanitizeFreeText` replaces email-shaped patterns with
`[EMAIL-REDACTED]` and phone-shaped patterns with `[PHONE-REDACTED]` using regex, so
free-text descriptions never leak PII even when callers pass raw strings.

---

## 13. Packaging, Update, and Rollback

### MSI packaging

WiX Toolset 4.x generates a signed `.msi` from `packaging/product.wxs`. The installer:
- Installs to `%ProgramFiles%\ShelterOps Desk Console\`
- Creates a Start menu shortcut
- Registers for Add/Remove Programs

The `.msi` is Authenticode signed post-build. The signing certificate thumbprint is
included in `trusted_publishers.json` shipped with the installation.

### Update import

`UpdateManager::VerifyPackage(msi_path)`:
1. Calls `WinVerifyTrust` (Authenticode chain)
2. Extracts signer thumbprint from PE/MSI signature
3. Compares thumbprint against `trusted_publishers.json`
4. If valid: saves rollback metadata to `update_history` table, then launches installer
5. If invalid: rejects with `SIGNATURE_INVALID` and writes audit event

### Rollback

The `update_history` table stores the path and hash of the previous installer.
`UpdateManager::Rollback()` verifies the stored hash, then launches the previous `.msi`
with `/passive` flags. Available from the Admin Panel and the automation endpoint.

---

## 14. Requirement-to-Module Traceability

| Original Prompt Requirement | Primary Module(s) |
|---|---|
| Multi-window: Intake & Kennel Board | `src/ui/views/KennelBoardView`, `src/ui/controllers/KennelBoardController`, `src/services/BookingService`, `src/domain/BookingRules`, `src/repositories/KennelRepository` |
| Multi-window: Item Ledger | `src/ui/views/ItemLedgerView`, `src/ui/controllers/ItemLedgerController`, `src/services/InventoryService`, `src/domain/InventoryRules`, `src/repositories/InventoryRepository` |
| Multi-window: Reports Studio | `src/ui/views/ReportsView`, `src/ui/controllers/ReportsController`, `src/services/ReportService`, `src/domain/ReportPipeline`, `src/repositories/ReportRepository` |
| Role-based access (4 roles) | `src/services/AuthService`, `src/domain/RolePermissions`, `src/repositories/UserRepository` |
| Keyboard shortcuts (Ctrl+F, Ctrl+N, F2, Ctrl+Shift+E, Ctrl+1..5) | `src/shell/KeyboardShortcutHandler` |
| Right-click context menus | `src/ui/views/ItemLedgerView`, `src/ui/views/KennelBoardView`, `src/ui/controllers/ItemLedgerController` |
| Clipboard TSV copy | `src/shell/ClipboardHelper` |
| System tray + alert badges | `src/shell/TrayManager`, `src/services/AlertService`, `src/workers/AlertWorker` |
| Kennel search/filter/sort | `src/services/BookingService`, `src/repositories/KennelRepository` |
| Constraint-based bookability + ranked results | `src/domain/BookingRules` |
| SQLite inventory ledger + timestamps | `src/repositories/InventoryRepository`, `src/infrastructure/Database` |
| Barcode keyboard-wedge input | `src/infrastructure/BarcodeHandler` |
| Reporting dashboards (4 metrics) | `src/domain/ReportPipeline`, `src/services/ReportService` |
| Multi-dim filter + CSV/PDF export | `src/services/ExportService`, `src/services/ReportService` |
| Scheduled report orchestration | `src/services/SchedulerService`, `src/workers/JobQueue` |
| Anomaly/failure alerting in-app | `src/services/AlertService`, `src/workers/AlertWorker` |
| Historical version comparison (metric deltas) | `src/services/ReportService`, `src/repositories/ReportRepository` |
| Admin catalogs, policies, price rules/promos | `src/services/AdminService`, `src/ui/views/AdminPanelView`, `src/ui/controllers/AdminPanelController` |
| After-sales adjustments | `src/services/AdminService`, `src/ui/controllers/AdminPanelController` |
| Local job queue + throttling | `src/workers/JobQueue`, `src/services/SchedulerService` |
| AES-256 field encryption | `src/infrastructure/CryptoHelper` |
| DPAPI key wrapping | `src/infrastructure/CredentialVault` |
| Field masking (PII) | `src/domain/RolePermissions` |
| Consent flags + retention controls | `src/services/RetentionService`, `src/repositories/UserRepository` |
| Append-only audit log + export | `src/repositories/AuditRepository`, `src/services/AuditService`, `src/services/ExportService` |
| Local automation endpoint (in-process) | `src/services/CommandDispatcher` (depends on `UserRepository` for real role lookup), `src/services/AutomationAuthMiddleware` |
| Device fingerprinting | `src/infrastructure/DeviceFingerprint`, `src/services/AutomationAuthMiddleware` |
| Optional LAN sync transport | `src/workers/WorkerRegistry.cpp` — Schannel TLS client with pinned peer certificate verification (`pinned_certs_path` JSON); AES-256-GCM payload encryption before TLS send. Submission wired via `SchedulerService::SubmitLanSync` (reads `AppConfig::lan_sync_peer_host/peer_port/pinned_certs_path`, creates WAL-safe DB snapshot, builds `params_json`, calls `EnqueueOnDemand`). Periodic auto-trigger in `main.cpp` (300 s cadence, gated on `cfg.lan_sync_enabled` and authenticated session). Admin-triggered path via `SchedulerPanelController::TriggerLanSync`. |
| Crash checkpointing | `src/infrastructure/CrashCheckpoint` |
| Signed MSI import + rollback | `src/infrastructure/UpdateManager` |
| Bounded caches + resource cleanup | `src/infrastructure/Database`, all workers via `std::jthread` |
| Debug-build leak detection | `src/main.cpp` (D3D11_CREATE_DEVICE_DEBUG flag, `SHELTEROPS_DEBUG_LEAKS`) |
| Maintenance response time metric | `src/domain/ReportPipeline`, `src/repositories/MaintenanceRepository` |

---

## 15. Actual Source Tree

```
repo/desktop/
├── CMakeLists.txt
├── vcpkg.json
├── Dockerfile
├── src/
│   ├── main.cpp                          ← Win32 + DX11 + ImGui entry (Windows only)
│   ├── AppConfig.cpp
│   ├── shell/
│   │   ├── ShellController.cpp
│   │   ├── SessionContext.cpp
│   │   ├── ErrorDisplay.cpp
│   │   ├── TrayBadgeState.cpp            ← cross-platform badge counter
│   │   ├── TrayManager.cpp               ← Win32-only: Shell_NotifyIconW
│   │   ├── KeyboardShortcutHandler.cpp
│   │   ├── DockspaceShell.cpp            ← Win32/ImGui dockspace host
│   │   └── ClipboardHelper.cpp           ← Win32-only: SetClipboardData
│   ├── ui/views/                         ← Win32/ImGui render functions
│   │   ├── LoginView.cpp
│   │   ├── KennelBoardView.cpp
│   │   ├── ItemLedgerView.cpp
│   │   ├── ReportsView.cpp
│   │   ├── AdminPanelView.cpp
│   │   ├── AuditLogView.cpp
│   │   ├── AlertsPanelView.cpp
│   │   └── SchedulerPanelView.cpp
│   ├── ui/controllers/                   ← cross-platform; no ImGui dependency
│   │   ├── AppController.cpp
│   │   ├── GlobalSearchController.cpp
│   │   ├── KennelBoardController.cpp
│   │   ├── ItemLedgerController.cpp
│   │   ├── ReportsController.cpp
│   │   ├── AdminPanelController.cpp
│   │   ├── AuditLogController.cpp
│   │   ├── AlertsPanelController.cpp
│   │   ├── SchedulerPanelController.cpp
│   │   └── DiagnosticsController.cpp
│   ├── ui/primitives/
│   │   ├── TableSortState.cpp
│   │   └── ValidationState.cpp
│   ├── services/
│   │   ├── AuthService.cpp
│   │   ├── AuthorizationService.cpp
│   │   ├── AuditService.cpp
│   │   ├── FieldMasker.cpp
│   │   ├── AutomationAuthMiddleware.cpp
│   │   ├── CommandDispatcher.cpp
│   │   ├── BookingService.cpp
│   │   ├── InventoryService.cpp
│   │   ├── ReportService.cpp
│   │   ├── AlertService.cpp
│   │   ├── SchedulerService.cpp
│   │   ├── ExportService.cpp
│   │   ├── RetentionService.cpp
│   │   ├── AdminService.cpp
│   │   ├── ConsentService.cpp
│   │   ├── CheckpointService.cpp
│   │   └── LockoutPolicy.cpp
│   ├── domain/
│   │   ├── BookingRules.cpp
│   │   ├── BookingStateMachine.cpp
│   │   ├── BookingSearchFilter.cpp
│   │   ├── PriceRuleEngine.cpp
│   │   ├── InventoryRules.cpp
│   │   ├── ReportPipeline.cpp
│   │   ├── ReportStageGraph.cpp
│   │   ├── AlertRules.cpp
│   │   ├── RetentionPolicy.cpp
│   │   ├── RolePermissions.cpp
│   │   └── SchedulerGraph.cpp
│   ├── repositories/
│   │   ├── UserRepository.cpp
│   │   ├── SessionRepository.cpp
│   │   ├── AuditRepository.cpp
│   │   ├── ConfigRepository.cpp
│   │   ├── KennelRepository.cpp
│   │   ├── BookingRepository.cpp
│   │   ├── AnimalRepository.cpp
│   │   ├── InventoryRepository.cpp
│   │   ├── MaintenanceRepository.cpp
│   │   ├── ReportRepository.cpp
│   │   ├── SchedulerRepository.cpp
│   │   └── AdminRepository.cpp
│   ├── workers/
│   │   ├── JobQueue.cpp
│   │   └── WorkerRegistry.cpp
│   ├── common/
│   │   ├── ErrorEnvelope.cpp
│   │   ├── Validation.cpp
│   │   ├── SecretSafeLogger.cpp
│   │   └── Uuid.cpp
│   └── infrastructure/
│       ├── Database.cpp
│       ├── MigrationRunner.cpp
│       ├── CryptoHelper.cpp
│       ├── CredentialVault.cpp
│       ├── DeviceFingerprint.cpp
│       ├── RateLimiter.cpp
│       ├── AtomicFileWriter.cpp
│       ├── BarcodeHandler.cpp
│       ├── CrashCheckpoint.cpp
│       └── UpdateManager.cpp
├── include/shelterops/
│   └── (mirrors src/ structure with .h files, plus Version.h)
├── database/
│   ├── 001_initial_schema.sql
│   ├── 002_facility_and_kennels.sql
│   ├── 003_animals_and_bookings.sql
│   ├── 004_inventory.sql
│   ├── 005_maintenance.sql
│   ├── 006_reports_and_exports.sql
│   ├── 007_admin_policies.sql
│   ├── 008_scheduler_and_jobs.sql
│   ├── 009_update_management.sql
│   ├── 010_auth_lockout.sql
│   ├── 011_audit_immutability.sql
│   └── 012_session_absolute_expiry.sql
├── resources/
│   └── ShelterOpsDesk.exe.manifest
├── packaging/
│   ├── product.wxs
│   ├── version.wxi
│   └── trusted_publishers.json          ← pinned Authenticode thumbprints (provision at release)
├── unit_tests/
│   ├── CMakeLists.txt
│   └── (84 test files — see docs/test-traceability.md §20)
└── api_tests/
    ├── CMakeLists.txt
    └── (15 test files — see docs/test-traceability.md §18)
```

---

## 16. Domain Sequence Flows

### 16.1 Kennel Recommendation and Bookability Evaluation

1. Operator enters search criteria in `KennelBoardWindow` (date range, zone filter, pet type, max distance, price range).
2. `KennelBoardController` calls `BookingService::SearchAndRank(filter)`.
3. `BookingService` calls `KennelRepository::GetCandidates(filter)` → list of `KennelInfo` (with restrictions + coords).
4. `BookingService` calls `KennelRepository::GetExistingBookings(date_range)` → list of `ExistingBooking`.
5. `domain::RankKennels(candidates, request, existing_bookings, reference_coord, max_dist_ft)` evaluates each:
   a. `EvaluateBookability` checks: purpose == Boarding, restrictions, overlap count vs. capacity.
   b. Bookable kennels are scored: restriction compliance, distance, price, rating.
   c. Returns `vector<RankedKennel>` sorted by score descending.
6. `KennelBoardController` builds the view-model; each row shows rank, score, and the formatted explanation string.
7. Recommendation results are persisted via `KennelRepository::SaveRecommendationResults(query_hash, results)` for audit trail.

### 16.2 Approval-Request and Approval-Decision Flow

1. Operator creates a booking in `KennelBoardWindow`. `KennelBoardController` calls `BookingService::CreateBooking(request)`.
2. `BookingService` checks `system_policies['booking_approval_required']`.
   - If `true`: booking is inserted with `status='pending'`; a `booking_approvals` row is inserted with `decided_at=NULL`.
   - If `false`: booking is inserted with `status='approved'` and no approval row.
3. `AlertService` surfaces pending approvals as in-app notifications for Operations Managers and Administrators.
4. Approver opens the pending booking in `KennelBoardWindow`. `KennelBoardController` calls `BookingService::ApproveBooking(booking_id, decision, notes)`.
5. `BookingService` validates `CanApproveBooking(actor_role)`.
6. `BookingRepository` updates `bookings.status` and `booking_approvals.decision` + `decided_at` in a single transaction.
7. `AuditService` writes an `audit_events` row with event_type `BOOKING_APPROVED` or `BOOKING_REJECTED`.

### 16.3 Barcode Entry and Duplicate Serial Handling

1. Operator focuses the barcode field in `ItemLedgerWindow` and scans with USB wedge scanner.
2. `BarcodeHandler::OnInput(raw_input)` validates the input string via `ValidateBarcode(barcode)`.
3. On invalid format: `ItemLedgerController` shows an inline error; an audit event `BARCODE_INVALID` is written.
4. On valid barcode: `InventoryRepository::LookupByBarcode(barcode)` returns the matching item.
5. If a serial number is provided during item creation: `InventoryService` calls `InventoryRepository::ExistsSerial(serial)`.
   - If `true`: `ValidateSerial(serial, true, existing_id)` returns the error with the owning item name.
   - `ItemLedgerController` displays the error with the conflicting item name; no INSERT is issued.
   - `AuditService` writes `DUPLICATE_SERIAL_REJECTED` with the attempted serial value (no PII).
   - If `false`: item is inserted; serial uniqueness constraint also guards at the SQLite layer.

### 16.4 Report Orchestration: Collect → Cleanse → Analyze → Visualize

1. `SchedulerService` detects that a `report_generate` job is ready (scheduled or manually triggered).
2. A `job_runs` row is inserted for each of four stages linked by `job_dependencies`: COLLECT → CLEANSE → ANALYZE → VISUALIZE.
3. `JobQueue` dispatches COLLECT stage to an available worker thread:
   - Worker queries repositories for raw rows (bookings, inventory, maintenance, etc.) filtered by `report_run.filter_json`.
   - Worker writes intermediate data to a temp table or serialized JSON in `job_runs.output_json`.
4. COLLECT completes (`status='completed'`); `SchedulerService` enqueues CLEANSE.
5. CLEANSE worker applies date/zone/staff filters, removes null critical fields, and normalizes units.
6. ANALYZE worker calls domain functions: `ComputeOccupancyRate`, `ComputeTurnoverRate`, `ComputeMaintenanceResponseHours`, `ComputeOverdueFeeDistribution`, etc. Writes metric rows to `report_snapshots`.
7. VISUALIZE worker populates ImPlot data buffers (stored as controller-level view-model) and/or writes CSV/PDF if an export was requested.
8. On stage failure: worker sets `job_runs.status='failed'` and `error_message`. `AlertService` surfaces an in-app notification via `NotificationBus`. No email or SMS.
9. `report_runs.status` transitions to `completed` or `failed` atomically when the final stage finishes.

### 16.5 Export Throttling and Snapshot/Version Comparison

1. Operator requests an export from `ReportsStudioWindow`. `ReportsController` calls `ExportService::EnqueueExport(run_id, format)`.
2. `ExportService` inserts into `export_jobs` with `max_concurrency=1` for PDF, `max_concurrency=4` for CSV.
3. `JobQueue` checks: if the number of running `export_csv`/`export_pdf` jobs ≥ `max_concurrency`, the job remains `queued`. This prevents simultaneous heavy I/O from freezing the UI.
4. When the queue slot is available, the worker reads `report_snapshots` rows for the run, formats them as CSV or calls the PDF renderer, and writes the output file.
5. For version comparison: operator selects two `report_runs` for the same `report_id` in `ReportsStudioWindow`.
6. `ReportsController` calls `ReportService::CompareVersions(run_id_a, run_id_b)`.
7. `ReportRepository` fetches `report_snapshots` for both runs; `ComputeVersionDelta(before_metrics, after_metrics)` produces `vector<MetricDelta>`.
8. The delta table is shown in `ReportsStudioWindow` with absolute and percentage change per metric.

### 16.6 Crash Checkpoint Save and Restore

**Save (on exit or OS signal):**
1. `ShellController::OnExit()` is triggered by `WM_CLOSE` / signal handler.
2. `AppController::SerializeWindowInventory()` → JSON of open window IDs and active window.
3. `KennelBoardController`, `ItemLedgerController`, `ReportsController` each expose `SerializeFormState()` → sanitized JSON (no PII, no passwords).
4. `CheckpointService::CaptureState()` runs a PII guard check, then calls `CrashCheckpoint::SaveCheckpoint(window_json, form_json)` which inserts into `crash_checkpoints` and prunes old rows (keeps last 3).
5. Application exits cleanly.

**Restore (on startup):**
1. `MigrationRunner` applies any pending migrations.
2. `AppController::RestoreCheckpoint()` calls `CrashCheckpoint::LoadLatest()` to fetch the most recent row.
3. `AppController` re-opens the previously open windows in their saved state.
4. Each controller receives its `form_state_json` and pre-fills form fields from it.
5. A banner is shown informing the operator that a previous session state was restored.

### 16.7 Update Import, Signature Verification, and Rollback

**Import:**
1. Operator selects an `.msi` file via file dialog in `AdminPanelWindow`, or via the `/update/import` automation endpoint (deferred; in-process only for now).
2. `UpdateManager::ImportPackage(msi_path, now_unix)`:
   a. Copies the file to `<update_metadata_dir>/staged.msi`.
   b. Calls `WinVerifyTrust` for Authenticode chain verification. On non-Windows CI: always returns `SignatureFailed`.
   c. On signature success: sets state to `Staged`.
3. `UpdateManager::Apply()` (Windows only):
   a. Preserves the previous staged installer as `rollback.msi` when available.
   b. Writes `rollback.json` to `update_metadata_dir` (previous version, previous installer path, timestamp).
   c. Runs `msiexec /quiet /norestart /i "<staged.msi>"`; waits for exit code.
   d. On msiexec success: sets state to `Applied`.
4. On next startup, `MigrationRunner` applies any new `.sql` files; session restored from checkpoint.

**Note:** `update_packages` and `update_history` SQLite tables (migration 009) are available for a future full-audit-trail extension of `UpdateManager`. See `docs/questions.md` Q-027.

**Rollback:**
1. Administrator clicks Rollback in `AdminPanelWindow`.
2. `UpdateManager::RollbackToPrevious()` reads `rollback.json` from `update_metadata_dir`.
3. Verifies `previous_msi_path` still exists via `fs::exists`.
4. Runs `msiexec /quiet /norestart /i "<previous_msi_path>"`; waits for exit code.
5. If path not found: sets `LastError` to `NotFound`; rollback aborted.

---

## 17. Security Architecture and Trust Boundaries

### 17.1 Boundary Diagram

```
┌─────────────────────────────────────────────────────────┐
│  UI Layer (ImGui render functions)                        │
│  Input: masked view-models only; no raw PII               │
│  Output: user actions dispatched to controllers           │
│  Trust boundary: all values come from FieldMasker        │
├─────────────────────────────────────────────────────────┤
│  Controller/Presenter Layer                               │
│  Calls FieldMasker::MaskViewModel() before building VM   │
│  Calls AuthorizationService for every privileged action  │
│  Never passes decrypted data to ImGui layer               │
├─────────────────────────────────────────────────────────┤
│  Application Service Layer                                │
│  AuthService: hashes + verifies passwords (Argon2id)     │
│  AuditService: appends only; strips PII before write     │
│  FieldMasker: applies masked_field_policies per role     │
│  No decrypted PII ever leaves this layer into logs        │
├─────────────────────────────────────────────────────────┤
│  Repository Layer                                         │
│  Encrypts sensitive fields via CryptoHelper before write  │
│  Decrypts on read; returns plaintext only when authorized │
│  AuditRepository: INSERT-only — no UPDATE or DELETE ever  │
├─────────────────────────────────────────────────────────┤
│  Infrastructure / OS Layer                                │
│  CredentialVault: DPAPI (Windows) / file-based (CI)      │
│  CryptoHelper: libsodium AES-256-GCM + Argon2id          │
│  DeviceFingerprint: HMAC-SHA256 machine binding           │
│  RateLimiter: token-bucket per session token              │
│  SecretSafeLogger: scrubs secrets before spdlog call      │
└─────────────────────────────────────────────────────────┘
```

### 17.2 Where Masking Is Applied

Masking is applied at the **presentation boundary** — the controller/presenter layer — before view-models reach ImGui render functions. `FieldMasker::MaskViewModel(role, entity_type, row)` is called on every outbound row. Unmasked data is never passed to the UI layer.

Default masking invariant: for an Auditor, any field not explicitly known returns `Redact`. Other roles receive `None` (pass-through) for unknown fields. This ensures defense-in-depth — a newly added PII field will be redacted for Auditors by default.

### 17.3 Where Encryption Is Applied

Encryption is applied at the **service/repository boundary** before any write to SQLite. Sensitive fields (phone, email, address, consent-linked contact data) are encrypted with AES-256-GCM via `CryptoHelper::Encrypt(plaintext, data_key)`. The `data_key` is fetched from `CredentialVault` under the name `ShelterOps/DataKey`.

Decryption is performed on read, inside the repository, and the plaintext is passed to the service layer. Services apply masking before forwarding to controllers.

### 17.4 Where Auditing Is Applied

Auditing is applied at the **service boundary**. Every mutation to a sensitive entity calls `AuditService::RecordMutation(...)` which computes a field-level diff, strips PII-bearing fields from the diff (fields in the `kPiiFields` set), and appends one row to `audit_events` via `AuditRepository::Append`. The `audit_events` table is never modified after write.

### 17.5 Append-Only Audit Invariant (Code-Level Guardrail)

`AuditRepository` declares only `Append`, `Query`, and `ExportCsv` methods. No `Update` or `Delete` method exists in the class interface or implementation. The test `test_audit_repository.cpp` includes a compile-time check via `static_assert` and a runtime check confirming row count is exactly as inserted.

### 17.6 Lockout and Anti-Bot Controls

`LockoutPolicy` is a pure stateless policy class (no I/O):
- 5 consecutive failures within a 15-minute window → `locked_until = now + 15 min`
- 5 more failures while locked → escalate to `locked_until = now + 60 min`
- Successful login resets the counter

`AuthService` evaluates the policy and delegates counter storage to `UserRepository`. A dummy hash is computed on unknown-username attempts to prevent timing-based username enumeration.

`AuthService::ChangePassword` computes a new Argon2id hash and calls `UserRepository::UpdatePasswordHash(user_id, new_hash)` to persist it, then calls `SessionRepository::ExpireAllForUser(user_id)` to invalidate all existing sessions.

### 17.7 Automation Surface Security

The local automation endpoint (when enabled) requires:
1. `X-Session-Token`: a bearer token for an active `user_sessions` row
2. `X-Device-Fingerprint`: HMAC-SHA256(`machine_guid:operator_username`, `AutomationKey`) matching the session row
3. Rate limiting: 60 req/min per token (token-bucket; configurable)
4. Role checks before any mutating operation

All three checks are implemented in `AutomationAuthMiddleware` as pure functions without HTTP server dependency, allowing unit testing. The actual HTTP server (cpp-httplib) is wired in a later prompt.

### 17.8 Updated Requirement-to-Module Additions

| Requirement | Module(s) |
|---|---|
| Lockout / anti-bot | `src/services/LockoutPolicy.cpp`, `src/services/AuthService.cpp` |
| Service-level authorization | `src/services/AuthorizationService.cpp` |
| Append-only audit with diff | `src/services/AuditService.cpp`, `src/repositories/AuditRepository.cpp` |
| Presentation-boundary masking | `src/services/FieldMasker.cpp` |
| Token-bucket rate limiting | `src/infrastructure/RateLimiter.cpp` |
| Device fingerprinting (HMAC) | `src/infrastructure/DeviceFingerprint.cpp` |
| Crash-safe atomic writes | `src/infrastructure/AtomicFileWriter.cpp`, `src/infrastructure/CrashCheckpoint.cpp` |
| LRU bounded cache | `include/shelterops/infrastructure/BoundedCache.h` |
| Worker cancellation | `include/shelterops/infrastructure/CancellationToken.h` |
| Error envelope (JSON) | `src/common/ErrorEnvelope.cpp` |
| Secret-safe structured logging | `src/common/SecretSafeLogger.cpp` |
| V4 UUID generation | `src/common/Uuid.cpp` |
| Input validation | `src/common/Validation.cpp` |
| Session state machine | `src/shell/ShellController.cpp`, `src/shell/SessionContext.cpp` |
| User-friendly error display | `src/shell/ErrorDisplay.cpp` |
| Auth middleware (automation) | `src/services/AutomationAuthMiddleware.cpp` |

---

## §18. Core Domain Engine

This section describes the real operational flows implemented in the business engine layer (repositories, domain extensions, services, workers).

### 18.1 Kennel Search and Rank

1. `BookingService::SearchAndRank` loads all active kennels via `KennelRepository::ListActiveKennels` (includes zone coordinates and restrictions).
2. `FilterKennelsByHardConstraints` drops kennels outside requested zone_ids, below min_rating, above max_price, or non-boarding when `only_bookable=true`.
3. For each remaining kennel, `BookingRepository::ListOverlapping` counts active bookings in the requested window.
4. `BookingRules::EvaluateBookability` checks `overlapping < capacity` and restriction compatibility.
5. `BookingRules::RankKennels` scores bookable kennels and produces a `RankedKennel` list with `RankReason` codes (`RESTRICTION_MET`, `DISTANCE_OK`, `PRICE_BELOW_MAX`).
6. Results are persisted to `recommendation_results` with `ComputeQueryHash(filter)` as the key for 90-day audit trail.

### 18.2 Booking State Machine

Transitions are validated by `BookingStateMachine::Transition(current_status, event, can_approve_callback)`.

| From | Event | To | Notes |
|---|---|---|---|
| Pending | Approve | Approved | Requires `can_approve()=true` |
| Pending | Reject | Cancelled | Requires `can_approve()=true` |
| Pending | Cancel | Cancelled | Any write role |
| Approved | Activate | Active | |
| Approved | NoShow | NoShow | |
| Active | Complete | Completed | |
| Active | Cancel | Cancelled | |
| Completed/Cancelled/NoShow | any | — | ILLEGAL_TRANSITION |

Terminal states reject all events with `StateTransitionError{code="ILLEGAL_TRANSITION"}`.

### 18.3 Item Ledger — Receive/Issue

**Receive:** `InventoryService::ReceiveStock` inserts an `inbound_records` row (immutable `received_at`) and increments `inventory_items.quantity` in a single transaction via `InsertInbound`. Audit: `STOCK_RECEIVED`.

**Issue:** `InventoryService::IssueStock` guards `item.quantity >= qty`, inserts `outbound_records` (immutable `issued_at`), calls `DecrementQuantity`, then `UpsertUsageHistory` (ON CONFLICT accumulates per-day totals). Audit: `STOCK_ISSUED`.

**Duplicate Serial:** `AddItem` calls `FindBySerial` before INSERT. Duplicate → `DUPLICATE_SERIAL_REJECTED` audit + `ErrorCode::BookingConflict` with the existing item_id in the message.

**Barcode Lookup:** `LookupByBarcode` calls `BarcodeHandler::ProcessScan` → `ValidateBarcode` → `FindByBarcode`. Returns item or `ErrorCode::ItemNotFound`.

### 18.4 Maintenance Response-Time Capture

`MaintenanceService::RecordEvent` calls `SetFirstActionAt(ticket_id, ts)` only when `event_type ∈ {status_changed, assigned, resolved}`. `SetFirstActionAt` uses `WHERE first_action_at IS NULL`, making it idempotent — the second call is a no-op.

`MaintenanceRepository::GetResponsePoints` returns `{created_at, first_action_at, resolved_at}` tuples for use in `ReportPipeline::ComputeAvgMaintenanceResponseHours`.

### 18.5 Report Pipeline (collect → cleanse → analyze → visualize)

`ReportService::RunPipeline` executes four stages in the order defined by `ReportStageGraph::StageSequence()`:

1. **Collect** — loads raw data from repositories (bookings, kennels, items, tickets).
2. **Cleanse** — applies filter_json, drops nulls and invalid records.
3. **Analyze** — calls `domain::ReportPipeline::*` functions per report type (occupancy, maintenance_response, overdue_fees, inventory_summary).
4. **Visualize** — serializes per-metric values to JSON via `AtomicFileWriter`; inserts `report_snapshots` rows.

On any stage failure: `UpdateRunStatus(run_id, "failed", error_message, anomaly_flags_json)` + `JOB_FAILED` audit.

Version label: `GenerateVersionLabel(report_id, type, now_unix, prior_runs_today)` produces `<type>-<YYYYMMDD>-<NNN>` (zero-padded 3-digit sequence, 1-indexed).

### 18.6 Throttled Exports

`ExportService::RequestExport` checks `AdminRepository::CanExport(role, report_type, format)`. Denied → `EXPORT_UNAUTHORIZED` audit + `ErrorCode::ExportUnauthorized`. Allowed → inserts `export_jobs` with `max_concurrency=1` for PDF.

`JobQueue` enforces `ExportPdf=1`, `ExportCsv=1` concurrency caps in-memory. The DB `max_concurrency` field is documentation only.

### 18.7 Scheduler with Cycle Detection

`SchedulerService::RegisterDependency` loads all existing edges via `ListAllDependencies()`, runs `HasCircularDependency(edges, job_id, depends_on_id)` (DFS; also catches self-edges), and rejects with `CIRCULAR_JOB_DEPENDENCY` audit + error before any INSERT.

`ListReadyJobIds` returns only jobs with `status="queued"` and all prerequisite jobs `status="completed"`.

### 18.8 Retention Runner

`RetentionService::Run` loads policies from `AdminRepository::ListRetentionPolicies()` and evaluates candidates for `users`, `bookings`, `animals`, and `inventory_items`.
For each retention decision:
- `Delete` attempts repository hard-delete first.
- If delete fails (or policy action is `Anonymize`), the service anonymizes the record.
- A `RETENTION_APPLIED` audit event is appended for each applied decision.

`audit_events` remains append-only: retention does not update or delete existing audit rows.

### 18.9 Crash Checkpoint Persistence

`CheckpointService::CaptureState` serializes `WindowInventory` + `FormSnapshot` list to JSON and delegates to `CrashCheckpoint::SaveCheckpoint`. The PII guard rejects any payload containing `password`, `token`, `email`, or `@`.

`RestoreState` calls `LoadLatest()` and deserializes the structured payload. Returns `std::nullopt` on parse failure (corrupt checkpoint is silently skipped).

### 18.10 In-Process Command Dispatch

`CommandDispatcher::Dispatch(envelope, now_unix)` implements the middleware chain:
1. Reject missing `device_fingerprint` in the envelope (401).
2. Session lookup → reject if expired/inactive (401).
3. Reject if session fingerprint is empty or does not match the envelope fingerprint (401).
4. Rate-limit via `RateLimiter::TryAcquire(session_token)` (429 + `retry_after`).
5. Route to handler by `envelope.command`.
6. Mask response via `FieldMasker` for Auditor role.
7. Handlers write audit events for mutations.

Supported commands: `kennel.search`, `booking.create`, `booking.approve`, `booking.cancel`, `inventory.issue`, `inventory.receive`, `report.trigger`, `report.status`, `export.request`, `alerts.list`, `alerts.dismiss`.

### 18.11 Updated Requirement-to-Module Table

| Requirement | Module(s) |
|---|---|
| Kennel search + ranking | `BookingService`, `KennelRepository`, `BookingSearchFilter`, `BookingRules` |
| Booking lifecycle + approvals | `BookingService`, `BookingStateMachine`, `BookingRepository` |
| Item ledger receive/issue | `InventoryService`, `InventoryRepository` |
| Duplicate-serial rejection | `InventoryService`, `InventoryRules::ValidateSerial` |
| Barcode lookup | `InventoryService`, `BarcodeHandler`, `InventoryRules::ValidateBarcode` |
| Maintenance response-time | `MaintenanceService`, `MaintenanceRepository`, `ReportPipeline` |
| Report pipeline (4 stages) | `ReportService`, `ReportStageGraph`, `ReportRepository` |
| Throttled exports | `ExportService`, `JobQueue`, `AdminRepository::CanExport` |
| Scheduler cycle detection | `SchedulerService`, `SchedulerGraph::HasCircularDependency` |
| Retention runner | `RetentionService`, `RetentionPolicy::EvaluateRetention` |
| Checkpoint persistence | `CheckpointService`, `CrashCheckpoint` |
| In-process command dispatch | `CommandDispatcher`, `AutomationAuthMiddleware`, `RateLimiter` |
| Price rule engine | `PriceRuleEngine`, `BookingService::CreateBooking` |
| Alert scanning | `AlertService`, `AlertRules::EvaluateAlerts` |
| Desktop shell + dockspace | `DockspaceShell`, `AppController`, `ShellController` |
| Keyboard shortcuts | `KeyboardShortcutHandler`, `AppController::ProcessKeyEvent` |
| Global search | `GlobalSearchController`, `DockspaceShell::RenderGlobalSearchPopup` |
| System tray badges | `TrayBadgeState`, `TrayManager` |
| Kennel board window | `KennelBoardController`, `KennelBoardView` |
| Item ledger window | `ItemLedgerController`, `ItemLedgerView` |
| Clipboard TSV export | `TableSortState::FormatTsv`, `ClipboardHelper` |
| Crash checkpoint (UI) | `CheckpointService`, `AppController::CaptureCheckpoint` |

---

## 19. Desktop Shell Architecture

### 19.1 Dockspace Layout

`DockspaceShell::Render` drives the full ImGui frame. On each call it:
1. Calls `ApplyHighDpiScaling` — scales `ImGui::GetStyle()` sizes and `FontGlobalScale` proportionally to DPI.
2. Renders a fullscreen host window with `ImGuiWindowFlags_NoDocking` that acts as the dockspace anchor.
3. Calls `ImGui::DockSpace(id, size, ImGuiDockNodeFlags_PassthruCentralNode)`.
4. Renders the menu bar, status bar, notification overlay, global search popup, and then the active business windows via `RenderBusinessWindows`.
5. Returns `false` only when the user triggers a quit action (Ctrl+Shift+L confirmed, or closes the OS window).

### 19.2 State and Controller Boundaries

All business state lives in controllers (`KennelBoardController`, `ItemLedgerController`, `ReportsController`) that are members of `AppController`. Controllers are in `shelterops_lib` — cross-platform, no ImGui dependency, fully testable in Docker CI. Views in `ShelterOpsDesk` call controller methods and read controller state; they do not hold any persistent data themselves.

`AppController::SyncShortcutContext` is called each frame to propagate the active role, active window, and edit-mode flag into `KeyboardShortcutHandler` so `IsEnabled` returns the correct value.

### 19.3 Global Search Scope and Routing

`GlobalSearchController::Search` queries four categories:
- **Kennels** — `KennelRepository::ListActiveKennels()`, substring match on name.
- **Bookings** — `AuditRepository::Query(entity_type="booking")`, substring match on description. For Auditor role the `display_text` is set to initials-only via `MaskName` and `is_masked=true`.
- **Inventory** — `InventoryRepository::ListLowStockCandidates() + ListExpirationCandidates()`, deduped, substring match on name.
- **Reports** — `AuditRepository::Query(entity_type="report")`, substring match on description.

Each result carries a `target_window` (`WindowId`) so clicking a result in the popup calls `AppController::OpenWindow + SetActiveWindow` to route the user.

### 19.4 Tray Behavior

`TrayBadgeState::Update` counts unacknowledged `AlertStateRecord` entries (`acknowledged_at == 0`) by type. The main render loop calls this every 60 s and passes the result to `TrayManager::UpdateBadge`. The tray tooltip shows "ShelterOps — N unacknowledged alert(s)".

`TrayManager::ShowBalloon` is called by `DockspaceShell` when a new high-severity alert triggers.

### 19.5 Keyboard Command Matrix

| Action | Chord | Role restriction |
|---|---|---|
| Global search | Ctrl+F | All roles |
| New record | Ctrl+N | Not Auditor |
| Edit selected | F2 | Not Auditor |
| Export table TSV | Ctrl+Shift+E | Not Auditor |
| Close active window | Ctrl+W | Requires active window |
| Logout | Ctrl+Shift+L | All roles |

`KeyboardShortcutHandler::Evaluate` returns `ShortcutAction::None` for any disabled chord so the caller never needs to check role separately.

### 19.6 Cross-Window Refresh

When `SubmitBooking` or `SubmitReceiveStock` succeeds, the controller sets `is_dirty_ = true` and `AppController::SetCrossWindowRefresh()` is called by `ProcessKeyEvent`. On the next frame `DockspaceShell::RenderBusinessWindows` checks `HasCrossWindowRefresh()`, triggers `Refresh` on the other visible controllers, then calls `ClearCrossWindowRefresh()`.

---

## 20. Secondary Operational Modules

### 20.1 Reports Studio

`ReportsController` (cross-platform, in `shelterops_lib`) manages:
- `TriggerReport(report_id, filter_json, ctx, now)` — calls `ReportService::RunPipeline` synchronously; stores the resulting `run_id` in `active_runs_`.
- `RefreshRunStatus(run_id)` — re-reads `ReportRepository::FindRun` and updates the cached status, version label, anomaly flags, and error message.
- `LoadRunsForReport(report_id)` — bulk-loads all runs for history display.
- `RequestExport(run_id, format, ctx, now)` — delegates to `ExportService::RequestExport`; returns `0` and sets `last_error_` on permission failure.

`ReportsView` (Win32 only) renders: filter bar, run history table (with colored status cells), metric snapshot table, anomaly banner (yellow text for non-empty `anomaly_flags_json`), export buttons (CSV / PDF), and an inline version-comparison table backed by `ReportService::CompareVersions`.

Dashboard metrics covered: occupancy rate, kennel turnover, maintenance response time, overdue-fee distribution, inventory summary.

### 20.2 Scheduler Panel

`SchedulerPanelController` loads `ScheduledJobRecord` list, provides `ViewJobDetail(job_id)` which populates `JobDetailView` with the job record, its `DependencyEdge` list, the pipeline stage string (`collect → cleanse → analyze → visualize` via `domain::StageName`/`NextStage`), and recent run history. `EnqueueJob` is gated to OperationsManager+ role.

`SchedulerPanelView` renders a split-pane layout: left = job list table, right = detail panel showing stage pipeline, dependency list, run history (with per-run error messages in red), and an "Enqueue Now" button for permitted roles.

### 20.3 Admin Panel

`AdminPanelController` covers five tabs via dedicated load/submit methods:

| Tab | Operations |
|---|---|
| Catalog | `LoadCatalog`, `BeginEditCatalogEntry`, `SubmitCatalogEntry` |
| Price Rules | `LoadPriceRules`, `BeginCreatePriceRule`, `SubmitPriceRule`, `DeactivatePriceRule` |
| Approval Queue | `LoadApprovalQueue`, `ApproveBooking`, `RejectBooking` |
| Retention | `LoadRetentionPolicies`, `SetRetentionPolicy` |
| Export Permissions | `LoadExportPermissions`, `SetExportPermission` |

All mutating operations require Administrator role (enforced via `AuthorizationService::RequireAdminPanel`). `ApproveBooking`/`RejectBooking` delegate to `BookingService` and require OperationsManager+.

`AdminPanelView` renders the tabbed UI. Each tab shows a read list and an edit form with inline validation banners.

### 20.4 Audit Log

`AuditLogController` wraps `AuditRepository::Query` with role gate (`RequireAuditLogAccess`) and `ExportCsv` with gate (`CanExportAuditLog`). Auditor role receives `[masked]` for `actor_user_id` and `description` fields in the export output. The controller has no write path — `AuditRepository` is append-only.

`AuditLogView` renders a 7-column table with resizable columns. Auditor role sees `[masked]` cells. The export bar (visible to Auditor + Administrator only) copies masked CSV to clipboard.

### 20.5 Alerts Panel

`AlertsPanelController` calls `AlertService::Scan` on `Refresh`, then `ListActive` to populate the display list, and updates `TrayBadgeState`. `AcknowledgeAlert` blocks Auditor role. The controller immediately removes the acknowledged alert from the local list for responsive UI without waiting for the next `Refresh`.

`AlertsPanelView` shows a badge count in the window title, a per-row "Ack" button for non-Auditor roles, and a green "No active alerts" message when the list is empty.

### 20.6 Update Manager

`UpdateManager` drives the signed `.msi` lifecycle:

1. **ImportPackage** — copies the `.msi` to `metadata_dir_/staged.msi`, calls `DoVerify`.
2. **DoVerify** — on Win32: calls `WinVerifyTrust` (WINTRUST_ACTION_GENERIC_VERIFY_V2). On non-Win32: always returns `SignatureFailed` (enables CI tests of the failure path).
3. **Apply** — saves rollback metadata (previous version, previous installer path, timestamp) to `metadata_dir_/rollback.json`, then launches `msiexec /quiet /norestart /i <staged.msi>`. Calls progress callback at 0.0 before and 1.0 after.
4. **RollbackToPrevious** — reads `rollback.json`, re-launches the previous installer path.
5. **LoadRollbackMetadata** — called in constructor; populates `rollback_available` flag from `rollback.json`.

Rollback metadata persists across process restarts so the operator can recover even if the new version crashes on first launch.

### 20.7 Diagnostics

`DiagnosticsController` provides:
- `GetWorkerStatus()` — polls `JobQueue::IsIdle()`.
- `GetDatabaseStats()` — runs `PRAGMA page_count`, `page_size`, `freelist_count`, `journal_mode`, `wal_checkpoint` to expose DB health.
- `RegisterCache(name, reporter)` — accepts a callable returning `(current_size, max_size)`.
- `Refresh()` — calls all registered reporters and updates `cache_stats_`.
- `HasLeaks()` — on MSVC debug builds with `_CRTDBG_MAP_ALLOC`: checks `_CrtMemState` for live normal-block allocations. Returns `false` on release builds and non-MSVC.

---

## 20.8 Updated Requirement-to-Module Table (Prompt 7 Additions)

| Requirement | Module(s) |
|---|---|
| Reports Studio dashboard | `ReportsController`, `ReportsView`, `ReportService` |
| Report filter + history | `ReportsController::LoadRunsForReport`, `ReportRepository::ListRunsForReport` |
| CSV / PDF export | `ExportService`, `ReportsController::RequestExport` |
| Version comparison | `ReportService::CompareVersions`, `ReportRepository::ListSnapshotsForRun` |
| Anomaly surfacing | `ReportsController::RefreshRunStatus`, `ReportRunRecord::anomaly_flags_json` |
| Scheduler visibility | `SchedulerPanelController`, `SchedulerPanelView` |
| Pipeline stage display | `ReportStageGraph::StageName`, `NextStage` |
| Approval queue | `AdminPanelController::ApproveBooking/RejectBooking`, `BookingService` |
| Product catalog edit | `AdminPanelController::SubmitCatalogEntry`, `AdminService::UpdateCatalogEntry` |
| Price rules | `AdminPanelController::SubmitPriceRule`, `AdminService::CreatePriceRule` |
| Retention policy edit | `AdminPanelController::SetRetentionPolicy`, `AdminService::SetRetentionPolicy` |
| Export permission edit | `AdminPanelController::SetExportPermission`, `AdminService::SetExportPermission` |
| Audit log view | `AuditLogController`, `AuditLogView` |
| Masked audit export | `AuditLogController::ExportCsv`, `AuditRepository::ExportCsv` |
| Active alert display | `AlertsPanelController`, `AlertsPanelView` |
| Alert acknowledgement | `AlertsPanelController::AcknowledgeAlert`, `AlertService::AcknowledgeAlert` |
| Tray badge update | `TrayBadgeState::Update`, `AlertsPanelController::Refresh` |
| Signed .msi import | `UpdateManager::ImportPackage`, `WinVerifyTrust` |
| Signature verification | `UpdateManager::DoVerify` |
| Rollback metadata | `UpdateManager::RollbackToPrevious`, `rollback.json` |
| Worker diagnostics | `DiagnosticsController::GetWorkerStatus` |
| Cache inspection | `DiagnosticsController::RegisterCache`, `Refresh` |
| Debug leak detection | `DiagnosticsController::HasLeaks`, `SHELTEROPS_DEBUG_LEAKS` |
