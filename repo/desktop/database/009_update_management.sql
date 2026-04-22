-- Migration 009: Update package history, rollback records, and automation clients.
--
-- Update import flow:
--   1. UpdateManager verifies Authenticode chain via WinVerifyTrust
--   2. Signer thumbprint checked against trusted_publishers.json
--   3. Record inserted into update_packages
--   4. Previous version info saved in update_history for rollback
--   5. Installer launched; application shuts down after crash checkpoint write
--
-- Rollback:
--   UpdateManager reads update_history.rollback_package_id to find the prior .msi,
--   verifies its sha256, then launches it with /passive.

CREATE TABLE IF NOT EXISTS update_packages (
    package_id          INTEGER PRIMARY KEY,
    version             TEXT    NOT NULL,
    msi_path            TEXT    NOT NULL,
    msi_sha256          TEXT    NOT NULL,   -- hex SHA-256 of the .msi binary
    signer_thumbprint   TEXT    NOT NULL,   -- Authenticode signer cert thumbprint
    imported_at         INTEGER NOT NULL,   -- IMMUTABLE
    imported_by         INTEGER REFERENCES users(user_id),
    status              TEXT    NOT NULL DEFAULT 'imported' CHECK(status IN (
                            'imported','applied','rolled_back','superseded'
                        ))
);

CREATE INDEX IF NOT EXISTS idx_packages_version ON update_packages(version);
CREATE INDEX IF NOT EXISTS idx_packages_status  ON update_packages(status);

CREATE TABLE IF NOT EXISTS update_history (
    history_id          INTEGER PRIMARY KEY,
    from_version        TEXT    NOT NULL,
    to_version          TEXT    NOT NULL,
    package_id          INTEGER NOT NULL REFERENCES update_packages(package_id),
    applied_at          INTEGER NOT NULL,   -- IMMUTABLE
    applied_by          INTEGER REFERENCES users(user_id),
    -- package_id of the .msi to use if this update is rolled back
    rollback_package_id INTEGER REFERENCES update_packages(package_id),
    is_rollback         INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_update_history_time ON update_history(applied_at);

-- Automation clients: tracks machine+session pairs that have accessed the loopback endpoint.
-- Used for device fingerprint validation and revocation.
CREATE TABLE IF NOT EXISTS automation_clients (
    client_id       INTEGER PRIMARY KEY,
    -- Derived from Windows machine GUID via HMAC-SHA256
    machine_id      TEXT    NOT NULL,
    session_id      TEXT    REFERENCES user_sessions(session_id),
    registered_at   INTEGER NOT NULL,   -- IMMUTABLE
    last_seen_at    INTEGER,
    is_revoked      INTEGER NOT NULL DEFAULT 0,
    revoked_at      INTEGER,
    revoked_by      INTEGER REFERENCES users(user_id)
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_automation_active
    ON automation_clients(machine_id, session_id)
    WHERE is_revoked = 0;

CREATE INDEX IF NOT EXISTS idx_automation_machine ON automation_clients(machine_id);
