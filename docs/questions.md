# ShelterOps Desk Console — Ambiguity Log

This file records only blocker-level or implementation-shaping ambiguities.
Each entry has three sections: The Gap, The Interpretation, and Proposed Implementation.
Entries are resolved forward-movingly; new gaps are added as they surface.

---

## Q-001 — Kennel distance modeling

**The Gap**  
The original prompt describes search/filter by "distance within the facility (feet between zones)"
but does not specify how inter-zone distances are stored or computed — whether as a coordinate
system, a lookup table, or a graph of adjacency edges.

**The Interpretation**  
Each zone (e.g., "Building A / Row 3") has a fixed centroid position expressed as (x, y)
coordinates in feet from a facility-defined origin point. Distance between zones is computed
as 2D Euclidean distance between centroids on demand.

**Proposed Implementation**  
Add a `zones` table with `x_coord_ft REAL` and `y_coord_ft REAL` columns. Distance
queries compute `SQRT(POW(z1.x - z2.x, 2) + POW(z1.y - z2.y, 2))` inline or cache
the result in a `zone_distance_cache` table populated at startup. No physical map image
is required; the coordinate grid is managed through the Admin panel.

---

## Q-002 — Boarding vs. adoption scope

**The Gap**  
The prompt mentions both "adoptable listings" and "nightly price (USD) for boarding" as
kennel search attributes, but does not clarify whether a kennel can serve both boarding
(paid, time-limited stay) and adoption (animal awaiting placement) simultaneously or
sequentially without design ambiguity.

**The Interpretation**  
Kennels are multi-purpose physical units. Each kennel has a `current_purpose` field
(boarding / adoption / medical / quarantine / empty). Bookings are boarding-specific
time-bounded records. Adoptable animals are a separate entity linked to a kennel
via `current_kennel_id`. A kennel marked for adoption is not simultaneously bookable
for boarding.

**Proposed Implementation**  
Three separate tables: `kennels` (physical units with zone, capacity, restrictions,
purpose), `bookings` (boarding stays with date range, price, guest pet info), and
`adoptable_animals` (animals with adoption status, linked to a kennel). Bookability
check confirms `current_purpose = 'boarding'` and no date-overlap with existing bookings.

---

## Q-003 — Maintenance response-time metric formula

**The Gap**  
The prompt lists "maintenance response time" as a reporting dashboard metric but
provides no definition of what constitutes a response or how to measure it.

**The Interpretation**  
Response time is the elapsed wall-clock duration from when a maintenance ticket is
created to when a staff member logs the first action on it, measured in decimal hours.
"Resolved time" is a separate metric (ticket open → ticket closed).

**Proposed Implementation**  
A `maintenance_tickets` table with columns `created_at INTEGER` (immutable),
`first_action_at INTEGER` (set on first status update), and `resolved_at INTEGER`
(nullable). The dashboard metric is `AVG((first_action_at - created_at) / 3600.0)`
filtered by date range, location zone, and assigned staff. NULL `first_action_at`
rows are excluded from the average and counted separately as "unacknowledged."

---

## Q-004 — Local automation endpoint protocol

**The Gap**  
The prompt specifies "API-style rate limiting and anti-bot controls apply to any local
automation endpoints/plugins" but does not name a protocol (HTTP, named pipe, IPC, etc.)
or describe the request/response shape.

**The Interpretation**  
Current implementation scope keeps automation strictly in-process via
`CommandDispatcher` and `AutomationAuthMiddleware`. HTTP transport is deferred and
treated as a future adapter only.

**Proposed Implementation**  
Use the in-process command envelope as the authoritative contract:
- Required fields: `command`, `body`, `session_token`, `device_fingerprint`
- `device_fingerprint` is mandatory and must match the active session row
- Rate limit remains 60 requests/minute per session token
- All auth failures return unauthorized envelopes and emit audit events

When a loopback HTTP adapter is later introduced, it will map HTTP headers/body to this
existing envelope and call `CommandDispatcher::Dispatch` directly, without duplicating
business logic.

---

## Q-005 — Optional LAN sync scope and conflict model

**The Gap**  
The prompt says "encryption in transit for any optional LAN file-share sync" but does
not specify what data is synced, in which direction, or how conflicts are resolved when
two stations modify the same record.

**The Interpretation**  
LAN sync is one-way outbound only in v1: the primary station exports a WAL-flushed
SQLite snapshot to a peer station. No bidirectional merge or conflict resolution is
required in this version.

**Proposed Implementation**  
The `LanSync` job handler in `WorkerRegistry` reads a local snapshot file, encrypts it
with AES-256-GCM using a per-installation key from Windows Credential Manager
(`ShelterOps/LanSyncKey`), then transmits the encrypted payload to the peer over a
**Schannel TLS 1.2/1.3 socket**. Before any payload is sent, the server certificate
SHA-256 thumbprint is validated against the pinned list in
`AppConfig::lan_sync_pinned_certs_path` (`lan_sync_trusted_peers.json` by default).
The connection is rejected if the thumbprint is not pinned. The feature is off by default
(`lan_sync_enabled = false`) and requires explicit admin configuration of
`lan_sync_peer_host`, `lan_sync_peer_port`, and `lan_sync_pinned_certs_path`.

---

## Q-006 — Update-signing trust chain

**The Gap**  
The prompt says updates are delivered as "signed .msi package" but does not specify
who holds the signing certificate or how the application establishes which signers
are trusted, beyond the standard Windows trust store.

**The Interpretation**  
Updates are signed with a standard Authenticode code-signing certificate. The application
augments Windows trust-store verification with a pinned-thumbprint check against a
`trusted_publishers.json` file shipped with the initial installation. This guards against
a compromised intermediate CA issuing a valid-but-unauthorized signature.

**Proposed Implementation**  
`UpdateManager::VerifyPackage(path)` calls `WinVerifyTrust` for the Authenticode chain,
then reads the signer's SHA-256 thumbprint from the PE/MSI signature and compares it
against the pinned list in `trusted_publishers.json`. If either check fails, the import
is rejected and an audit event is written. Rollback metadata (path to the previous .msi
and its hash) is stored in the SQLite `update_history` table.

---

## Q-007 — Retention behavior for audit-log-referenced records

**The Gap**  
The prompt specifies "delete or anonymize records after 7 years" but does not address
what happens to `audit_events` rows that reference a user or entity record that has
been purged, which would break referential context in audit exports.

**The Interpretation**  
PII fields in subject tables (name, phone, email, address) are anonymized (set to a
fixed placeholder string and the `anonymized_at` timestamp is recorded) rather than
the row being deleted. This preserves audit log referential integrity. The audit_events
table itself is never modified regardless of retention action.

**Proposed Implementation**  
`RetentionService` runs as a scheduled job. For records whose `created_at` exceeds the
configured retention threshold: if `retention_anonymize = true` (default), it NULLs or
replaces PII columns and sets `anonymized_at`. If `retention_anonymize = false`, it
deletes the record and writes a synthetic audit event noting the deletion. The
`audit_events` table is excluded from all retention operations.

---

## Q-008 — Overdue-fee distribution metric

**The Gap**  
The prompt lists "overdue-fee distribution" as a reporting dashboard metric but does
not define what constitutes an overdue fee or how the distribution is bucketed.

**The Interpretation**  
An overdue fee is a boarding-stay charge where `paid_at IS NULL` and the stay has ended
(`checkout_at < NOW()`). The distribution is a histogram of outstanding balances grouped
by age (days since the stay ended): 0–30 days, 31–60 days, 61–90 days, 91–180 days,
180+ days.

**Proposed Implementation**  
`boarding_fees` table with columns `booking_id`, `amount_cents INTEGER`, `due_at INTEGER`,
`paid_at INTEGER` (nullable). The overdue-fee distribution report queries unpaid rows
where `due_at < strftime('%s','now')`, groups them by age bucket, and aggregates count
and total amount per bucket. Multi-dimensional filters (date range, zone, staff) apply
via JOIN to the `bookings` table.

---

## Q-009 — Duplicate serial-number rejection scope

**The Gap**  
The prompt says to reject "duplicate serial numbers" for inventory items but does not
clarify whether uniqueness is scoped globally across all categories or per-category.

**The Interpretation**  
Serial numbers are globally unique across all inventory items in the facility. A barcode
is a facility-wide identifier; two items in different categories cannot share a serial.

**Proposed Implementation**  
`UNIQUE` constraint on `serial_number` in the `inventory_items` table (NULL allowed for
items without a serial). On rejection, the error message names the existing item
(category, description, location) that already owns that serial. A corresponding audit
event of type `DUPLICATE_SERIAL_REJECTED` is written including the attempted serial value.

---

## Q-010 — Export permission matrix

**The Gap**  
The prompt says exports are supported but does not specify which roles may export which
data sets in which formats (CSV vs. PDF).

**The Interpretation**  
Administrators and Operations Managers may export all report types in both CSV and PDF.
Inventory Clerks may export inventory ledger reports only. Auditors may export audit
logs in CSV only (masked PII). No role may export raw sensitive fields (decrypted values,
password hashes, session tokens).

**Proposed Implementation**  
An `export_permissions` configuration table maps (role, report_type) to
(csv_allowed BOOLEAN, pdf_allowed BOOLEAN). The table is seeded with the defaults above
and is editable by Administrators. `ExportService` enforces this check before generating
output. Violations are logged as audit events of type `EXPORT_UNAUTHORIZED`.

---

## Q-011 — Booking approval trigger condition

**The Gap**  
The prompt mentions "approvals" and "approval requests" but does not specify when a
booking requires approval versus being auto-approved on creation.

**The Interpretation**  
Approval is required for all new boarding bookings by default. Configuring approval as
optional is an Administrator action. Operations Manager and Administrator can approve.
Inventory Clerk and Auditor cannot approve bookings.

**Proposed Implementation**  
`system_policies['booking_approval_required']` (default `true`). When true: new bookings
start with `status='pending'` and a `booking_approvals` row with `decided_at=NULL` is
inserted atomically in the same transaction. When false: bookings created by eligible
roles go directly to `status='approved'` with no approval row. `CanApproveBooking(role)`
gates the approval action in `BookingService`.

---

## Q-012 — Kennel capacity semantics

**The Gap**  
The prompt states "capacity limits" as part of bookability checks but does not clarify
whether capacity is the maximum number of simultaneous animals in a kennel unit (e.g., a
large suite can hold two small dogs) or simply a 1-per-kennel rule.

**The Interpretation**  
Capacity is the maximum number of simultaneously active bookings for one kennel unit.
Most kennels have `capacity=1`. A large suite or multi-animal enclosure may have
`capacity=2` or higher. The bookability check is:
`CountOverlappingBookings(kennel_id, window) >= kennel.capacity` → not bookable.

**Proposed Implementation**  
`kennels.capacity` column (INTEGER, default 1, CHECK > 0). `BookingRules::EvaluateBookability`
calls `CountOverlappingBookings` and compares against `kennel.capacity`. The capacity
is configurable per kennel from the Admin Panel.

---

## Q-013 — Report version label format

**The Gap**  
The schema has a `version_label` column on `report_runs` but the prompt does not define
how version labels are generated or what format they must follow.

**The Interpretation**  
Version labels are system-generated, non-editable, and follow the pattern
`<report_type>-<YYYYMMDD>-<sequence>`, e.g., `occupancy-20260420-003`. The sequence is
the count of prior runs for the same report_id on the same calendar day, zero-padded
to 3 digits.

**Proposed Implementation**  
`ReportService::GenerateVersionLabel(report_id, report_type, started_at)` queries
`SELECT COUNT(*) FROM report_runs WHERE report_id=? AND DATE(started_at,'unixepoch')=DATE(?,unixepoch)`
and formats the label. Collision within the same second is protected by the SQLite
UNIQUE constraint on `(report_id, version_label)` — the second attempt would retry
with an incremented sequence.

---

## Q-014 — Circular job dependency detection

**The Gap**  
The `job_dependencies` table can express directed edges between jobs. The prompt does not
address how circular dependency cycles (A depends on B depends on A) are detected or
rejected.

**The Interpretation**  
Circular dependencies would cause `SchedulerService` to deadlock waiting for a job that
can never complete. They must be detected at insert time and rejected with a clear error.

**Proposed Implementation**  
Before inserting a `job_dependencies` row, `SchedulerService` performs a reachability
check: starting from `depends_on_job_id`, follow all existing dependency edges; if
`job_id` is reachable, the insertion is rejected with error `CIRCULAR_JOB_DEPENDENCY`
and the attempt is written to the audit log.

---

## Q-015 — Credential vault provisioning on non-Windows

**The Gap**  
`CredentialVault` uses Windows DPAPI + Credential Manager on Windows. The Docker CI
environment is Linux, where DPAPI is unavailable.

**The Interpretation**  
The non-Windows path is a file-based fallback: keys are stored as raw binary files in
a `.shelterops_vault/` directory with `0600` permissions. This fallback has a weaker
security posture than DPAPI (no hardware-backed protection, no OS user binding) and is
**for CI use only**. The production deployment target is Windows 11; the Linux path
exists only to allow `shelterops_lib` to compile and tests to run in Docker.

**Proposed Implementation**  
`CredentialVault` logs a `spdlog::warn` at startup on non-Windows platforms. The vault
interface is abstract (`ICredentialVault`) so tests use an `InMemoryCredentialVault`
injected at construction time — no filesystem or registry access in tests.

---

## Q-016 — Initial administrator provisioning (first-boot)

**The Gap**  
The prompt requires no hardcoded credentials. An empty users table must bootstrap
the first admin without hardcoded passwords or special bypass roles.

**The Interpretation**  
On first launch (users table is empty), the application enters a one-time setup mode
where the operator is prompted to create the initial Administrator account interactively.
The "setup" flow is available only when `UserRepository::IsEmpty()` returns true; once
the first admin exists, the setup path is permanently unavailable.

**Proposed Implementation**  
`AuthService::CreateInitialAdmin(username, password, display_name, now_unix)` creates the
first admin only when the users table is empty (enforced by `UserRepository::IsEmpty()` check).
The application detects first-run in `ShellController` and shows a setup dialog instead of
the normal login form. No temporary role or backdoor account is used. Password minimum
length is 12 characters, enforced at this entry point.

---

## Q-017 — Session lifetime for long-shift desktop usage

**The Gap**  
The prompt does not specify session expiry duration. Animal shelter operations typically
involve 8–12 hour shifts; a short session would require repeated logins mid-shift.

**The Interpretation**  
Session lifetime defaults to **12 hours** from creation, with a sliding **1-hour inactivity
timeout**. Each successful API or GUI interaction refreshes the inactivity timer. The
12-hour ceiling prevents indefinitely long sessions even with constant activity.

**Proposed Implementation**  
`AuthService::kSessionLifetimeSec = 43200` (12h). `kInactivityTimeoutSec = 3600` (1h).
`ValidateSession` checks `now > expires_at`; the `expires_at` is refreshed in a later
prompt when the controller layer calls `ValidateSession` on each significant action.

---

## Q-018 — Optional LAN sync TLS trust model

**The Gap**  
The prompt says LAN sync uses "encryption in transit" but does not specify how the
application verifies the TLS certificate of the destination peer or how the per-installation
encryption key is provisioned.

**The Interpretation**  
The application establishes its own Schannel TLS connection to the peer (independent of
SMB or OS-level share settings) and performs application-level pinned certificate
verification. The administrator configures a `lan_sync_trusted_peers.json` file listing
the SHA-256 thumbprints of trusted sync peers; connections whose server certificate does
not match are rejected before any payload is transmitted. The per-installation AES-256-GCM
encryption key is auto-generated at first sync and stored in Windows Credential Manager
under `ShelterOps/LanSyncKey` — it never leaves the machine and is separate from the
TLS handshake.

**Implemented**  
`WorkerRegistry::RegisterAll` — `LanSync` handler opens a TCP socket, performs a Schannel
TLS 1.2/1.3 handshake, extracts the server certificate SHA-256 thumbprint via
`QueryContextAttributes(SECPKG_ATTR_REMOTE_CERT_CONTEXT)`, and rejects connections whose
thumbprint is not in the pinned list loaded from `AppConfig::lan_sync_pinned_certs_path`.
The `lan_sync_trusted_peers.json` schema matches `trusted_publishers.json`. The admin UI
requires all three TLS fields (`peer_host`, `peer_port`, `pinned_certs_path`) to be
configured before the sync feature can be enabled.

---

## Q-019 — Idempotency token for booking commands

**The Gap**  
The command envelope spec does not define how duplicate booking.create requests from
automated clients (e.g., retry on timeout) are handled when the same booking should
only be created once.

**The Interpretation**  
Callers may include an optional `idempotency_key` string in the `CreateBookingRequest`
body. If the same key is received within a 5-minute window with an identical payload,
the prior result is returned. If the same key is received with a different payload,
a `BOOKING_CONFLICT` error is returned.

**Proposed Implementation**  
`CreateBookingRequest.idempotency_key` is an optional client-supplied string.
`BookingService::CreateBooking` checks for an existing booking with the same key
and created_at within 5 minutes (300 seconds). Duplicate → return existing booking_id.
Conflicting payload → return `ErrorCode::BookingConflict`. No persistent
idempotency table required in v1; the check uses the bookings table itself.

---

## Q-020 — Throttled export queue semantics

**The Gap**  
The schema has `max_concurrency` on `export_jobs` but it is not clear whether this
is enforced by the database layer or application layer, or both.

**The Interpretation**  
`max_concurrency` on the `export_jobs` row is a documentation field (audit trail)
showing the cap that was in effect when the job was inserted. The actual enforcement
is in `JobQueue::WorkerLoop` via the in-memory `active_by_type_` counter and
`type_cap_cv_` condition variable. The database field is not the enforcement point.

**Proposed Implementation**  
`JobQueue` enforces: `ExportPdf=1`, `ExportCsv=1`, `ReportGenerate=2` concurrency limits
regardless of the `max_concurrency` value stored in `export_jobs`. This avoids TOCTOU
races that would occur if enforcement were in the database layer.

---

## Q-021 — Checkpoint form-state scope

**The Gap**  
`CrashCheckpoint` mentions "form state" but does not define which fields constitute
form state or which are excluded for privacy.

**The Interpretation**  
Checkpoint-eligible form state is limited to: active filter state (e.g., selected
category, date range), selected row keys (e.g., `"42"` for the highlighted item),
and draft text fields (e.g., a partially typed note). Excluded: passwords, session
tokens, plaintext PII, and any field matching the patterns `password`, `token`, `email`,
or `@`. The `CrashCheckpoint::ContainsPiiMarker` regex guard enforces this at write time.

**Proposed Implementation**  
`CheckpointService::CaptureState` serializes `WindowInventory` and `FormSnapshot`
structs to JSON and delegates to `CrashCheckpoint::SaveCheckpoint`. The PII guard
rejects any payload containing the above markers. UI controllers are responsible for
not including raw PII in `draft_text` or `filter_json` fields.

---

## Q-022 — Retention of recommendation_results

**The Gap**  
The `recommendation_results` table accumulates rows over time but no retention policy
is defined for it.

**The Interpretation**  
`recommendation_results` rows older than 90 days are eligible for deletion under the
retention framework. This provides an audit trail of rank rationale for 90 days
(sufficient for dispute resolution) without unbounded table growth.

**Proposed Implementation**  
`RetentionService` includes `recommendation_results` with a 90-day `delete` action
policy. The retention policy is seeded via `AdminRepository::UpsertRetentionPolicy`
with `entity_type='recommendation_results'`, `retention_years=0` (use days instead —
enforced via a special-case 90-day calculation), `action='delete'`.
In practice the cutoff is `now - 90 * 86400`; the years field is a documentation hint.


---

## Q-023 -- Signature trust chain for .msi import

**The Gap**
The signed .msi update flow requires WinVerifyTrust to validate the installer
signature, but the certificate trust chain (internal CA, EV certificate, or
self-signed) is not specified.

**The Interpretation**
The implementation calls WinVerifyTrust with WINTRUST_ACTION_GENERIC_VERIFY_V2,
which follows the Windows root certificate store. Any certificate trusted by the
OS store (including enterprise-deployed CAs) will pass. On non-Windows CI builds
DoVerify always returns SignatureFailed � this is intentional and enables testing
the failure path without needing a real certificate.

**Proposed Implementation**
UpdateManager::DoVerify uses the standard WinVerifyTrust API. The metadata_dir_
and staged.msi path are configurable at construction time so integration tests
can supply predictable paths. Tests verify the failure path by constructing a
dummy .msi on non-Windows CI.

---

## Q-024 -- Diagnostics panel visibility gating

**The Gap**
The diagnostics window surfaces internal health data (DB page counts, cache sizes,
worker status). Who should be able to view this?

**The Interpretation**
Diagnostics data (DB stats, cache sizes, worker status) is non-sensitive operational
data. The panel is accessible to Administrator role in all builds. In debug builds
(SHELTEROPS_DEBUG_LEAKS=1) it is also visible to OperationsManager for on-site
troubleshooting. Auditor and InventoryClerk roles cannot open the diagnostics panel.

**Proposed Implementation**
DiagnosticsController gating is enforced in the view layer (AdminPanelView renders
the diagnostics section only when session role is Administrator or when
SHELTEROPS_DEBUG_LEAKS is defined and role is OperationsManager). The controller
itself has no role gate since it is used by the diagnostics session only.

---

## Q-025 -- Rollback installer path after OS upgrade

**The Gap**
Rollback metadata stores the previous installer `.msi` path. After a Windows feature
update or manual cleanup, the installer file may no longer exist.

**The Interpretation**
RollbackToPrevious checks fs::exists(previous_msi_path) before launching msiexec.
If the path does not exist it returns NotFound error with an explicit message rather
than silently failing. The operator is then responsible for sourcing the previous
installer from backup storage. This is consistent with offline-only constraint.

**Proposed Implementation**
Implemented in UpdateManager::RollbackToPrevious. If the path does not exist the
function sets state=Failed, last_error_.code=NotFound, and logs a warning.

---

## Q-026 — CommandDispatcher rate-limit bucket scope

**The Gap**
`CommandDispatcher` enforces a per-session-token token-bucket rate limit (default 60
RPM). It is unspecified whether `export.request` and `report.trigger` draw from the
same session-level bucket or from a command-type-specific sub-bucket. When both are
exercised in rapid succession (as in `test_command_report_flow.cpp`), the test author
could not determine from the spec whether a burst of `report.trigger` calls would
consume rate-limit capacity for a subsequent `export.request` in the same session.

**The Interpretation**
The rate-limit bucket is scoped per session token only, not per command name. All
commands dispatched under the same session token share one bucket. This keeps the
middleware implementation simple (one `RateLimiter` entry per session) and matches the
spec language "per session token" in api-spec.md §8.2. A session that triggers many
reports in rapid succession will therefore consume capacity available to export
requests in the same window.

**Proposed Implementation**
`CommandDispatcher::Dispatch` calls `rate_limiter_.IsAllowed(session_token)` with no
command-specific key. This is the current implementation. If per-command sub-buckets are
required in a future prompt, add a `RateLimiter` keyed on `session_token + ":" + command`
alongside the session-level limiter.

---

## Q-027 — UpdateManager persistence authority

**Current State**
`UpdateManager` uses `rollback.json` in `AppConfig::update_metadata_dir` as the
authoritative rollback record. The sidecar stores the previous installer path and the
previous version inferred from the staged MSI filename so rollback can proceed without
requiring the SQLite audit tables at startup.

**Interpretation**
The migration-defined `update_packages` / `update_history` tables remain reserved for a
future richer audit trail, but the runtime contract is now explicitly documented around
`rollback.json` to avoid schema/runtime ambiguity.

**Proposed Implementation**
In a future prompt, extend `UpdateManager` to accept a `Database&` reference and insert
into `update_packages` on `ImportPackage` success and into `update_history` on `Apply`
and `RollbackToPrevious`. The `rollback.json` sidecar can then be removed once the
SQLite path is stable. Until then, `rollback.json` is the authoritative rollback record.

---

## Q-028 — Atomic booking and inventory writes

**Current State**
`InventoryRepository::IssueStockAtomic` now performs the decrement, outbound record
insert, and usage-history update inside a single `BEGIN IMMEDIATE` transaction with a
conditional quantity guard. `BookingRepository::InsertIfBookable` also performs the final
availability check and insert atomically so the write path cannot double-book a kennel
between validation and persistence.

**Interpretation**
The services still perform user-friendly pre-validation for error messages and pricing, but
the final persistence step is protected against the earlier read-then-write race window.

**Proposed Implementation**
Wrap both read-modify-write sequences in `BEGIN IMMEDIATE` transactions:
```sql
BEGIN IMMEDIATE;
SELECT quantity_on_hand FROM inventory_items WHERE item_id = ?;
-- application checks threshold
UPDATE inventory_items SET quantity_on_hand = ? WHERE item_id = ?;
COMMIT;
```
Defer to stakeholder confirmation on whether `IMMEDIATE` (writer lock at BEGIN) or
`EXCLUSIVE` (full lock) is the acceptable isolation level. Until confirmed, document as a
known race but do not change the code to avoid unreviewed locking behavior.

---

## Q-029 — RetentionService Delete action path appears unreachable

**The Gap**
`RetentionService::RunPolicy` contains a branch for `RetentionAction::Delete` that would
permanently remove rows from the target table. However, no `RetentionPolicy` instance
anywhere in the production codebase or test suite uses `RetentionAction::Delete`; all
configured policies use `RetentionAction::Anonymize`. The Delete branch is therefore dead
code at runtime.

**The Interpretation**
Either (a) Delete is intentionally disabled pending explicit stakeholder approval before
shipping destructive data removal, or (b) the branch is an implementation artifact from
early design that was never connected.

**Proposed Implementation**
If Delete is out of scope for the initial release, remove the branch and add a comment
noting it is deferred pending compliance review. If Delete is in scope, add at least one
test that constructs a `RetentionPolicy{.action = RetentionAction::Delete}`, runs
`RunPolicy`, and asserts that the targeted rows are gone from the DB while `audit_events`
rows are untouched. Awaiting stakeholder direction before either path is taken.
