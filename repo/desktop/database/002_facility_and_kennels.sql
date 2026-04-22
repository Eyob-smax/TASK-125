-- Migration 002: Facility structure, zones, kennel units, and kennel restrictions
-- Zones model building/row sections with XY coordinates for distance queries.
-- Zone distance is computed as 2D Euclidean from (x_coord_ft, y_coord_ft).

CREATE TABLE IF NOT EXISTS zones (
    zone_id         INTEGER PRIMARY KEY,
    name            TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    building        TEXT    NOT NULL,
    row_label       TEXT,
    x_coord_ft      REAL    NOT NULL DEFAULT 0.0,  -- feet from facility origin
    y_coord_ft      REAL    NOT NULL DEFAULT 0.0,
    description     TEXT,
    is_active       INTEGER NOT NULL DEFAULT 1
);

CREATE INDEX IF NOT EXISTS idx_zones_building   ON zones(building);
CREATE INDEX IF NOT EXISTS idx_zones_active     ON zones(is_active);

-- Precomputed inter-zone distances; populated/refreshed by ZoneDistanceService.
-- Queried by distance-filtered kennel search.
CREATE TABLE IF NOT EXISTS zone_distance_cache (
    from_zone_id    INTEGER NOT NULL REFERENCES zones(zone_id),
    to_zone_id      INTEGER NOT NULL REFERENCES zones(zone_id),
    distance_ft     REAL    NOT NULL CHECK(distance_ft >= 0.0),
    PRIMARY KEY (from_zone_id, to_zone_id)
);

CREATE TABLE IF NOT EXISTS kennels (
    kennel_id           INTEGER PRIMARY KEY,
    zone_id             INTEGER NOT NULL REFERENCES zones(zone_id),
    name                TEXT    NOT NULL,
    -- Maximum simultaneous active boarders; most kennels = 1
    capacity            INTEGER NOT NULL DEFAULT 1 CHECK(capacity > 0),
    current_purpose     TEXT    NOT NULL DEFAULT 'empty'
                                CHECK(current_purpose IN (
                                    'boarding','adoption','medical','quarantine','empty'
                                )),
    -- USD in cents; boarding-only; adoption fee is on adoptable_listings
    nightly_price_cents INTEGER NOT NULL DEFAULT 0 CHECK(nightly_price_cents >= 0),
    rating              REAL    CHECK(rating IS NULL OR (rating >= 0.0 AND rating <= 5.0)),
    is_active           INTEGER NOT NULL DEFAULT 1,
    notes               TEXT
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_kennels_zone_name ON kennels(zone_id, name);
CREATE INDEX IF NOT EXISTS idx_kennels_purpose          ON kennels(current_purpose);
CREATE INDEX IF NOT EXISTS idx_kennels_active           ON kennels(is_active);

-- Restriction semantics:
--   no_cats, no_dogs, no_large_dogs: exclude animals of that type/size
--   dogs_only, cats_only, small_animals_only: permit only that type
--   no_aggressive: reject animals flagged is_aggressive=true
--   medical_only, quarantine_only: reserve for specific purposes
CREATE TABLE IF NOT EXISTS kennel_restrictions (
    restriction_id  INTEGER PRIMARY KEY,
    kennel_id       INTEGER NOT NULL REFERENCES kennels(kennel_id),
    restriction_type TEXT   NOT NULL CHECK(restriction_type IN (
        'no_cats','no_dogs','no_large_dogs','dogs_only','cats_only',
        'small_animals_only','no_aggressive','medical_only','quarantine_only'
    )),
    notes           TEXT,
    UNIQUE(kennel_id, restriction_type)
);

CREATE INDEX IF NOT EXISTS idx_restrictions_kennel ON kennel_restrictions(kennel_id);
