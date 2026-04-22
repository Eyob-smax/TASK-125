#include "shelterops/repositories/KennelRepository.h"
#include <stdexcept>
#include <cmath>

namespace shelterops::repositories {

KennelRepository::KennelRepository(infrastructure::Database& db) : db_(db) {
    auto conn = db_.Acquire();
    conn->Exec(
        "CREATE TABLE IF NOT EXISTS kennel_restrictions("
        "restriction_id INTEGER PRIMARY KEY, "
        "kennel_id INTEGER NOT NULL, "
        "restriction_type TEXT NOT NULL, "
        "notes TEXT, "
        "UNIQUE(kennel_id,restriction_type))",
        {});
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

domain::KennelPurpose KennelRepository::ParsePurpose(const std::string& s) {
    if (s == "boarding")   return domain::KennelPurpose::Boarding;
    if (s == "adoption")   return domain::KennelPurpose::Adoption;
    if (s == "medical")    return domain::KennelPurpose::Medical;
    if (s == "quarantine") return domain::KennelPurpose::Quarantine;
    return domain::KennelPurpose::Empty;
}

std::string KennelRepository::PurposeToString(domain::KennelPurpose p) {
    switch (p) {
    case domain::KennelPurpose::Boarding:   return "boarding";
    case domain::KennelPurpose::Adoption:   return "adoption";
    case domain::KennelPurpose::Medical:    return "medical";
    case domain::KennelPurpose::Quarantine: return "quarantine";
    default:                                return "empty";
    }
}

domain::KennelRestrictionType KennelRepository::ParseRestriction(const std::string& s) {
    if (s == "no_cats")            return domain::KennelRestrictionType::NoCats;
    if (s == "no_dogs")            return domain::KennelRestrictionType::NoDogs;
    if (s == "no_large_dogs")      return domain::KennelRestrictionType::NoLargeDogs;
    if (s == "dogs_only")          return domain::KennelRestrictionType::DogsOnly;
    if (s == "cats_only")          return domain::KennelRestrictionType::CatsOnly;
    if (s == "small_animals_only") return domain::KennelRestrictionType::SmallAnimalsOnly;
    if (s == "no_aggressive")      return domain::KennelRestrictionType::NoAggressive;
    if (s == "medical_only")       return domain::KennelRestrictionType::MedicalOnly;
    return domain::KennelRestrictionType::QuarantineOnly;
}

std::string KennelRepository::RestrictionToString(domain::KennelRestrictionType r) {
    switch (r) {
    case domain::KennelRestrictionType::NoCats:           return "no_cats";
    case domain::KennelRestrictionType::NoDogs:           return "no_dogs";
    case domain::KennelRestrictionType::NoLargeDogs:      return "no_large_dogs";
    case domain::KennelRestrictionType::DogsOnly:         return "dogs_only";
    case domain::KennelRestrictionType::CatsOnly:         return "cats_only";
    case domain::KennelRestrictionType::SmallAnimalsOnly: return "small_animals_only";
    case domain::KennelRestrictionType::NoAggressive:     return "no_aggressive";
    case domain::KennelRestrictionType::MedicalOnly:      return "medical_only";
    default:                                               return "quarantine_only";
    }
}

domain::KennelInfo KennelRepository::RowToKennelInfo(const std::vector<std::string>& vals) const {
    // Columns: kennel_id, zone_id, name, capacity, current_purpose,
    //          nightly_price_cents, rating, is_active, x_coord_ft, y_coord_ft
    domain::KennelInfo info;
    info.kennel_id           = vals[0].empty() ? 0 : std::stoll(vals[0]);
    info.zone_id             = vals[1].empty() ? 0 : std::stoll(vals[1]);
    info.name                = vals[2];
    info.capacity            = vals[3].empty() ? 1 : std::stoi(vals[3]);
    info.purpose             = ParsePurpose(vals[4]);
    info.nightly_price_cents = vals[5].empty() ? 0 : std::stoi(vals[5]);
    info.rating              = vals[6].empty() ? 0.0f : std::stof(vals[6]);
    info.is_active           = vals[7].empty() ? true : (vals[7] == "1");
    info.coord.x_ft          = vals[8].empty() ? 0.0f : std::stof(vals[8]);
    info.coord.y_ft          = vals[9].empty() ? 0.0f : std::stof(vals[9]);
    return info;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::vector<domain::KennelInfo> KennelRepository::ListActiveKennels() const {
    static const std::string sql =
        "SELECT k.kennel_id, k.zone_id, k.name, k.capacity, k.current_purpose, "
        "       k.nightly_price_cents, COALESCE(k.rating,0.0), k.is_active, "
        "       z.x_coord_ft, z.y_coord_ft "
        "FROM kennels k JOIN zones z ON k.zone_id = z.zone_id "
        "WHERE k.is_active = 1 ORDER BY k.kennel_id";

    std::vector<domain::KennelInfo> result;
    std::vector<int64_t> kennel_ids;
    {
        auto conn = db_.Acquire();
        conn->Query(sql, {}, [&](const auto&, const auto& vals) {
            auto info = RowToKennelInfo(vals);
            kennel_ids.push_back(info.kennel_id);
            result.push_back(std::move(info));
        });
    }
    for (size_t i = 0; i < result.size(); ++i) {
        result[i].restrictions = FindRestrictionsFor(kennel_ids[i]);
    }
    return result;
}

std::vector<domain::KennelInfo> KennelRepository::FindByFilter(
    const KennelSearchParams& params) const {
    std::string sql =
        "SELECT k.kennel_id, k.zone_id, k.name, k.capacity, k.current_purpose, "
        "       k.nightly_price_cents, COALESCE(k.rating,0.0), k.is_active, "
        "       z.x_coord_ft, z.y_coord_ft "
        "FROM kennels k JOIN zones z ON k.zone_id = z.zone_id WHERE 1=1";

    std::vector<std::string> binds;
    if (params.active_only) {
        sql += " AND k.is_active = 1";
    }
    if (params.zone_id.has_value()) {
        sql += " AND k.zone_id = ?";
        binds.push_back(std::to_string(*params.zone_id));
    }
    if (params.purpose.has_value()) {
        sql += " AND k.current_purpose = ?";
        binds.push_back(*params.purpose);
    }
    sql += " ORDER BY k.kennel_id";

    std::vector<domain::KennelInfo> result;
    std::vector<int64_t> kennel_ids;
    {
        auto conn = db_.Acquire();
        conn->Query(sql, binds, [&](const auto&, const auto& vals) {
            auto info = RowToKennelInfo(vals);
            kennel_ids.push_back(info.kennel_id);
            result.push_back(std::move(info));
        });
    }
    for (size_t i = 0; i < result.size(); ++i) {
        result[i].restrictions = FindRestrictionsFor(kennel_ids[i]);
    }
    return result;
}

std::vector<domain::KennelRestrictionType> KennelRepository::FindRestrictionsFor(
    int64_t kennel_id) const {
    static const std::string sql =
        "SELECT restriction_type FROM kennel_restrictions WHERE kennel_id = ?";

    std::vector<domain::KennelRestrictionType> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(kennel_id)},
        [&](const auto&, const auto& vals) {
            result.push_back(ParseRestriction(vals[0]));
        });
    return result;
}

std::vector<ZoneRecord> KennelRepository::ListZones() const {
    static const std::string sql =
        "SELECT zone_id, name, building, COALESCE(row_label,''), "
        "       x_coord_ft, y_coord_ft, is_active FROM zones ORDER BY zone_id";

    std::vector<ZoneRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {}, [&](const auto&, const auto& vals) {
        ZoneRecord z;
        z.zone_id    = vals[0].empty() ? 0 : std::stoll(vals[0]);
        z.name       = vals[1];
        z.building   = vals[2];
        z.row_label  = vals[3];
        z.x_coord_ft = vals[4].empty() ? 0.0f : std::stof(vals[4]);
        z.y_coord_ft = vals[5].empty() ? 0.0f : std::stof(vals[5]);
        z.is_active  = vals[6].empty() ? true : (vals[6] == "1");
        result.push_back(z);
    });
    return result;
}

std::optional<ZoneRecord> KennelRepository::FindZoneById(int64_t zone_id) const {
    static const std::string sql =
        "SELECT zone_id, name, building, COALESCE(row_label,''), "
        "       x_coord_ft, y_coord_ft, is_active FROM zones WHERE zone_id = ?";

    std::optional<ZoneRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(zone_id)},
        [&](const auto&, const auto& vals) {
            ZoneRecord z;
            z.zone_id    = vals[0].empty() ? 0 : std::stoll(vals[0]);
            z.name       = vals[1];
            z.building   = vals[2];
            z.row_label  = vals[3];
            z.x_coord_ft = vals[4].empty() ? 0.0f : std::stof(vals[4]);
            z.y_coord_ft = vals[5].empty() ? 0.0f : std::stof(vals[5]);
            z.is_active  = vals[6].empty() ? true : (vals[6] == "1");
            result = z;
        });
    return result;
}

void KennelRepository::UpsertZoneDistance(int64_t from_zone_id, int64_t to_zone_id,
                                           float distance_ft) {
    static const std::string sql =
        "INSERT OR REPLACE INTO zone_distance_cache "
        "(from_zone_id, to_zone_id, distance_ft) VALUES (?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(from_zone_id),
                     std::to_string(to_zone_id),
                     std::to_string(distance_ft)});
}

float KennelRepository::GetDistance(int64_t from_zone_id, int64_t to_zone_id) const {
    static const std::string sql =
        "SELECT distance_ft FROM zone_distance_cache "
        "WHERE from_zone_id = ? AND to_zone_id = ?";
    float dist = -1.0f;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(from_zone_id), std::to_string(to_zone_id)},
        [&](const auto&, const auto& vals) {
            if (!vals[0].empty()) dist = std::stof(vals[0]);
        });
    return dist;
}

void KennelRepository::SetKennelPurpose(int64_t kennel_id,
                                         domain::KennelPurpose purpose) {
    static const std::string sql =
        "UPDATE kennels SET current_purpose = ? WHERE kennel_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {PurposeToString(purpose), std::to_string(kennel_id)});
}

int64_t KennelRepository::InsertZone(const std::string& name,
                                      const std::string& building,
                                      const std::string& row_label,
                                      float x_ft, float y_ft) {
    static const std::string sql =
        "INSERT INTO zones (name, building, row_label, x_coord_ft, y_coord_ft) "
        "VALUES (?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {name, building, row_label,
                     std::to_string(x_ft), std::to_string(y_ft)});
    return conn->LastInsertRowId();
}

int64_t KennelRepository::InsertKennel(int64_t zone_id, const std::string& name,
                                        int capacity, int nightly_price_cents) {
    static const std::string sql =
        "INSERT INTO kennels (zone_id, name, capacity, nightly_price_cents) "
        "VALUES (?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(zone_id), name,
                     std::to_string(capacity),
                     std::to_string(nightly_price_cents)});
    return conn->LastInsertRowId();
}

void KennelRepository::InsertRestriction(int64_t kennel_id,
                                          domain::KennelRestrictionType type) {
    static const std::string sql =
        "INSERT OR IGNORE INTO kennel_restrictions (kennel_id, restriction_type) "
        "VALUES (?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(kennel_id), RestrictionToString(type)});
}

} // namespace shelterops::repositories
