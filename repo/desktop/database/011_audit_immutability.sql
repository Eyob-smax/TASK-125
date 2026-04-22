-- Enforce append-only semantics on audit_events.
-- Any attempt to UPDATE or DELETE an audit row is rejected at the database level.

CREATE TRIGGER audit_events_no_update
BEFORE UPDATE ON audit_events
BEGIN
    SELECT RAISE(ABORT, 'audit_events rows are immutable: UPDATE is not permitted');
END;

CREATE TRIGGER audit_events_no_delete
BEFORE DELETE ON audit_events
BEGIN
    SELECT RAISE(ABORT, 'audit_events rows are immutable: DELETE is not permitted');
END;
