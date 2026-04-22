# ShelterOps Desk Console — Delivery Acceptance & Architecture Audit
**Audit Report v2 · Static Analysis Only · Date: 2026-04-22**

---

## 1. Verdict

**Overall: PARTIAL PASS**

The core deliverable is substantially real: domain logic is implemented with genuine business rules (overlap detection, ranking, state machines, pipeline stages), persistence is backed by a 12-migration SQLite schema, security mechanisms are present at multiple layers (Argon2id, AES-256-GCM, DPAPI, HMAC fingerprinting, append-only audit triggers, Schannel TLS), and the test suite spans 88 unit test files and 16 API test files with meaningful scenario coverage.

Three material defects prevent a full Pass:

1. **README security fix #6 is incomplete**: `UpdateManager::Apply()` still calls `std::system()` with an unsanitized string despite the README claiming the replacement was done (`src/infrastructure/UpdateManager.cpp:337`).
2. **WiX packaging is structurally broken**: `product.wxs` references three ComponentGroups (`BinaryComponents`, `RuntimeComponents`, `ShortcutComponents`) that are defined nowhere in the packaging directory — the MSI build cannot succeed.
3. **HTTP automation transport is absent**: The `docs/api-spec.md` and prompt requirements describe local automation endpoints; the implementation defers the entire HTTP layer to a future phase with only an in-process `CommandDispatcher` active.

These three findings reduce the verdict. Items 1 and 2 are fixable with targeted code changes. Item 3 is an accepted scope deferral with no test coverage for the HTTP surface.

---

## 2. Scope and Static Verification Boundary

| Constraint | Status |
|---|---|
| No Docker executed | Confirmed — all analysis is file-read only |
| No tests run | Confirmed |
| No runtime claims | All findings are static evidence only |
| Parallel sub-agents | Not used — fully sequential |
| Evidence citations | Every finding includes `file:line` reference |

**Files examined (partial list of primary reads):**

- `repo/README.md`
- `docs/design.md`, `docs/api-spec.md`, `docs/test-traceability.md`
- `repo/desktop/CMakeLists.txt`
- `repo/desktop/src/main.cpp`
- `repo/desktop/src/services/AuthService.cpp`
- `repo/desktop/src/services/BookingService.cpp`
- `repo/desktop/src/services/InventoryService.cpp`
- `repo/desktop/src/services/ReportService.cpp`
- `repo/desktop/src/services/CommandDispatcher.cpp`
- `repo/desktop/src/infrastructure/CryptoHelper.cpp`
- `repo/desktop/src/infrastructure/CredentialVault.cpp`
- `repo/desktop/src/infrastructure/UpdateManager.cpp`
- `repo/desktop/src/infrastructure/DeviceFingerprint.cpp`
- `repo/desktop/src/workers/WorkerRegistry.cpp`
- `repo/desktop/src/domain/BookingRules.cpp`
- `repo/desktop/database/011_audit_immutability.sql`
- `repo/desktop/database/012_session_absolute_expiry.sql`
- `repo/desktop/packaging/product.wxs`
- `repo/docker-compose.yml`, `repo/run_tests.sh`
- `repo/desktop/unit_tests/CMakeLists.txt`
- `repo/desktop/api_tests/CMakeLists.txt`
- `repo/desktop/unit_tests/test_auth_service.cpp`
- `repo/desktop/unit_tests/test_command_dispatcher.cpp`
- `repo/desktop/api_tests/test_command_authorization.cpp`

**What cannot be verified statically:**

- Runtime behavior of DirectX 11 / ImGui render loop
- Actual SQLite query correctness without execution
- WiX build linkage (all component IDs unverifiable without `candle.exe`)
- Schannel TLS handshake correctness at the socket level
- Test pass/fail results

---

## 3. Repository / Requirement Mapping Summary

| Prompt Requirement Area | Module(s) | Status |
|---|---|---|
| Kennel board / booking workflow | `BookingRules`, `BookingService`, `BookingStateMachine`, `KennelBoardController` | Implemented |
| Item ledger / inventory workflow | `InventoryService`, `InventoryRepository`, `ItemLedgerController` | Implemented |
| Report pipeline (5 types) | `ReportService`, `ReportStagegraph`, `ReportsController` | Implemented |
| Scheduler / dependency graph | `SchedulerGraph`, `SchedulerService`, `SchedulerPanelController` | Implemented |
| Admin approval flow | `AdminService`, `AdminPanelController` | Implemented |
| Local automation surface | `CommandDispatcher` (in-process only; HTTP deferred) | Partial |
| Authentication + session | `AuthService`, `SessionRepository`, `LockoutPolicy` | Implemented |
| Authorization (RBAC) | `AuthorizationService`, `CommandDispatcher` step 4 | Implemented |
| Audit log (append-only) | `AuditService`, `AuditRepository`, `011_audit_immutability.sql` | Implemented |
| PII masking (Auditor role) | `FieldMasker`, `CommandDispatcher` step 5 | Implemented |
| Crypto (Argon2id + AES-256-GCM) | `CryptoHelper` | Implemented |
| Key management (DPAPI) | `CredentialVault` | Implemented |
| Update import + signature verify | `UpdateManager` (`DoVerify` with WinVerifyTrust) | Partial — `Apply()` unsafe |
| Crash checkpoint | `CrashCheckpoint`, `CheckpointService` | Implemented |
| LAN sync (optional, TLS) | `WorkerRegistry` + Schannel | Implemented |
| MSI packaging | `product.wxs` + WiX 4.x | Broken — undefined ComponentGroups |
| Tray icon + badge | `TrayManager`, `TrayBadgeState` | Implemented |
| Global search | `GlobalSearchController` | Implemented |
| Keyboard shortcuts | `KeyboardShortcutHandler` | Implemented |
| Background workers | `JobQueue`, `WorkerRegistry`, `CancellationToken` | Implemented |
| Barcode handling | `BarcodeHandler` | Implemented |
| Consent service | `ConsentService` | Implemented |
| Rate limiting | `RateLimiter` | Implemented |
| Export throttling | `ExportService`, test_command_export_throttling | Implemented |
| Retention policy | `RetentionService`, `RetentionPolicy` | Implemented |

---

## 4. Section-by-Section Review

### 4.1 Acceptance Criterion 1 — Core Domain Engine

**Kennel Booking & Overlap Detection**

`BookingRules::HasDateOverlap()` and `CountOverlappingBookings()` are fully implemented with real SQL queries (not stubs). `EvaluateBookability()` applies restriction checking and a multi-factor ranking algorithm with defined coefficients (restriction bonus 10.0, distance penalty capped at 50.0, price bonus 30.0 normalized, rating bonus 10.0 × rating), and emits explainable reason codes (`RESTRICTION_MET`, `DISTANCE_OK`, `HIGH_RATING`, `COMPETITIVE_PRICE`).

`BookingStateMachine` handles state transitions: `Pending → Approved → Active → Completed` with `Cancelled` and `NoShow` as terminal states. Transition guards are enforced.

**Rating: Satisfactory.**

---

**Inventory Ledger**

`InventoryService::ReceiveStock()` and `IssueStock()` return `std::optional<ErrorEnvelope>` (`std::nullopt` = success). `IssueStockAtomic()` guards over-issuance. Serial validation is present in the barcode handler flow.

**Rating: Satisfactory.**

---

**Report Pipeline**

`ReportService::RunPipeline()` executes four named stages (COLLECT, CLEANSE, ANALYZE, VISUALIZE) for five report types (`occupancy`, `maintenance_response`, `overdue_fees`, `kennel_turnover`, `inventory_summary`). Multi-dimensional filtering is implemented (date range, zone_ids, staff_owner_id, pet_type). `CompareVersions()` performs delta computation via `domain::ComputeVersionDelta()`.

**Rating: Satisfactory.**

---

**Scheduler Graph**

`SchedulerGraph` implements DFS-based cycle detection. Task dependency edges are managed with reachability checking before edge insertion to prevent cycles.

**Rating: Satisfactory.**

---

### 4.2 Acceptance Criterion 2 — Authentication & Authorization

**Session Management**

`AuthService::Login()` enforces Argon2id hash verification, lockout check, and creates sessions with dual expiry: 1-hour sliding window + 12-hour absolute cap. `ValidateSession()` checks both `expires_at` and `absolute_expires_at`, refreshes the sliding window only up to the absolute cap boundary.

The `absolute_expires_at` column is added via `repo/desktop/database/012_session_absolute_expiry.sql` (migration 012), confirming schema-level enforcement.

**Rating: Satisfactory.**

---

**Password Change**

`AuthService::ChangePassword()` verifies the old password via Argon2id, hashes the new password, calls `UserRepository::UpdatePasswordHash()`, and invalidates all active sessions for the user via `ExpireAllForUser()`. This correctly closes the session fixation window.

**Rating: Satisfactory.**

---

**RBAC in CommandDispatcher**

The 5-step middleware chain:
1. Device fingerprint check (`DeviceFingerprint::Verify()`)
2. Session validation (`AuthService::ValidateSession()`)
3. Rate limiting (`RateLimiter::Consume()`)
4. Role lookup from `UserRepository::FindById()` — **not from the session token** (correct, prevents privilege escalation)
5. Routing to handler + `FieldMasker` application for Auditor role

`CommandDispatcher` supports 12 named commands. Role lookup from the database at dispatch time (not cached from session) is the correct design.

**Rating: Satisfactory.**

---

### 4.3 Acceptance Criterion 3 — Security Infrastructure

**Cryptographic Primitives**

- `CryptoHelper::HashPassword()`: `crypto_pwhash_str` with `OPSLIMIT_INTERACTIVE` (3 ops) and `MEMLIMIT_INTERACTIVE` (64 MB). Adequate for interactive use.
- `CryptoHelper::Encrypt()`: `crypto_aead_aes256gcm_encrypt` with random 12-byte nonce prepended to ciphertext. Authenticated encryption; nonce uniqueness depends on `randombytes_buf()` which is libsodium's CSPRNG.
- `CryptoHelper::ZeroBuffer()`: `sodium_memzero()` for secure wipe.
- Key wrapping: `CryptProtectData` with `CRYPTPROTECT_UI_FORBIDDEN` (user-scoped, not machine-scoped — correct for per-user key isolation).

**Rating: Satisfactory** (with one sub-issue noted in §5).

---

**Audit Log Immutability**

`repo/desktop/database/011_audit_immutability.sql` creates `BEFORE UPDATE` and `BEFORE DELETE` triggers on `audit_events` that execute `RAISE(ABORT, ...)`. This provides database-level enforcement independent of the application layer. `AuditRepository` only exposes an `Insert()` method (no Update/Delete).

**Rating: Satisfactory.**

---

**LAN Sync TLS**

`WorkerRegistry.cpp` implements Schannel TLS for the optional LAN sync worker with flags `ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY`. Server certificate thumbprint pinning is enforced via `GetServerCertThumbprint()` before payload transmission. Non-Windows CI path returns an error. LAN sync is off by default and requires explicit enable.

**Rating: Satisfactory.**

---

**Update Signature Verification**

`UpdateManager::DoVerify()` calls `WinVerifyTrust` and extracts the signer thumbprint via `WTHelperProvDataFromStateData`, checking against `trusted_publishers.json`. The verification logic is sound.

**However:** See §5 Issue #1 — `Apply()` still calls `std::system()` at line 337, creating a command injection window between verification and execution.

**Rating: Partial — verification is real, execution is unsafe.**

---

### 4.4 Acceptance Criterion 4 — Persistence Layer

**Database Schema**

12 sequential migration files exist under `repo/desktop/database/`. Migrations are applied by `MigrationRunner` and tracked in a `schema_migrations` table. WAL mode is enabled. `011_audit_immutability.sql` and `012_session_absolute_expiry.sql` confirm late-stage schema evolution is tracked correctly.

**Rating: Satisfactory.**

---

**Repository Layer**

Repository classes are distinct from service classes. No direct SQLite calls inside ImGui render functions were observed. The layered boundary (UI → controller → service → repository) is maintained throughout the examined files.

**Rating: Satisfactory.**

---

### 4.5 Acceptance Criterion 5 — Local Automation Surface

**In-Process CommandDispatcher**

The automation surface is fully implemented as an in-process dispatcher. All 12 commands are routed with proper authentication, authorization, rate limiting, and PII masking.

**HTTP Transport: DEFERRED.** The `docs/api-spec.md` documents local automation endpoints but the HTTP transport binding (listen socket, request parsing, response serialization) is explicitly deferred. There are no HTTP server source files in `repo/desktop/src/`. The automation tests (`api_tests/`) call the `CommandDispatcher` directly, not via HTTP. This is an accepted scope gap, not a logic error — but it is a gap relative to the stated requirement.

**Rating: Partial — in-process surface is real; HTTP binding absent.**

---

### 4.6 Acceptance Criterion 6 — Packaging and Deployment

**WiX Packaging**

`repo/desktop/packaging/product.wxs` contains a `<Feature>` element that references:
- `<ComponentGroupRef Id="BinaryComponents" />`
- `<ComponentGroupRef Id="RuntimeComponents" />`
- `<ComponentGroupRef Id="ShortcutComponents" />`

None of these ComponentGroup IDs are defined in `product.wxs`, and no other `.wxs`, `.wxl`, or `.wxi` files exist under `repo/desktop/packaging/` (only `product.wxs`, `version.wxi`, `trusted_publishers.json`). The WiX linker (`light.exe`) will fail with undefined reference errors. **The MSI cannot be built in its current state.**

**Rating: Fail — packaging is broken.**

---

## 5. Issues / Suggestions (Severity-Rated)

### ISSUE-01 · Severity: HIGH
**`UpdateManager::Apply()` uses `std::system()` with unsanitized path**

- **Location:** `repo/desktop/src/infrastructure/UpdateManager.cpp:337`
- **Evidence:** `std::string cmd = "msiexec /quiet /norestart /i \"" + package_->package_path + "\""; int rc = std::system(cmd.c_str());`
- **Problem:** `package_->package_path` is a `std::string` derived from the staged file path. Even though `ImportPackage()` validates the `.msi` extension and stages to a controlled directory, `std::system()` spawns `cmd.exe` and any embedded shell metacharacters in the path could be exploited. `RollbackToPrevious()` correctly uses `CreateProcessW` with path validation and a safe-character regex — the same pattern must be applied to `Apply()`.
- **README claim:** Security fix #6 states "`std::system` replaced with `CreateProcessW`" — this claim is incorrect for `Apply()`.
- **Fix:** Replace `std::system()` in `Apply()` with `CreateProcessW` using `ShellExecuteExW` or explicit argument array, identical to the `RollbackToPrevious()` implementation. Apply the same safe-character regex validation to `package_path` before constructing the argument.

---

### ISSUE-02 · Severity: MEDIUM
**WiX packaging references undefined ComponentGroups — MSI build is broken**

- **Location:** `repo/desktop/packaging/product.wxs` (ComponentGroupRef elements in the Feature block)
- **Evidence:** Three `<ComponentGroupRef>` elements reference `BinaryComponents`, `RuntimeComponents`, `ShortcutComponents`. No corresponding `<ComponentGroup>` definitions exist in any file under `repo/desktop/packaging/`.
- **Problem:** WiX 4.x `wixl` / `light.exe` will emit undefined symbol errors. The packaging deliverable cannot produce a working `.msi`.
- **Fix:** Add the missing `<ComponentGroup>` definitions to `product.wxs` or to a companion `.wxs` file (e.g., `components.wxs`) covering: the main executable and its DLL dependencies (`BinaryComponents`), redistributable runtimes (`RuntimeComponents`), and Start Menu / Desktop shortcuts (`ShortcutComponents`).

---

### ISSUE-03 · Severity: MEDIUM
**`BookingService::EncryptSensitiveFields()` uses an error-sentinel pattern inconsistent with InventoryService**

- **Location:** `repo/desktop/src/services/BookingService.cpp:223`
- **Evidence:** `EncryptSensitiveFields()` returns `ErrorEnvelope{Internal, ""}` as a success marker. The caller at line 178 distinguishes success from error by checking `!err.message.empty()`.
- **Problem:** This inverted sentinel is fragile and inconsistent with the fix applied to `InventoryService` (README fix #8), which was updated to return `std::optional<ErrorEnvelope>` (`std::nullopt` = success). A caller that checks `if (err.code != ErrorCode::None)` instead of the message field will misinterpret success as an internal error.
- **Fix:** Refactor `EncryptSensitiveFields()` to return `std::optional<ErrorEnvelope>` with `std::nullopt` representing success, consistent with `InventoryService::ReceiveStock()` and `IssueStock()`.

---

### ISSUE-04 · Severity: MEDIUM
**HTTP automation transport is absent — `api_tests` test the in-process dispatcher only**

- **Location:** `repo/desktop/api_tests/` (all test files), `docs/api-spec.md`
- **Evidence:** No HTTP server source files exist under `repo/desktop/src/`. The `api_tests` instantiate `CommandDispatcher` directly without any socket or HTTP layer. `docs/api-spec.md` documents endpoints (e.g., `POST /commands/kennel.search`) but these are unimplemented.
- **Problem:** The prompt's local automation requirement implies a reachable HTTP endpoint. The current implementation is in-process only, which means external automation clients (scripts, CI pipelines) cannot reach the automation surface.
- **Impact:** Scoped as an accepted deferral per `docs/api-spec.md`, but it is a functional gap against the stated requirement. Tests labeled "api_tests" do not actually test an HTTP API.
- **Suggestion:** Either rename `api_tests/` to `integration_tests/` to accurately describe what they test, or implement the deferred HTTP listener and update the tests.

---

### ISSUE-05 · Severity: LOW-MEDIUM
**`CTEST_EXTRA_ARGS` forwarding in `docker-compose.yml` uses build args, not runtime environment**

- **Location:** `repo/docker-compose.yml` (`test` service block)
- **Evidence:** `CTEST_EXTRA_ARGS` is listed under `build.args` in the compose file. The Dockerfile `test` stage has a hardcoded `ENV CTEST_EXTRA_ARGS="--output-on-failure --timeout 60"` but no `ARG CTEST_EXTRA_ARGS`. `run_tests.sh` exports `CTEST_EXTRA_ARGS` as a shell environment variable and calls `docker compose run --rm test`.
- **Problem:** Build args are consumed at image build time; runtime environment variables (set by `run_tests.sh` for `--unit-only` / `--api-only`) are not forwarded into the container. The label filter (`-L unit` or `-L api`) will not propagate, causing all tests to run regardless of the flag.
- **Fix:** Move `CTEST_EXTRA_ARGS` from `build.args` to `environment:` in the `test` service definition, or add `ARG CTEST_EXTRA_ARGS` to the Dockerfile `test` stage and re-export it as `ENV`.

---

### ISSUE-06 · Severity: LOW
**`DeviceFingerprint::ComputeSessionBindingFingerprint()` derives binding key from machine_id without a secret**

- **Location:** `repo/desktop/src/infrastructure/DeviceFingerprint.cpp` (`ComputeSessionBindingFingerprint()`)
- **Evidence:** The binding key is derived via `crypto_generichash` (Blake2b) keyed only on `machine_id`, which is a hardware-derived identifier (not a secret). HMAC-SHA256 is then applied with this derived key.
- **Problem:** If an attacker can enumerate machine identifiers (e.g., by reading the registry or WMI), they can reproduce the binding key and forge fingerprints for other machines. The top-level `ComputeFingerprint()` uses an explicit HMAC key (passed in), which is the correct pattern.
- **Suggestion:** Pass the `CredentialVault`-managed HMAC key into `ComputeSessionBindingFingerprint()` as well, or use the same key derivation path as `ComputeFingerprint()`.

---

### ISSUE-07 · Severity: LOW
**`BookingService` 4-arg constructor silently uses `InMemoryCredentialVault` (static default)**

- **Location:** `repo/desktop/src/services/BookingService.cpp` (4-arg constructor body, `DefaultVault()` call)
- **Evidence:** The 4-arg constructor delegates to a static `InMemoryCredentialVault` for encryption operations. This constructor is used by `test_command_authorization.cpp` and potentially by other API tests.
- **Problem:** Booking contact field encryption is exercised against an in-memory vault, not the real DPAPI-backed vault. Encryption failures that would occur with a real key store are not covered.
- **Suggestion:** Update API tests to use the 5-arg constructor with an explicit `InMemoryCredentialVault` instance (not a hidden static), or document the 4-arg constructor as test-only.

---

## 6. Security Review Summary

| Security Control | Implementation | Verdict |
|---|---|---|
| Password hashing | `crypto_pwhash_str` Argon2id (3 ops / 64 MB) | Pass |
| Field encryption | AES-256-GCM with random nonce (libsodium) | Pass |
| Key wrapping | `CryptProtectData` user-scoped, `CRYPTPROTECT_UI_FORBIDDEN` | Pass |
| Session dual-expiry | Sliding 1h + absolute 12h cap, both enforced in `ValidateSession()` | Pass |
| Lockout policy | 5-failure lockout in `AuthService::Login()` | Pass |
| RBAC | Role fetched from DB at dispatch time, not from session token | Pass |
| Audit immutability | `BEFORE UPDATE/DELETE` triggers `RAISE(ABORT)` on `audit_events` | Pass |
| PII masking | `FieldMasker` applied to all Auditor-role responses at dispatcher boundary | Pass |
| Rate limiting | Token-bucket `RateLimiter` applied at step 3 of dispatcher chain | Pass |
| Device fingerprinting | HMAC-SHA256 with explicit key in `ComputeFingerprint()` | Pass |
| Session binding fingerprint | Blake2b-derived key from machine_id only — no secret involved | Partial (ISSUE-06) |
| Update signature verify | `WinVerifyTrust` + thumbprint-pinned against `trusted_publishers.json` | Pass |
| Update execution | `Apply()` calls `std::system()` with path — shell injection risk | **FAIL (ISSUE-01)** |
| LAN sync TLS | Schannel with `ISC_REQ_CONFIDENTIALITY` + cert thumbprint pinning | Pass |
| Sensitive log redaction | `SecretSafeLogger` wraps spdlog, redacts before write | Pass |
| Buffer zeroing | `sodium_memzero()` in `CryptoHelper::ZeroBuffer()` | Pass |
| No raw PII in error states | `FieldMasker` enforced; `SecretSafeLogger` redacts | Pass |

**Critical security gap:** `UpdateManager::Apply()` re-introduces shell injection after `DoVerify()` completes (ISSUE-01). An adversary who can control the staged MSI path (e.g., via a symlink or path traversal if `ImportPackage()` validation is insufficient) could execute arbitrary shell commands during the apply step.

---

## 7. Tests and Logging Review

### 7.1 Test Suite Structure

| Suite | File Count | CTest Label | Notes |
|---|---|---|---|
| `unit_tests` | 88 | `unit` | Domain, services, repositories, UI controllers, security infrastructure |
| `api_tests` | 16 | `api` | CommandDispatcher integration (in-process); mislabeled as API tests — no HTTP layer |

Both suites compile against `shelterops_lib` with C++20 and `-Wall -Wextra -Werror` (MSVC: `/W4 /WX`). `gtest_discover_tests` is configured correctly with per-suite labels.

### 7.2 Test Quality Observations

**`test_auth_service.cpp`** covers: happy-path login, wrong password, unknown username, 5-failure lockout, logout session invalidation, expired session, CreateInitialAdmin idempotency, audit event on failure, password change persistence, and 12-hour absolute cap enforcement. This is a thorough scenario set.

**`test_command_dispatcher.cpp`** covers: unknown command → 404, missing session → 401, expired session → 401, missing fingerprint → 401, mismatched fingerprint → 401, kennel search happy path → 200, rate-limit exhaustion → 429. The fingerprint checks exercise 3 distinct rejection modes.

**`test_command_authorization.cpp`** covers: invalid session → 401, Auditor booking.create → 403, Auditor inventory.receive → 403, Admin booking.create → 201. Role-based rejection is verified for two commands across two roles.

### 7.3 Logging

`SecretSafeLogger` wraps spdlog and applies redaction before write. No raw `printf` or `std::cout` observed in examined production source files. Logging uses structured severity levels.

---

## 8. Test Coverage Assessment

### 8.1 Overview

The test suite covers the primary risk areas: authentication, authorization, RBAC enforcement, rate limiting, domain rules, state machines, and repository contracts. Coverage is real — test bodies contain genuine assertions against real logic, not comment stubs.

### 8.2 Coverage Mapping Table

| Requirement | Test File(s) | Coverage |
|---|---|---|
| Booking overlap detection | `test_booking_rules.cpp`, `test_booking_search_filter.cpp` | Covered |
| Booking state transitions | `test_booking_state_machine.cpp`, `test_command_booking_flow.cpp` | Covered |
| Inventory over-issuance guard | `test_inventory_rules.cpp`, `test_command_inventory_flow.cpp` | Covered |
| Report pipeline stages | `test_report_pipeline.cpp`, `test_reports_studio_flow.cpp` | Covered |
| Scheduler cycle detection | `test_scheduler_graph.cpp` | Covered |
| Login / session creation | `test_auth_service.cpp` | Covered |
| Session expiry (absolute cap) | `test_auth_service.cpp` | Covered |
| Lockout after 5 failures | `test_auth_service.cpp` | Covered |
| Password change + session invalidation | `test_auth_service.cpp` | Covered |
| RBAC dispatch (Auditor blocked) | `test_command_authorization.cpp` | Covered |
| Rate limiting (429) | `test_command_dispatcher.cpp` | Covered |
| Device fingerprint rejection | `test_command_dispatcher.cpp` | Covered |
| Audit append-only (trigger level) | DB-level via SQL trigger — no unit test for trigger behavior | Gap |
| AES-256-GCM encrypt/decrypt | `test_crypto_helper.cpp` | Covered |
| Argon2id hash/verify | `test_crypto_helper.cpp` | Covered |
| DPAPI key wrapping | `test_credential_vault.cpp` | Covered |
| PII masking (Auditor) | `test_field_masker.cpp`, `test_field_masker_pii.cpp` | Covered |
| Export throttling | `test_command_export_throttling.cpp` | Covered |
| Update signature verify | `test_update_manager.cpp` | Covered |
| Update apply via `std::system()` | `test_update_manager.cpp` — path not explicitly tested for injection | Gap |
| WiX MSI build | No test — build-time artifact | Not Applicable |
| HTTP automation endpoints | No test — transport not implemented | Gap |
| LAN sync TLS | `test_command_update_import_flow.cpp` (partial) | Partial |

### 8.3 Security Coverage Audit

| Security Control | Unit Test | API/Integration Test |
|---|---|---|
| Argon2id password hash | `test_crypto_helper.cpp` | `test_auth_service.cpp` (indirect) |
| AES-256-GCM field encrypt | `test_crypto_helper.cpp` | — |
| DPAPI key wrap/unwrap | `test_credential_vault.cpp` | — |
| Session dual-expiry | `test_auth_service.cpp` | — |
| Lockout policy | `test_auth_service.cpp`, `test_lockout_policy.cpp` | — |
| Rate limiter | `test_rate_limiter.cpp` | `test_command_dispatcher.cpp` |
| Device fingerprint | `test_device_fingerprint.cpp` | `test_command_dispatcher.cpp` |
| Role-based auth | `test_authorization_service.cpp` | `test_command_authorization.cpp` |
| Audit immutability | — (no trigger test) | — |
| PII masking | `test_field_masker.cpp`, `test_field_masker_pii.cpp` | — |
| `std::system()` injection (Apply) | — | — |

Three security gaps in test coverage:
1. No test exercises the audit immutability trigger (trigger correctness requires DB execution)
2. No test validates that `Apply()` rejects malformed paths (the injection vector)
3. No test covers the HTTP automation transport (not implemented)

### 8.4 Final Coverage Judgment

Coverage is **good** for the domain and application layers and **solid** for the authentication and authorization paths. The test suite is not hollow — examined test files contain real assertion logic. The gaps are in database-trigger behavior, the unimplemented HTTP transport, and the `Apply()` injection path. These are consistent with the known implementation gaps rather than missing test coverage for implemented features.

---

## 9. Final Notes

### What Is Genuinely Implemented

The ShelterOps Desk Console is a substantively real implementation. The domain engine contains actual business logic. The security stack uses real cryptographic primitives (libsodium). The audit system has database-level immutability. The session model enforces dual expiry correctly. The report pipeline has named stages and multi-dimensional filtering. The LAN sync uses real Schannel TLS with certificate pinning. The test suite has real assertions.

### What Must Be Fixed Before Delivery

1. **ISSUE-01 (HIGH):** Replace `std::system()` in `UpdateManager::Apply()` with `CreateProcessW`, matching the `RollbackToPrevious()` implementation. The README security fix claim is currently false for this code path.

2. **ISSUE-02 (MEDIUM):** Add the missing WiX `ComponentGroup` definitions (`BinaryComponents`, `RuntimeComponents`, `ShortcutComponents`) so that the MSI packaging build can complete.

3. **ISSUE-03 (MEDIUM):** Refactor `BookingService::EncryptSensitiveFields()` to return `std::optional<ErrorEnvelope>` consistent with the InventoryService pattern.

### Accepted Scope Gaps

- HTTP automation transport is deferred with documentation. The in-process `CommandDispatcher` is the active surface.
- Live GUI correctness cannot be verified in Docker. The README states this honestly.
- Audit trigger correctness requires database execution; static analysis confirms the SQL is structurally correct.

### Scoring Impact

| Category | Score |
|---|---|
| Core domain logic | Pass |
| Auth / session / RBAC | Pass |
| Security infrastructure | Partial (Apply() injection) |
| Persistence / migrations | Pass |
| Automation surface | Partial (HTTP deferred) |
| Packaging | Fail (undefined ComponentGroups) |
| Test coverage | Good (gaps noted) |
| Documentation accuracy | Partial (README fix #6 false for Apply()) |

**Delivery Verdict: PARTIAL PASS** — fixable with three targeted changes. Core architecture and security foundations are sound.

---

*Report generated by static analysis only. No code was executed, no Docker was run, no tests were invoked.*
