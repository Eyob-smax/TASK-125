-- Add absolute_expires_at to user_sessions to enforce the hard 12-hour
-- session lifetime independent of the sliding 1-hour inactivity window.
-- Existing rows default to 0; AuthService treats 0 as "no absolute cap"
-- for legacy rows, but all new logins set this correctly.
ALTER TABLE user_sessions
    ADD COLUMN absolute_expires_at INTEGER NOT NULL DEFAULT 0;
