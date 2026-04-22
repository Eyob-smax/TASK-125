-- Migration 007: System policies, product catalog, price rules, after-sales
-- adjustments, retention policies, consent records, masked-field policies,
-- and export permission matrix.

CREATE TABLE IF NOT EXISTS system_policies (
    policy_id   INTEGER PRIMARY KEY,
    key         TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    value       TEXT    NOT NULL,
    updated_by  INTEGER REFERENCES users(user_id),
    updated_at  INTEGER NOT NULL
);

INSERT OR IGNORE INTO system_policies (key, value, updated_at) VALUES
    ('low_stock_days_threshold',  '7',         strftime('%s','now')),
    ('expiration_alert_days',     '14',        strftime('%s','now')),
    ('retention_years_default',   '7',         strftime('%s','now')),
    ('retention_action',          'anonymize', strftime('%s','now')),
    ('booking_approval_required', 'true',      strftime('%s','now')),
    ('max_login_attempts',        '5',         strftime('%s','now')),
    ('lockout_duration_minutes',  '30',        strftime('%s','now'));

CREATE TABLE IF NOT EXISTS product_catalog (
    entry_id                INTEGER PRIMARY KEY,
    name                    TEXT    NOT NULL,
    category_id             INTEGER REFERENCES inventory_categories(category_id),
    default_unit_cost_cents INTEGER NOT NULL DEFAULT 0 CHECK(default_unit_cost_cents >= 0),
    vendor                  TEXT,
    sku                     TEXT    UNIQUE,
    is_active               INTEGER NOT NULL DEFAULT 1,
    created_by              INTEGER REFERENCES users(user_id),
    created_at              INTEGER NOT NULL    -- IMMUTABLE
);

CREATE INDEX IF NOT EXISTS idx_catalog_category ON product_catalog(category_id);
CREATE INDEX IF NOT EXISTS idx_catalog_active   ON product_catalog(is_active);

-- Price rules: conditional adjustments to boarding, adoption, or inventory costs.
-- condition_json: {"species":"dog","zone":"Building A","days_gt":7,...}
-- Adjustment is applied by PriceRuleEngine during booking or issuance creation.
CREATE TABLE IF NOT EXISTS price_rules (
    rule_id             INTEGER PRIMARY KEY,
    name                TEXT    NOT NULL,
    applies_to          TEXT    NOT NULL CHECK(applies_to IN ('boarding','adoption','inventory')),
    condition_json      TEXT    NOT NULL DEFAULT '{}',
    adjustment_type     TEXT    NOT NULL CHECK(adjustment_type IN (
                            'fixed_discount_cents','percent_discount',
                            'fixed_surcharge_cents','percent_surcharge'
                        )),
    amount              REAL    NOT NULL CHECK(amount >= 0),
    valid_from          INTEGER,
    valid_to            INTEGER,
    is_active           INTEGER NOT NULL DEFAULT 1,
    created_by          INTEGER REFERENCES users(user_id),
    created_at          INTEGER NOT NULL    -- IMMUTABLE
);

CREATE INDEX IF NOT EXISTS idx_price_rules_active ON price_rules(is_active, valid_from, valid_to);

-- After-sales adjustments to a completed booking.
-- created_at is IMMUTABLE; approved_by must be operations_manager or administrator.
CREATE TABLE IF NOT EXISTS after_sales_adjustments (
    adjustment_id   INTEGER PRIMARY KEY,
    booking_id      INTEGER REFERENCES bookings(booking_id),
    amount_cents    INTEGER NOT NULL,       -- positive = additional charge; negative = refund
    reason          TEXT    NOT NULL,
    approved_by     INTEGER REFERENCES users(user_id),
    created_by      INTEGER NOT NULL REFERENCES users(user_id),
    created_at      INTEGER NOT NULL        -- IMMUTABLE
);

CREATE INDEX IF NOT EXISTS idx_adjustments_booking ON after_sales_adjustments(booking_id);

-- Retention policies per entity type.
-- entity_type matches table names: 'users','animals','bookings','inventory_items', etc.
CREATE TABLE IF NOT EXISTS retention_policies (
    policy_id       INTEGER PRIMARY KEY,
    entity_type     TEXT    NOT NULL UNIQUE,
    retention_years INTEGER NOT NULL DEFAULT 7 CHECK(retention_years > 0),
    action          TEXT    NOT NULL DEFAULT 'anonymize'
                            CHECK(action IN ('anonymize','delete')),
    updated_by      INTEGER REFERENCES users(user_id),
    updated_at      INTEGER NOT NULL
);

INSERT OR IGNORE INTO retention_policies (entity_type, retention_years, action, updated_at) VALUES
    ('users',            7, 'anonymize', strftime('%s','now')),
    ('animals',          7, 'anonymize', strftime('%s','now')),
    ('bookings',         7, 'anonymize', strftime('%s','now')),
    ('inventory_items',  7, 'anonymize', strftime('%s','now'));

-- Consent records: one row per consent event (grant or withdrawal).
CREATE TABLE IF NOT EXISTS consent_records (
    consent_id      INTEGER PRIMARY KEY,
    entity_type     TEXT    NOT NULL,
    entity_id       INTEGER NOT NULL,
    consent_type    TEXT    NOT NULL CHECK(consent_type IN (
                        'data_processing','marketing','retention'
                    )),
    given_at        INTEGER NOT NULL,   -- IMMUTABLE
    withdrawn_at    INTEGER
);

CREATE INDEX IF NOT EXISTS idx_consent_entity ON consent_records(entity_type, entity_id);

-- Field-level masking rules applied in view-model construction per role.
-- masking_rule applied by RolePermissions::MaskField before data reaches UI.
CREATE TABLE IF NOT EXISTS masked_field_policies (
    policy_id       INTEGER PRIMARY KEY,
    entity_type     TEXT    NOT NULL,
    field_name      TEXT    NOT NULL,
    masking_rule    TEXT    NOT NULL CHECK(masking_rule IN (
                        'last4','initials_only','domain_only','redact','none'
                    )),
    applies_to_role TEXT    NOT NULL,
    UNIQUE(entity_type, field_name, applies_to_role)
);

INSERT OR IGNORE INTO masked_field_policies
    (entity_type, field_name, masking_rule, applies_to_role) VALUES
    ('users',    'phone',          'last4',         'auditor'),
    ('users',    'email',          'domain_only',   'auditor'),
    ('users',    'display_name',   'initials_only', 'auditor'),
    ('bookings', 'guest_name',     'initials_only', 'auditor'),
    ('bookings', 'guest_phone_enc','last4',          'auditor'),
    ('bookings', 'guest_email_enc','domain_only',    'auditor');

-- Export permissions matrix: which roles may export which report types and formats.
CREATE TABLE IF NOT EXISTS export_permissions (
    permission_id   INTEGER PRIMARY KEY,
    role            TEXT    NOT NULL,
    report_type     TEXT    NOT NULL,
    csv_allowed     INTEGER NOT NULL DEFAULT 0,
    pdf_allowed     INTEGER NOT NULL DEFAULT 0,
    UNIQUE(role, report_type)
);

INSERT OR IGNORE INTO export_permissions (role, report_type, csv_allowed, pdf_allowed) VALUES
    ('administrator',       'occupancy',             1, 1),
    ('administrator',       'turnover',              1, 1),
    ('administrator',       'maintenance_response',  1, 1),
    ('administrator',       'overdue_fees',          1, 1),
    ('administrator',       'inventory_summary',     1, 1),
    ('administrator',       'audit_export',          1, 0),
    ('operations_manager',  'occupancy',             1, 1),
    ('operations_manager',  'turnover',              1, 1),
    ('operations_manager',  'maintenance_response',  1, 1),
    ('operations_manager',  'overdue_fees',          1, 1),
    ('operations_manager',  'inventory_summary',     1, 0),
    ('inventory_clerk',     'inventory_summary',     1, 0),
    ('auditor',             'audit_export',          1, 0);
