-- Migration 010: Authentication lockout columns
-- Adds failed-login tracking to support LockoutPolicy
-- (5 failures within 15 min → lock 15 min; escalates to 1h).
-- No edits to 001; forward-only migration.

ALTER TABLE users ADD COLUMN failed_login_attempts INTEGER NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN locked_until          INTEGER;           -- NULL = not locked

CREATE INDEX IF NOT EXISTS idx_users_locked_until ON users(locked_until)
    WHERE locked_until IS NOT NULL;
