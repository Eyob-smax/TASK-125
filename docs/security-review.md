# ShelterOps Desk Console - Security Review

Static review aligned to the current implementation in repo/desktop/src.

## 1. Authentication and Session Management

- Password hashing: Argon2id via libsodium in `CryptoHelper` and used by `AuthService`.
- Account lockout policy: enforced by `LockoutPolicy` + `AuthService`.
- Session lifecycle: create on login, invalidate on logout/change-password, expiry checks in auth validation and command dispatch.
- Session fingerprint binding: now machine+operator binding via `DeviceFingerprint::ComputeSessionBindingFingerprint` in shell login flow.

## 2. Authorization

- Command/service authorization uses `AuthorizationService` with role checks for write/approval/admin/export/audit paths.
- Auditor is read-only for mutation paths and receives masked output through response masking logic.
- Command dispatcher resolves caller role from `UserRepository` for each request, not hardcoded role.

## 3. Data Protection

- Local sensitive-field encryption helpers are implemented in `CryptoHelper`.
- Credential vault wrapper exists in `CredentialVault` for key storage behavior per platform.
- Crash checkpoint payloads reject known sensitive markers before persistence.

## 4. Audit Immutability and Secret Safety

- Database-level append-only guard for `audit_events` is enforced by migration triggers in `011_audit_immutability.sql`.
- Repository-level audit writes are append-only (`AuditRepository::Append`).
- Session identifiers are no longer persisted in audit rows by `AuditService`.
- Command rate-limit audit messages no longer include raw session token material.

## 5. Automation Security Surface

- In-process middleware requires session token + device fingerprint.
- Expired/inactive/missing sessions return unauthorized envelope.
- Rate limiting enforced through token bucket with retry-after.
- HTTP transport adapter remains deferred; security review scope is current in-process command/middleware path.

## 6. Known Boundaries

- Loopback HTTP adapter is not implemented in current build; only in-process dispatcher/middleware is active.
- LAN sync worker handlers are currently not implemented.
- Trusted publisher thumbprints are deployment inputs and must be provisioned in `packaging/trusted_publishers.json` before release signing workflows.

## 7. Test Coverage Highlights

- Auth/session/lockout: `unit_tests/test_auth_service.cpp`, `unit_tests/test_lockout_policy.cpp`
- Command/middleware authz and rate limits: `unit_tests/test_command_dispatcher.cpp`, `api_tests/test_command_authorization.cpp`, `api_tests/test_automation_auth_middleware.cpp`
- Audit and masking behavior: `unit_tests/test_audit_service.cpp`, `unit_tests/test_audit_repository.cpp`, `unit_tests/test_field_masker.cpp`
- Audit immutability trigger coverage: repository and migration tests assert append-only behavior.

This document reflects static implementation status only and does not claim runtime verification results.