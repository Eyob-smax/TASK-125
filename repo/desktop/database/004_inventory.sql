-- Migration 004: Inventory categories, items, inbound/outbound ledger, usage history,
-- alert states.
--
-- Serial number uniqueness: UNIQUE constraint on inventory_items.serial_number
-- enforces facility-wide uniqueness. Application code checks for duplicates
-- before INSERT and produces a named-item error + audit_event on rejection.
--
-- Timestamp immutability: received_at and issued_at are set once on record creation.
-- No UPDATE on these columns is ever issued by the application.

CREATE TABLE IF NOT EXISTS inventory_categories (
    category_id             INTEGER PRIMARY KEY,
    name                    TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    unit                    TEXT    NOT NULL DEFAULT 'unit',
    -- Default low-stock threshold: alert when stock < N days of average usage
    low_stock_threshold_days  INTEGER NOT NULL DEFAULT 7  CHECK(low_stock_threshold_days > 0),
    -- Default expiration alert: surface N days before expiry date
    expiration_alert_days   INTEGER NOT NULL DEFAULT 14 CHECK(expiration_alert_days > 0),
    is_active               INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE IF NOT EXISTS inventory_items (
    item_id             INTEGER PRIMARY KEY,
    category_id         INTEGER NOT NULL REFERENCES inventory_categories(category_id),
    name                TEXT    NOT NULL,
    description         TEXT,
    storage_location    TEXT,
    quantity            INTEGER NOT NULL DEFAULT 0 CHECK(quantity >= 0),
    unit_cost_cents     INTEGER NOT NULL DEFAULT 0 CHECK(unit_cost_cents >= 0),
    expiration_date     INTEGER,            -- Unix timestamp UTC; NULL = no expiry
    -- serial_number: globally unique across all items; NULL if item has no serial
    serial_number       TEXT    UNIQUE,
    barcode             TEXT,
    is_active           INTEGER NOT NULL DEFAULT 1,
    created_at          INTEGER NOT NULL,   -- IMMUTABLE
    updated_at          INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_items_category   ON inventory_items(category_id);
CREATE INDEX IF NOT EXISTS idx_items_barcode    ON inventory_items(barcode);
CREATE INDEX IF NOT EXISTS idx_items_expiry     ON inventory_items(expiration_date)
    WHERE expiration_date IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_items_serial     ON inventory_items(serial_number)
    WHERE serial_number IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_items_active     ON inventory_items(is_active);

-- Rolling daily usage history; used to compute average_daily_usage for low-stock alerts.
-- InventoryService updates this on each outbound record creation.
CREATE TABLE IF NOT EXISTS item_usage_history (
    usage_id        INTEGER PRIMARY KEY,
    item_id         INTEGER NOT NULL REFERENCES inventory_items(item_id),
    period_date     INTEGER NOT NULL,   -- Unix midnight UTC (date boundary)
    quantity_used   INTEGER NOT NULL DEFAULT 0 CHECK(quantity_used >= 0),
    UNIQUE(item_id, period_date)
);

CREATE INDEX IF NOT EXISTS idx_usage_item_date ON item_usage_history(item_id, period_date);

-- Inbound records: receipts of stock into the facility.
-- received_at is IMMUTABLE; set at creation, never updated.
CREATE TABLE IF NOT EXISTS inbound_records (
    record_id       INTEGER PRIMARY KEY,
    item_id         INTEGER NOT NULL REFERENCES inventory_items(item_id),
    quantity        INTEGER NOT NULL CHECK(quantity > 0),
    received_at     INTEGER NOT NULL,   -- IMMUTABLE
    received_by     INTEGER NOT NULL REFERENCES users(user_id),
    vendor          TEXT,
    unit_cost_cents INTEGER NOT NULL DEFAULT 0 CHECK(unit_cost_cents >= 0),
    lot_number      TEXT,
    notes           TEXT
);

CREATE INDEX IF NOT EXISTS idx_inbound_item ON inbound_records(item_id);
CREATE INDEX IF NOT EXISTS idx_inbound_time ON inbound_records(received_at);

-- Outbound records: issuances from stock.
-- issued_at is IMMUTABLE; set at creation, never updated.
CREATE TABLE IF NOT EXISTS outbound_records (
    record_id       INTEGER PRIMARY KEY,
    item_id         INTEGER NOT NULL REFERENCES inventory_items(item_id),
    quantity        INTEGER NOT NULL CHECK(quantity > 0),
    issued_at       INTEGER NOT NULL,   -- IMMUTABLE
    issued_by       INTEGER NOT NULL REFERENCES users(user_id),
    recipient       TEXT,
    reason          TEXT    NOT NULL,
    booking_id      INTEGER REFERENCES bookings(booking_id),
    notes           TEXT
);

CREATE INDEX IF NOT EXISTS idx_outbound_item ON outbound_records(item_id);
CREATE INDEX IF NOT EXISTS idx_outbound_time ON outbound_records(issued_at);

-- Active alert states; one row per (item, alert_type) per triggering event.
-- acknowledged_at NULL = alert is active and visible in the tray badge count.
CREATE TABLE IF NOT EXISTS alert_states (
    alert_id            INTEGER PRIMARY KEY,
    item_id             INTEGER NOT NULL REFERENCES inventory_items(item_id),
    alert_type          TEXT    NOT NULL CHECK(alert_type IN ('low_stock','expiring_soon','expired')),
    triggered_at        INTEGER NOT NULL,
    acknowledged_at     INTEGER,
    acknowledged_by     INTEGER REFERENCES users(user_id),
    UNIQUE(item_id, alert_type, triggered_at)
);

CREATE INDEX IF NOT EXISTS idx_alerts_item      ON alert_states(item_id, alert_type);
CREATE INDEX IF NOT EXISTS idx_alerts_active    ON alert_states(acknowledged_at)
    WHERE acknowledged_at IS NULL;
