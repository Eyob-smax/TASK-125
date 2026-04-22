#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <optional>
#include <limits>

namespace shelterops::domain {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

enum class UserRole {
    Administrator,
    OperationsManager,
    InventoryClerk,
    Auditor
};

enum class KennelPurpose {
    Boarding, Adoption, Medical, Quarantine, Empty
};

enum class KennelRestrictionType {
    NoCats, NoDogs, NoLargeDogs,
    DogsOnly, CatsOnly, SmallAnimalsOnly,
    NoAggressive, MedicalOnly, QuarantineOnly
};

enum class AnimalSpecies {
    Dog, Cat, Rabbit, Bird, Reptile, SmallAnimal, Other
};

enum class AnimalStatus {
    Intake, Boarding, Adoptable, Adopted, Transferred, Deceased, Quarantine
};

enum class BookingStatus {
    Pending, Approved, Active, Completed, Cancelled, NoShow
};

enum class AlertType {
    LowStock, ExpiringSoon, Expired
};

enum class ReportType {
    Occupancy, Turnover, MaintenanceResponse, OverdueFees,
    InventorySummary, AuditExport, Custom
};

enum class JobType {
    ReportGenerate, ExportCsv, ExportPdf, RetentionRun,
    AlertScan, LanSync, Backup
};

enum class PriceAdjustmentType {
    FixedDiscountCents, PercentDiscount,
    FixedSurchargeCents, PercentSurcharge
};

enum class RetentionActionKind {
    Anonymize, Delete
};

enum class MaskingRule {
    Last4, InitialsOnly, DomainOnly, Redact, None
};

// ---------------------------------------------------------------------------
// Value objects
// ---------------------------------------------------------------------------

struct DateRange {
    int64_t from_unix = 0;  // inclusive lower bound (Unix timestamp UTC)
    int64_t to_unix   = 0;  // exclusive upper bound

    bool Contains(int64_t ts) const noexcept {
        return ts >= from_unix && ts < to_unix;
    }

    bool Overlaps(const DateRange& other) const noexcept {
        return from_unix < other.to_unix && to_unix > other.from_unix;
    }

    int64_t DurationSeconds() const noexcept { return to_unix - from_unix; }
    int64_t DurationDays()    const noexcept { return DurationSeconds() / 86400; }
};

struct ZoneCoord {
    float x_ft = 0.0f;
    float y_ft = 0.0f;

    float DistanceTo(const ZoneCoord& other) const noexcept {
        float dx = x_ft - other.x_ft;
        float dy = y_ft - other.y_ft;
        return std::sqrt(dx * dx + dy * dy);
    }
};

struct BookingConflict {
    int64_t conflicting_booking_id = 0;
    DateRange conflict_range;
    std::string description;
};

struct RestrictionViolation {
    KennelRestrictionType type;
    std::string description;
};

struct RankReason {
    std::string code;       // e.g. "RESTRICTION_MET", "DISTANCE_OK"
    std::string detail;     // human-readable detail string
};

struct BookabilityResult {
    bool is_bookable = false;
    std::vector<BookingConflict>      conflicts;
    std::vector<RestrictionViolation> restriction_violations;
    bool capacity_exceeded = false;
    std::string explanation;    // single human-readable summary
};

// Full kennel descriptor — defined here so RankedKennel can embed it.
struct KennelInfo {
    int64_t  kennel_id           = 0;
    int64_t  zone_id             = 0;
    std::string name;
    int      capacity            = 1;
    KennelPurpose purpose        = KennelPurpose::Empty;
    std::vector<KennelRestrictionType> restrictions;
    ZoneCoord coord;
    int      nightly_price_cents = 0;
    float    rating              = 0.0f;
    bool     is_active           = true;
};

struct RankedKennel {
    int64_t   kennel_id   = 0;
    float     score       = 0.0f;
    int       rank        = 0;
    KennelInfo kennel;
    std::vector<RankReason>   reasons;
    BookabilityResult         bookability;
};

struct OverdueFeeAgeBucket {
    int     min_days_inclusive = 0;
    int     max_days_inclusive = 0;  // -1 = unbounded (180+ days)
    int64_t count              = 0;
    int64_t total_cents        = 0;
};

struct MetricDelta {
    std::string metric_name;
    double value_before   = 0.0;
    double value_after    = 0.0;
    double delta_absolute = 0.0;
    double delta_pct      = std::numeric_limits<double>::quiet_NaN();
};

struct AlertThreshold {
    int low_stock_days      = 7;
    int expiration_days     = 14;
    int low_stock_qty       = 0;   // optional absolute-quantity override
    int expiring_soon_days  = 0;   // optional alias override
};

} // namespace shelterops::domain
