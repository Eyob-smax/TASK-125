-- Migration 005: Maintenance tickets and events.
--
-- Response-time metric formula (defined in ReportPipeline):
--   response_hours = (first_action_at - created_at) / 3600.0
--
-- first_action_at is set on the first maintenance_events INSERT for a ticket
-- where event_type IN ('status_changed','assigned','resolved').
-- created_at and first_action_at are IMMUTABLE once set.

CREATE TABLE IF NOT EXISTS maintenance_tickets (
    ticket_id       INTEGER PRIMARY KEY,
    zone_id         INTEGER REFERENCES zones(zone_id),
    kennel_id       INTEGER REFERENCES kennels(kennel_id),
    title           TEXT    NOT NULL,
    description     TEXT,
    priority        TEXT    NOT NULL DEFAULT 'normal' CHECK(priority IN (
                        'critical','high','normal','low'
                    )),
    status          TEXT    NOT NULL DEFAULT 'open' CHECK(status IN (
                        'open','acknowledged','in_progress','resolved','closed','wont_fix'
                    )),
    created_at      INTEGER NOT NULL,           -- IMMUTABLE; used in response-time formula
    created_by      INTEGER REFERENCES users(user_id),
    assigned_to     INTEGER REFERENCES users(user_id),
    -- Set when the first action event is recorded; used in response-time formula.
    -- NULL = no action taken yet (ticket counts as unacknowledged in report).
    first_action_at INTEGER,
    resolved_at     INTEGER
);

CREATE INDEX IF NOT EXISTS idx_tickets_status   ON maintenance_tickets(status);
CREATE INDEX IF NOT EXISTS idx_tickets_zone     ON maintenance_tickets(zone_id);
CREATE INDEX IF NOT EXISTS idx_tickets_kennel   ON maintenance_tickets(kennel_id);
CREATE INDEX IF NOT EXISTS idx_tickets_assigned ON maintenance_tickets(assigned_to);
CREATE INDEX IF NOT EXISTS idx_tickets_created  ON maintenance_tickets(created_at);
CREATE INDEX IF NOT EXISTS idx_tickets_open     ON maintenance_tickets(status, first_action_at)
    WHERE status = 'open' AND first_action_at IS NULL;

CREATE TABLE IF NOT EXISTS maintenance_events (
    event_id        INTEGER PRIMARY KEY,
    ticket_id       INTEGER NOT NULL REFERENCES maintenance_tickets(ticket_id),
    actor_id        INTEGER NOT NULL REFERENCES users(user_id),
    event_type      TEXT    NOT NULL CHECK(event_type IN (
                        'created','status_changed','note_added','assigned','resolved'
                    )),
    old_status      TEXT,
    new_status      TEXT,
    notes           TEXT,
    occurred_at     INTEGER NOT NULL    -- IMMUTABLE
);

CREATE INDEX IF NOT EXISTS idx_maint_events_ticket  ON maintenance_events(ticket_id);
CREATE INDEX IF NOT EXISTS idx_maint_events_time    ON maintenance_events(occurred_at);
CREATE INDEX IF NOT EXISTS idx_maint_events_actor   ON maintenance_events(actor_id);
