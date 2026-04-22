#pragma once
#include "shelterops/infrastructure/Database.h"
#include "shelterops/domain/Types.h"
#include "shelterops/domain/BookingRules.h"
#include <vector>
#include <optional>
#include <cstdint>
#include <string>

namespace shelterops::repositories {

struct ZoneRecord {
    int64_t     zone_id    = 0;
    std::string name;
    std::string building;
    std::string row_label;
    float       x_coord_ft = 0.0f;
    float       y_coord_ft = 0.0f;
    bool        is_active  = true;
};

struct KennelSearchParams {
    std::optional<int64_t> zone_id;
    std::optional<std::string> purpose;   // empty = any
    bool active_only = true;
};

class KennelRepository {
public:
    explicit KennelRepository(infrastructure::Database& db);

    std::vector<domain::KennelInfo> ListActiveKennels() const;
    std::vector<domain::KennelInfo> FindByFilter(const KennelSearchParams& params) const;
    std::vector<domain::KennelRestrictionType> FindRestrictionsFor(int64_t kennel_id) const;

    std::vector<ZoneRecord> ListZones() const;
    std::optional<ZoneRecord> FindZoneById(int64_t zone_id) const;

    // Upsert a precomputed distance between two zones.
    void UpsertZoneDistance(int64_t from_zone_id, int64_t to_zone_id, float distance_ft);

    // Returns the cached distance, or -1.0f if not cached.
    float GetDistance(int64_t from_zone_id, int64_t to_zone_id) const;

    void SetKennelPurpose(int64_t kennel_id, domain::KennelPurpose purpose);

    // Insert a new zone; returns its zone_id.
    int64_t InsertZone(const std::string& name, const std::string& building,
                       const std::string& row_label, float x_ft, float y_ft);

    // Insert a new kennel; returns its kennel_id.
    int64_t InsertKennel(int64_t zone_id, const std::string& name,
                         int capacity, int nightly_price_cents);

    void InsertRestriction(int64_t kennel_id, domain::KennelRestrictionType type);

private:
    domain::KennelInfo RowToKennelInfo(const std::vector<std::string>& vals) const;
    static domain::KennelPurpose ParsePurpose(const std::string& s);
    static std::string PurposeToString(domain::KennelPurpose p);
    static domain::KennelRestrictionType ParseRestriction(const std::string& s);
    static std::string RestrictionToString(domain::KennelRestrictionType r);

    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
