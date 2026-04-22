-- Migration 001: Core infrastructure tables
-- Establishes WAL mode, migration tracking, immutable audit log,
-- user authentication, session management, and crash-checkpoint storage.
-- All shelter domain tables (kennels, bookings, inventory, reports) are
-- added in subsequent migration files as each domain is implemented.

PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;
PRAGMA foreign_keys = ON;

-- -------------------------------------------------------------------------
-- Migration bookkeeping
-- -------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS db_migrations (
    migration_id  INTEGER PRIMARY KEY,
    script_name   TEXT    NOT NULL UNIQUE,
    applied_at    INTEGER NOT NULL  -- Unix timestamp UTC
);

-- -------------------------------------------------------------------------
-- Users and role-based access
-- -------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS users (
    user_id        INTEGER PRIMARY KEY,
    username       TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    display_name   TEXT    NOT NULL,
    -- Argon2id hash; plaintext is never stored
    password_hash  TEXT    NOT NULL,
    role           TEXT    NOT NULL
                           CHECK(role IN (
                               'administrator',
                               'operations_manager',
                               'inventory_clerk',
                               'auditor'
                           )),
    is_active      INTEGER NOT NULL DEFAULT 1,
    created_at     INTEGER NOT NULL,
    last_login_at  INTEGER,
    -- PII consent and retention support
    consent_given  INTEGER NOT NULL DEFAULT 0,
    anonymized_at  INTEGER             -- set by retention job, NULL = live record
);

CREATE INDEX IF NOT EXISTS idx_users_role     ON users(role);
CREATE INDEX IF NOT EXISTS idx_users_active   ON users(is_active);

-- -------------------------------------------------------------------------
-- Sessions
-- -------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS user_sessions (
    session_id         TEXT    PRIMARY KEY,      -- UUID v4
    user_id            INTEGER NOT NULL REFERENCES users(user_id),
    created_at         INTEGER NOT NULL,
    expires_at         INTEGER NOT NULL,
    -- Machine-ID + operator fingerprint for automation endpoint auth
    device_fingerprint TEXT,
    is_active          INTEGER NOT NULL DEFAULT 1
);

CREATE INDEX IF NOT EXISTS idx_sessions_user   ON user_sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_active ON user_sessions(is_active, expires_at);

-- -------------------------------------------------------------------------
-- Append-only audit log
-- No UPDATE or DELETE is ever issued against this table.
-- -------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS audit_events (
    event_id       INTEGER PRIMARY KEY,
    occurred_at    INTEGER NOT NULL,  -- Unix timestamp UTC; immutable once written
    actor_user_id  INTEGER,           -- NULL for system-generated events
    actor_role     TEXT,
    event_type     TEXT    NOT NULL,  -- e.g. LOGIN, LOGOUT, ITEM_ISSUED, BOOKING_CREATED
    entity_type    TEXT,              -- e.g. inventory_item, kennel, user
    entity_id      INTEGER,
    description    TEXT    NOT NULL,
    session_id     TEXT
    -- No PII, no decrypted fields, no secret material ever written here
);

CREATE INDEX IF NOT EXISTS idx_audit_occurred  ON audit_events(occurred_at);
CREATE INDEX IF NOT EXISTS idx_audit_actor     ON audit_events(actor_user_id);
CREATE INDEX IF NOT EXISTS idx_audit_entity    ON audit_events(entity_type, entity_id);
CREATE INDEX IF NOT EXISTS idx_audit_type      ON audit_events(event_type);

-- -------------------------------------------------------------------------
-- Crash-checkpoint storage
-- Saves open window layout and unsaved form state on exit / signal.
-- No PII or sensitive field values are written here.
-- -------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS crash_checkpoints (
    checkpoint_id  INTEGER PRIMARY KEY,
    saved_at       INTEGER NOT NULL,
    -- JSON: {"windows":[{"id":"KennelBoard","docked":true},...], "focused":"ItemLedger"}
    window_state   TEXT    NOT NULL,
    -- JSON: sanitized form field values only (no PII, no passwords)
    form_state     TEXT
);

-- Only the most recent checkpoint is useful; trim older entries on startup.
