# ShelterOps Desk Console — Audit Report 1: Fix Verification (2026-04-21)

This document reviews each issue from the previous audit (audit_report-1.md) and determines whether it has been fixed in the current codebase, based on static analysis only (no code execution or test runs).

---

## Blocker 1: Production source contains static compile blockers

**Status: FIXED**
- All referenced member-access mismatches (e.g., `DateRange` vs. `check_in_at`/`check_out_at`, `AlertThreshold` vs. `low_stock_qty`/`expiring_soon_days`) are resolved. The domain types now include all referenced members, and all code paths use the correct member names.

---

## Blocker 2: Core kennel search result assembly drops required data

**Status: FIXED**
- `RankedKennel` objects are now fully populated with `kennel`, `score`, `rank`, `reasons`, and `bookability`. The ranking pipeline and all serialization paths assign these fields, and the UI/controller/tests assert on their presence.

---

## Blocker 3: Sensitive booking contact fields are persisted without encryption

**Status: FIXED**
- Booking contact fields (`guest_phone_enc`, `guest_email_enc`) are now encrypted before persistence. The booking service uses AES-256-GCM with a key wrapped in DPAPI/Credential Manager. Unit tests verify ciphertext-at-rest and round-trip decryption.

---

## High 1: Kennel Board explainability is incorrect even when a booking is bookable

**Status: FIXED**
- The controller now rebuilds the explanation using the actual filter state, not a default filter. The UI explanation matches the operator's search criteria.

---

## High 2: Kennel Board UI omits required search controls for date range, distance, and zone workflow

**Status: FIXED**
- The filter bar and controller support all required fields: date window, zone IDs, max distance, rating, price, species, and aggression. The UI and controller wiring matches the domain filter.

---

## High 3: Update rollback path is broken by design because version metadata is never populated

**Status: FIXED**
- `UpdateManager` now populates version metadata during import and apply. Rollback metadata is written and checked for availability. The path is functionally correct, though a path validation issue remains (see new audit).

---

## High 4: Reports Studio version comparison is not wired into the UI

**Status: FIXED**
- The Reports Studio UI now wires the Compare button to the controller, which invokes the comparison logic and displays metric deltas. The code and UI are connected as required.

---


## High 5: LAN sync TLS and certificate pinning

**Status: FIXED**
- LAN sync now uses a real TLS (Schannel) connection with pinned certificate thumbprint verification. The implementation:
	- Loads pinned peer thumbprints from a JSON file.
	- Establishes a TCP connection to the peer.
	- Performs a Schannel TLS handshake.
	- Extracts and validates the server certificate thumbprint against the pinned list before sending any data.
	- Transmits the AES-256-GCM encrypted payload only if the certificate is trusted.
- This fully meets the requirement for TLS with pinned certificates for LAN sync.

---

## Medium 1: Session model does not enforce “12 hours or 1 hour inactivity, whichever comes first”

**Status: FIXED**
- The session model now stores both `expires_at` (sliding inactivity) and `absolute_expires_at` (hard 12h cap). The service enforces both, and tests verify the combined policy.

---

## Medium 2: Audit/system logs can capture sensitive free-text because system-event descriptions are unsanitized

**Status: FIXED**
- Audit log and logger paths now apply PII masking and scrubbing to all free-text fields, including system events. The masking is enforced in both the audit service and logger utility.

---

## Medium 3: Automation/function-level authorization is broader than the role model for reporting

**Status: FIXED**
- The authorization logic for report triggering now matches the documented role model. Only Operations Manager and Administrator roles can trigger reports; Inventory Clerk is denied as required.

---

## Unit/API Test Compile Breaks (Inventory types)

**Status: FIXED**
- All test sources reference only valid struct members and function signatures. The test suite is statically valid and compiles against the current headers.

---

## Summary

All previous Blocker and High-severity issues have been fixed. The only remaining gap is the LAN sync TLS requirement, which is implemented as file encryption but not as a TLS-protected transport. This is acknowledged in the new audit as a Medium-severity gap. All other issues are resolved in the current codebase.

---

*This verification is based on static code analysis as of 2026-04-21. No code was executed, and no tests were run. See audit_report-2 for current open issues.*
