# ShelterOps Desk Console — Local Automation API Specification

This document describes only the internal and local automation contracts of the
ShelterOps Desk Console. There is no hosted backend, no cloud API, and no external
web service. The application is offline-first and all surfaces described here are
loopback-only or in-process.

---

## 1. Overview

The application defines two automation surfaces, but only one is currently active:

| Surface | Current State | Protocol | Auth |
|---|---|---|---|
| In-process command dispatcher | **Active** | In-process (`CommandDispatcher`) | Session token + device fingerprint |
| Local loopback HTTP endpoint | **Deferred (not started)** | Planned HTTP/1.1 on 127.0.0.1 | Planned session token + device fingerprint |
| LAN sync export | **Manual Verification Required** | Optional administrator-managed export | Shared key in OS Credential Store |

Only the in-process dispatcher is active in the current build. The loopback HTTP
transport remains deferred and is documented here as a planned adapter contract.
Neither planned network surface is reachable from outside the local machine unless
the operator configures unsupported external routing.

---

## 2. Planned Local Loopback Automation Endpoint (Deferred)

### 2.1 Activation (Planned)

When implemented, set `automation_endpoint_enabled: true` and optionally
`automation_endpoint_port` (default 27315) in `shelterops.json`. The endpoint will
bind to `127.0.0.1:<port>` only and will never bind to `0.0.0.0`.

### 2.2 Authentication

Every request must include two headers:

```
X-Session-Token: <token>
X-Device-Fingerprint: <fingerprint>
```

`X-Session-Token` is the bearer token (session UUID v4) issued when an operator logs in
via the GUI. Session lifetime defaults to **12 hours** from creation; the session is
refreshed on activity. An inactivity timeout of **1 hour** applies: if no authenticated
action occurs within 1 hour, the session expires. Logout (GUI menu or automation endpoint)
immediately invalidates the session.

On account lockout (5 failed login attempts → 15-minute lock; escalates to 1 hour on
further failures), the session token remains invalid for all requests until the lock expires.

`X-Device-Fingerprint` is a deterministic SHA-256 fingerprint over
`machine_guid + ":" + operator_username` computed by `DeviceFingerprint` and stored with the
session at login time. This binds tokens to both machine identity and operator account,
preventing replay on a different machine or under a different operator context.

Requests missing either header receive `401 Unauthorized`.
Requests with a mismatched fingerprint receive `401 Unauthorized`.

### 2.3 Rate Limiting

The default limit is 60 requests/minute per session token (configurable via
`automation_rate_limit_rpm` in config). The rate limiter uses a **token-bucket** algorithm:
- Bucket capacity = rpm (full minute's worth of burst)
- Refill rate = rpm / 60 tokens per second (continuous)
- On bucket empty: returns 429 with `Retry-After` set to seconds until one token refills

Exceeding the limit returns:

```
HTTP 429 Too Many Requests
Retry-After: <seconds>
```

Rate-limit state is held in memory per session token and resets when the application
restarts or the session is evicted on logout.

### 2.4 Request/Response Envelope

All requests use `Content-Type: application/json`. All responses use
`Content-Type: application/json`.

**Success envelope:**
```json
{
  "ok": true,
  "data": { ... }
}
```

**Error envelope:**
```json
{
  "ok": false,
  "error": {
    "code": "ITEM_NOT_FOUND",
    "message": "No inventory item with id 42"
  }
}
```

### 2.5 Endpoint Groups

#### 2.5.1 Health

| Method | Path | Auth Required | Description |
|---|---|---|---|
| GET | /health | No | Returns application version and uptime |

**GET /health — response:**
```json
{
  "ok": true,
  "data": {
    "version": "1.0.0",
    "uptime_seconds": 3612,
    "db_path": "shelterops.db",
    "alerts_pending": 3
  }
}
```

#### 2.5.2 Alerts

| Method | Path | Auth Required | Role |
|---|---|---|---|
| GET | /alerts | Yes | Any authenticated |
| POST | /alerts/{id}/dismiss | Yes | operations\_manager, administrator |

**GET /alerts — response:**
```json
{
  "ok": true,
  "data": {
    "low_stock": [
      { "item_id": 7, "name": "Dog Food 25lb", "quantity": 2, "threshold_days": 7 }
    ],
    "expiring_soon": [
      { "item_id": 14, "name": "Flea Treatment", "expires_at": "2026-05-01", "days_remaining": 11 }
    ]
  }
}
```

#### 2.5.3 Export / Report Trigger

| Method | Path | Auth Required | Role |
|---|---|---|---|
| POST | /reports/trigger | Yes | operations\_manager, administrator |
| GET | /reports/status/{run\_id} | Yes | any authenticated |

**POST /reports/trigger — request:**
```json
{
  "report_type": "occupancy",
  "date_from": "2026-01-01",
  "date_to": "2026-03-31",
  "format": "csv",
  "filters": {
    "zone": "Building A",
    "pet_type": "dog"
  }
}
```

**POST /reports/trigger — response:**
```json
{
  "ok": true,
  "data": {
    "job_id": "a1b2c3d4",
    "queued_at": "2026-04-20T14:30:00Z",
    "estimated_seconds": 5
  }
}
```

**GET /reports/status/{run\_id} — response:**
```json
{
  "ok": true,
  "data": {
    "run_id": 42,
    "status": "completed",
    "output_path": "exports/occupancy_2026-01-01_2026-03-31.csv",
    "completed_at": "2026-04-20T14:30:05Z"
  }
}
```

Possible `status` values: `queued`, `running`, `completed`, `failed`.

#### 2.5.4 Update Import

| Method | Path | Auth Required | Role |
|---|---|---|---|
| POST | /update/import | Yes | administrator |

**POST /update/import — request:**
```json
{
  "msi_path": "C:\\Updates\\ShelterOpsDesk-1.1.0.msi"
}
```

**POST /update/import — response:**
```json
{
  "ok": true,
  "data": {
    "verified": true,
    "signer_thumbprint": "AA:BB:CC:...",
    "from_version": "1.0.0",
    "to_version": "1.1.0",
    "rollback_available": true
  }
}
```

On signature verification failure:
```json
{
  "ok": false,
  "error": {
    "code": "SIGNATURE_INVALID",
    "message": "Signer thumbprint not in trusted_publishers.json"
  }
}
```

---

## 3. Error Codes Reference

| Code | HTTP Status | Meaning |
|---|---|---|
| UNAUTHORIZED | 401 | Missing or invalid session token / fingerprint |
| FORBIDDEN | 403 | Authenticated but insufficient role |
| NOT_FOUND | 404 | Requested resource does not exist |
| RATE_LIMITED | 429 | Exceeded requests/minute limit |
| INVALID_INPUT | 400 | Malformed request body or invalid field values |
| SIGNATURE_INVALID | 400 | Update package signature check failed |
| ITEM_NOT_FOUND | 404 | Specific inventory item not found |
| BOOKING_CONFLICT | 409 | Booking date range overlaps existing stay |
| EXPORT_UNAUTHORIZED | 403 | Role does not have export permission for this report type |
| INTERNAL | 500 | Unexpected server-side error (details not exposed) |

---

## 4. LAN Sync Contract

LAN sync is off by default. When enabled by an Administrator, all data is transferred over
a **Schannel TLS connection with pinned peer certificate verification** — no data leaves
the local process until TLS has been established and the server certificate thumbprint
validated against the configured pinned list.

### Configuration fields (AppConfig)

| Field | Type | Default | Description |
|---|---|---|---|
| `lan_sync_enabled` | bool | `false` | Must be `true` to allow sync jobs to be enqueued |
| `lan_sync_peer_host` | string | `""` | Hostname or IP of the receiving sync peer |
| `lan_sync_peer_port` | int | `27316` | TLS port on the receiving peer |
| `lan_sync_pinned_certs_path` | string | `"lan_sync_trusted_peers.json"` | Path to JSON file listing trusted peer cert SHA-256 thumbprints |

### Transport sequence

1. The LAN sync worker reads a locally prepared SQLite snapshot file.
2. The snapshot is encrypted with AES-256-GCM using a per-installation key fetched from
   Windows Credential Manager (`ShelterOps/LanSyncKey`).
3. A TCP connection is opened to `lan_sync_peer_host:lan_sync_peer_port`.
4. A Schannel TLS 1.2/1.3 handshake is performed. The server certificate SHA-256
   thumbprint is extracted via `QueryContextAttributes(SECPKG_ATTR_REMOTE_CERT_CONTEXT)`
   and validated against the thumbprints listed in `lan_sync_pinned_certs_path`.
   If the thumbprint is not in the pinned list, the connection is torn down and the job
   fails — no payload data is sent.
5. The AES-256-GCM encrypted payload is transmitted via Schannel `EncryptMessage`.
6. The TLS session is gracefully shut down via `ApplyControlToken(SCHANNEL_SHUTDOWN)`.

The pinned-certificate list uses the same JSON schema as `trusted_publishers.json`:
```json
{ "trusted_peers": [ { "thumbprint": "AA:BB:CC:..." } ] }
```

No merge or conflict resolution is performed on the receiving side; the transmitted
snapshot is read-only archival.

---

## 5. Update Package Import Contract

Update packages are standard Windows Installer `.msi` files produced by WiX Toolset 4.x
and code-signed with an Authenticode certificate.

Import sequence:
1. User selects `.msi` via GUI or via the `/update/import` automation endpoint.
2. `UpdateManager::ImportPackage` copies the file to `<update_metadata_dir>/staged.msi`
   and calls `WinVerifyTrust` (full Authenticode chain). On non-Windows CI, verification
   always returns `SignatureFailed` to enable testing the failure path.
3. The signer thumbprint is extracted and matched against `trusted_publishers.json`
   (path configured by `AppConfig::trusted_publishers_path`).
4. If both checks pass, the previous installer path and inferred previous version are saved to
   `<update_metadata_dir>/rollback.json` for rollback recovery.
5. The update installer is launched with `msiexec /quiet /norestart`; the application
   shuts down cleanly after writing a crash checkpoint.
6. On the next launch, the migration runner applies any new `.sql` files in `database/`.

**Schema note:** `update_packages` and `update_history` tables exist in migration
`009_update_management.sql` for full audit trail. `UpdateManager` currently persists
rollback state in `rollback.json` only; writing to these SQLite tables is tracked in
`docs/questions.md` Q-027.

Rollback: the Administrator can trigger rollback from the Admin panel, which re-launches
the previous `.msi` recorded in `rollback.json` with `msiexec /quiet /norestart`.

---

## 6. Secret-Safe Response Contract

All error responses from the local automation endpoint must comply with this contract:

1. Error payloads never contain: session tokens, passwords, password hashes, encryption keys, HMAC keys, decrypted PII, or stack traces.
2. The `message` field in an error envelope is a safe, user-facing description — not an internal exception message.
3. Internal errors (`INTERNAL`, HTTP 500) return only: `{"ok":false,"error":{"code":"INTERNAL","message":"An unexpected error occurred."}}` — no detail beyond that.
4. Rate-limit responses include a `retry_after` field (seconds) alongside the error envelope.
5. Unauthorized responses do not indicate whether the token was missing, expired, or mismatched — all map to `UNAUTHORIZED` to prevent enumeration.

This contract is enforced for in-process automation by
`AutomationAuthMiddleware::BuildErrorResponse` and shared envelope utilities, verified in
`api_tests/test_automation_auth_middleware.cpp` and `api_tests/test_error_envelope_contract.cpp`.

---

## 7. In-Process Command Contracts (Non-HTTP)

The job scheduler uses an in-process command/event pattern (not HTTP). Controllers
submit `JobDescriptor` objects to the `JobQueue`. Each job has:

| Field | Type | Description |
|---|---|---|
| job\_id | string (UUID) | Unique identifier |
| job\_type | enum | REPORT\_GENERATE, EXPORT\_CSV, EXPORT\_PDF, RETENTION\_RUN, ALERT\_SCAN, LAN\_SYNC |
| parameters | JSON object | Type-specific parameters |
| priority | int | 1=high, 5=low |
| max\_concurrency | int | 1 for heavy exports to prevent UI freeze |
| submitted\_at | int64 | Unix timestamp |
| submitted\_by | int64 | user\_id |

Job status transitions: `QUEUED → RUNNING → COMPLETED | FAILED`.  
Failed jobs write an audit event and surface an in-app notification. No email/SMS.

---

## §8. In-Process Command Envelope

`CommandDispatcher` is the internal contract that the future HTTP adapter will implement.
It is the authoritative definition of how automation clients communicate with the business
engine. The HTTP transport is deferred; the in-process contract is frozen.

### 8.1 Envelope and Result Shapes

**Input (`CommandEnvelope`):**
```json
{
  "command":            "kennel.search",
  "body":               { ... },
  "session_token":      "sess-abc123",
  "device_fingerprint": "sha256-of-machine-and-operator"
}
```

**Output (`CommandResult`):**
```json
{ "ok": true, "data": { ... } }
```
or on error:
```json
{ "ok": false, "error": { "code": "BOOKING_CONFLICT", "message": "..." } }
```

HTTP status codes mirror the `ErrorCode` enum values (400/401/403/404/409/429/500).

### 8.2 Middleware Chain

1. **Verify session** — `sessions` table lookup; reject expired/inactive tokens → 401.
2. **Device fingerprint** — for the in-process contract, `CommandEnvelope.device_fingerprint`
  is required; for the planned HTTP adapter, `X-Device-Fingerprint` header is required.
  Missing or mismatched values return 401.
3. **Rate limit** — token-bucket 60 RPM per session key; exceed → 429 + `retry_after`.
4. **Role lookup** — `UserRepository::FindById(session->user_id)` resolves the real user role; `ctx.role` is populated from the database, not hardcoded. Missing user → 401.
5. **Route** — dispatch to handler by `command` name.
6. **Mask** — for Auditor role, apply `FieldMasker` to response data fields.
7. **Audit** — handlers write audit events for all mutations.

### 8.3 Supported Commands

| Command | Method | Required Role | Description |
|---|---|---|---|
| `kennel.search` | read | any | Search and rank available kennels |
| `booking.create` | write | ≥InventoryClerk | Create a boarding booking |
| `booking.approve` | write | ≥OperationsManager | Approve a pending booking |
| `booking.cancel` | write | ≥InventoryClerk | Cancel a booking |
| `inventory.issue` | write | ≥InventoryClerk | Issue stock units |
| `inventory.receive` | write | ≥InventoryClerk | Receive inbound stock |
| `report.trigger` | write | ≥OperationsManager | Start a report pipeline run |
| `report.status` | read | any | Fetch one run by `run_id` or list runs by `report_id` |
| `export.request` | write | per export_permissions | Request a CSV/PDF export job |
| `alerts.list` | read | any | List active unacknowledged alerts |
| `alerts.dismiss` | write | ≥OperationsManager | Acknowledge an alert |

### 8.4 Example: kennel.search

**Request body:**
```json
{
  "check_in_at": 1745712000,
  "check_out_at": 1745798400,
  "is_aggressive": false,
  "min_rating": 3.5,
  "max_nightly_price_cents": 6000,
  "only_bookable": true
}
```

**Response:**
```json
{
  "ok": true,
  "data": [
    {
      "kennel_id": 3,
      "kennel_name": "Suite B",
      "zone_id": 1,
      "score": 0.92,
      "nightly_cents": 5500,
      "reasons": [
        {"code": "RESTRICTION_MET", "detail": "No breed restrictions conflict"},
        {"code": "COMPETITIVE_PRICE", "detail": "Nightly rate below max filter"}
      ]
    }
  ]
}
```

### 8.5 Transport Note

HTTP transport is deferred. When the loopback HTTP adapter is added, each command maps
to `POST /cmd/<command>` with the `body` as the HTTP request body, `X-Session-Token`
and `X-Device-Fingerprint` headers. The in-process `CommandDispatcher` contract is the
adapter's sole interface; no business logic lives in the HTTP layer.


---

## 9. Secondary Operator APIs (Internal Dispatch)

These operations are dispatched via `CommandDispatcher` using the same envelope format
defined in �8. They were not in the initial 10 commands but are now registered.

### 9.1 report.list_runs

**Request body:** `{"report_id": <int>}`

**Response body:** Array of `ReportJobStatus` objects:
```json
{
  "ok": true,
  "data": [
    {"run_id": 42, "status": "completed", "version_label": "occupancy-20260101-001",
     "triggered_at": 1735689600, "completed_at": 1735689650, "error_message": ""}
  ]
}
```

### 9.2 report.compare_versions

**Request body:** `{"run_id_a": <int>, "run_id_b": <int>}`

**Response body:** Array of `MetricDelta` objects:
```json
{
  "ok": true,
  "data": [
    {"metric_name": "occupancy_rate", "value_before": 0.72, "value_after": 0.81,
     "delta_absolute": 0.09, "delta_pct": 12.5}
  ]
}
```

### 9.3 admin.set_retention_policy

**Request body:** `{"entity_type": "bookings", "retention_years": 7, "action": "anonymize"}`

**Response body:** `{"ok": true, "data": {}}`

Requires Administrator role. Emits `RETENTION_POLICY_UPDATED` audit event.

### 9.4 admin.set_export_permission

**Request body:** `{"role": "operations_manager", "report_type": "occupancy", "csv_allowed": true, "pdf_allowed": false}`

**Response body:** `{"ok": true, "data": {}}`

Requires Administrator role. Emits `EXPORT_PERMISSION_UPDATED` audit event.

### 9.5 scheduler.enqueue

**Request body:** `{"job_id": <int>, "params_json": "{}"}`

**Response body:** `{"ok": true, "data": {"run_id": <int>}}`

Requires OperationsManager or Administrator role. Emits `JOB_ENQUEUED` audit event.
