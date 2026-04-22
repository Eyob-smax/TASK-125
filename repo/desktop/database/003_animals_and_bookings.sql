-- Migration 003: Animals, adoptable listings, boarding bookings, approvals,
-- boarding fees, and recommendation result cache.
--
-- Overlap invariant: two bookings for the same kennel may not overlap if their
-- combined count exceeds the kennel's capacity. This is enforced in application
-- code by BookingRules::EvaluateBookability before any INSERT into bookings.
-- No database trigger enforces this; the application layer is authoritative.

CREATE TABLE IF NOT EXISTS animals (
    animal_id       INTEGER PRIMARY KEY,
    name            TEXT    NOT NULL,
    species         TEXT    NOT NULL CHECK(species IN (
                        'dog','cat','rabbit','bird','reptile','small_animal','other'
                    )),
    breed           TEXT,
    age_years       REAL    CHECK(age_years IS NULL OR age_years >= 0),
    weight_lbs      REAL    CHECK(weight_lbs IS NULL OR weight_lbs > 0),
    color           TEXT,
    sex             TEXT    CHECK(sex IN ('male','female','unknown',NULL)),
    microchip_id    TEXT,
    is_aggressive   INTEGER NOT NULL DEFAULT 0,
    is_large_dog    INTEGER NOT NULL DEFAULT 0,  -- weight_lbs > 50 by convention
    intake_at       INTEGER NOT NULL,            -- IMMUTABLE
    intake_type     TEXT    NOT NULL CHECK(intake_type IN (
                        'stray','surrender','transfer','return','confiscation'
                    )),
    status          TEXT    NOT NULL DEFAULT 'intake' CHECK(status IN (
                        'intake','boarding','adoptable','adopted',
                        'transferred','deceased','quarantine'
                    )),
    notes           TEXT,
    created_by      INTEGER REFERENCES users(user_id),
    anonymized_at   INTEGER
);

CREATE INDEX IF NOT EXISTS idx_animals_status   ON animals(status);
CREATE INDEX IF NOT EXISTS idx_animals_species  ON animals(species);
CREATE INDEX IF NOT EXISTS idx_animals_intake   ON animals(intake_at);

-- An adoptable listing links an animal to a kennel and advertises adoption.
-- While a listing is active, the kennel's current_purpose = 'adoption'
-- and is not available for boarding bookings.
CREATE TABLE IF NOT EXISTS adoptable_listings (
    listing_id          INTEGER PRIMARY KEY,
    animal_id           INTEGER NOT NULL REFERENCES animals(animal_id),
    kennel_id           INTEGER REFERENCES kennels(kennel_id),
    listing_date        INTEGER NOT NULL,       -- IMMUTABLE
    adoption_fee_cents  INTEGER NOT NULL DEFAULT 0 CHECK(adoption_fee_cents >= 0),
    description         TEXT,
    rating              REAL    CHECK(rating IS NULL OR (rating >= 0.0 AND rating <= 5.0)),
    status              TEXT    NOT NULL DEFAULT 'active' CHECK(status IN (
                            'active','pending','adopted','removed'
                        )),
    created_by          INTEGER REFERENCES users(user_id),
    adopted_at          INTEGER
);

CREATE INDEX IF NOT EXISTS idx_listings_animal  ON adoptable_listings(animal_id);
CREATE INDEX IF NOT EXISTS idx_listings_kennel  ON adoptable_listings(kennel_id);
CREATE INDEX IF NOT EXISTS idx_listings_status  ON adoptable_listings(status);

-- Boarding bookings: a time-bounded reservation of a kennel for a paying boarder.
-- guest_phone_enc and guest_email_enc store AES-256-GCM ciphertext; never plaintext.
CREATE TABLE IF NOT EXISTS bookings (
    booking_id          INTEGER PRIMARY KEY,
    kennel_id           INTEGER NOT NULL REFERENCES kennels(kennel_id),
    animal_id           INTEGER REFERENCES animals(animal_id),
    guest_name          TEXT,                   -- NULL for shelter animals
    guest_phone_enc     TEXT,                   -- AES-256-GCM; NULL if not provided
    guest_email_enc     TEXT,                   -- AES-256-GCM; NULL if not provided
    check_in_at         INTEGER NOT NULL,
    check_out_at        INTEGER NOT NULL,
    CHECK(check_out_at > check_in_at),
    status              TEXT    NOT NULL DEFAULT 'pending' CHECK(status IN (
                            'pending','approved','active','completed','cancelled','no_show'
                        )),
    nightly_price_cents INTEGER NOT NULL CHECK(nightly_price_cents >= 0),
    total_price_cents   INTEGER NOT NULL CHECK(total_price_cents >= 0),
    special_requirements TEXT,
    created_by          INTEGER REFERENCES users(user_id),
    created_at          INTEGER NOT NULL,       -- IMMUTABLE
    approved_by         INTEGER REFERENCES users(user_id),
    approved_at         INTEGER,
    notes               TEXT
);

-- Time-range index for overlap queries: WHERE kennel_id = ? AND check_out_at > ? AND check_in_at < ?
CREATE INDEX IF NOT EXISTS idx_bookings_kennel_time ON bookings(kennel_id, check_in_at, check_out_at);
CREATE INDEX IF NOT EXISTS idx_bookings_status      ON bookings(status);
CREATE INDEX IF NOT EXISTS idx_bookings_created     ON bookings(created_at);

-- Approval records: one per booking when booking_approval_required policy is true.
-- Operations Manager or Administrator sets decision.
CREATE TABLE IF NOT EXISTS booking_approvals (
    approval_id     INTEGER PRIMARY KEY,
    booking_id      INTEGER NOT NULL REFERENCES bookings(booking_id),
    requested_by    INTEGER NOT NULL REFERENCES users(user_id),
    requested_at    INTEGER NOT NULL,           -- IMMUTABLE
    approver_id     INTEGER REFERENCES users(user_id),
    -- NULL = pending; set when approver acts
    decision        TEXT    CHECK(decision IN ('approved','rejected','escalated',NULL)),
    decided_at      INTEGER,
    notes           TEXT
);

CREATE INDEX IF NOT EXISTS idx_approvals_booking    ON booking_approvals(booking_id);
CREATE INDEX IF NOT EXISTS idx_approvals_approver   ON booking_approvals(approver_id);
CREATE INDEX IF NOT EXISTS idx_approvals_pending
    ON booking_approvals(approver_id, decided_at)
    WHERE decided_at IS NULL;

-- Boarding fees: outstanding charges per booking.
-- paid_at NULL = unpaid; used for overdue-fee distribution report.
CREATE TABLE IF NOT EXISTS boarding_fees (
    fee_id          INTEGER PRIMARY KEY,
    booking_id      INTEGER NOT NULL REFERENCES bookings(booking_id),
    amount_cents    INTEGER NOT NULL CHECK(amount_cents > 0),
    due_at          INTEGER NOT NULL,
    paid_at         INTEGER,
    payment_method  TEXT,
    created_at      INTEGER NOT NULL    -- IMMUTABLE
);

CREATE INDEX IF NOT EXISTS idx_fees_booking ON boarding_fees(booking_id);
CREATE INDEX IF NOT EXISTS idx_fees_unpaid  ON boarding_fees(due_at) WHERE paid_at IS NULL;

-- Cached recommendation results for audit trail and UI display.
-- query_hash is SHA-256 of the serialized BookingSearchFilter.
CREATE TABLE IF NOT EXISTS recommendation_results (
    result_id       INTEGER PRIMARY KEY,
    query_hash      TEXT    NOT NULL,
    kennel_id       INTEGER NOT NULL REFERENCES kennels(kennel_id),
    rank_position   INTEGER NOT NULL CHECK(rank_position > 0),
    score           REAL    NOT NULL,
    -- JSON array: [{"code":"RESTRICTION_MET","detail":"Meets no-cats restriction"}, ...]
    reason_json     TEXT    NOT NULL,
    generated_at    INTEGER NOT NULL    -- IMMUTABLE
);

CREATE INDEX IF NOT EXISTS idx_recs_query   ON recommendation_results(query_hash, rank_position);
CREATE INDEX IF NOT EXISTS idx_recs_kennel  ON recommendation_results(kennel_id);
