#include "shelterops/repositories/AnimalRepository.h"

namespace shelterops::repositories {

AnimalRepository::AnimalRepository(infrastructure::Database& db) : db_(db) {}

domain::AnimalSpecies AnimalRepository::ParseSpecies(const std::string& s) {
    if (s == "dog")          return domain::AnimalSpecies::Dog;
    if (s == "cat")          return domain::AnimalSpecies::Cat;
    if (s == "rabbit")       return domain::AnimalSpecies::Rabbit;
    if (s == "bird")         return domain::AnimalSpecies::Bird;
    if (s == "reptile")      return domain::AnimalSpecies::Reptile;
    if (s == "small_animal") return domain::AnimalSpecies::SmallAnimal;
    return domain::AnimalSpecies::Other;
}

std::string AnimalRepository::SpeciesToString(domain::AnimalSpecies s) {
    switch (s) {
    case domain::AnimalSpecies::Dog:         return "dog";
    case domain::AnimalSpecies::Cat:         return "cat";
    case domain::AnimalSpecies::Rabbit:      return "rabbit";
    case domain::AnimalSpecies::Bird:        return "bird";
    case domain::AnimalSpecies::Reptile:     return "reptile";
    case domain::AnimalSpecies::SmallAnimal: return "small_animal";
    default:                                  return "other";
    }
}

domain::AnimalStatus AnimalRepository::ParseStatus(const std::string& s) {
    if (s == "intake")      return domain::AnimalStatus::Intake;
    if (s == "boarding")    return domain::AnimalStatus::Boarding;
    if (s == "adoptable")   return domain::AnimalStatus::Adoptable;
    if (s == "adopted")     return domain::AnimalStatus::Adopted;
    if (s == "transferred") return domain::AnimalStatus::Transferred;
    if (s == "deceased")    return domain::AnimalStatus::Deceased;
    if (s == "quarantine")  return domain::AnimalStatus::Quarantine;
    return domain::AnimalStatus::Intake;
}

std::string AnimalRepository::StatusToString(domain::AnimalStatus s) {
    switch (s) {
    case domain::AnimalStatus::Intake:      return "intake";
    case domain::AnimalStatus::Boarding:    return "boarding";
    case domain::AnimalStatus::Adoptable:   return "adoptable";
    case domain::AnimalStatus::Adopted:     return "adopted";
    case domain::AnimalStatus::Transferred: return "transferred";
    case domain::AnimalStatus::Deceased:    return "deceased";
    case domain::AnimalStatus::Quarantine:  return "quarantine";
    default:                                 return "intake";
    }
}

AnimalRecord AnimalRepository::RowToRecord(const std::vector<std::string>& vals) {
    // Columns: animal_id, name, species, breed, age_years, weight_lbs,
    //          is_aggressive, is_large_dog, intake_at, intake_type, status,
    //          notes, created_by, anonymized_at
    AnimalRecord r;
    r.animal_id     = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.name          = vals[1];
    r.species       = ParseSpecies(vals[2]);
    r.breed         = vals[3];
    r.age_years     = vals[4].empty() ? 0.0f : std::stof(vals[4]);
    r.weight_lbs    = vals[5].empty() ? 0.0f : std::stof(vals[5]);
    r.is_aggressive = vals[6] == "1";
    r.is_large_dog  = vals[7] == "1";
    r.intake_at     = vals[8].empty() ? 0 : std::stoll(vals[8]);
    r.intake_type   = vals[9];
    r.status        = ParseStatus(vals[10]);
    r.notes         = vals[11];
    r.created_by    = vals[12].empty() ? 0 : std::stoll(vals[12]);
    r.anonymized_at = vals[13].empty() ? 0 : std::stoll(vals[13]);
    return r;
}

int64_t AnimalRepository::Insert(const NewAnimalParams& params, int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO animals "
        "(name, species, breed, age_years, weight_lbs, is_aggressive, is_large_dog, "
        " intake_at, intake_type, status, notes, created_by) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'intake', ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {
        params.name,
        SpeciesToString(params.species),
        params.breed,
        std::to_string(params.age_years),
        std::to_string(params.weight_lbs),
        params.is_aggressive ? "1" : "0",
        params.is_large_dog  ? "1" : "0",
        std::to_string(now_unix),
        params.intake_type,
        params.notes,
        std::to_string(params.created_by)
    });
    return conn->LastInsertRowId();
}

std::optional<AnimalRecord> AnimalRepository::FindById(int64_t animal_id) const {
    static const std::string sql =
        "SELECT animal_id, COALESCE(name,''), species, COALESCE(breed,''), "
        "       COALESCE(age_years,0.0), COALESCE(weight_lbs,0.0), "
        "       is_aggressive, is_large_dog, intake_at, intake_type, status, "
        "       COALESCE(notes,''), COALESCE(created_by,0), COALESCE(anonymized_at,0) "
        "FROM animals WHERE animal_id = ?";
    std::optional<AnimalRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(animal_id)},
        [&](const auto&, const auto& vals) {
            result = RowToRecord(vals);
        });
    return result;
}

void AnimalRepository::UpdateStatus(int64_t animal_id, domain::AnimalStatus status) {
    static const std::string sql = "UPDATE animals SET status = ? WHERE animal_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {StatusToString(status), std::to_string(animal_id)});
}

std::vector<AdoptableListingRecord> AnimalRepository::ListAdoptableByKennel(
    int64_t kennel_id) const {
    static const std::string sql =
        "SELECT listing_id, animal_id, COALESCE(kennel_id,0), listing_date, "
        "       adoption_fee_cents, COALESCE(description,''), "
        "       COALESCE(rating,0.0), status, COALESCE(created_by,0), "
        "       COALESCE(adopted_at,0) "
        "FROM adoptable_listings WHERE kennel_id = ? AND status = 'active'";
    std::vector<AdoptableListingRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(kennel_id)},
        [&](const auto&, const auto& vals) {
            AdoptableListingRecord r;
            r.listing_id         = vals[0].empty() ? 0 : std::stoll(vals[0]);
            r.animal_id          = vals[1].empty() ? 0 : std::stoll(vals[1]);
            r.kennel_id          = vals[2].empty() ? 0 : std::stoll(vals[2]);
            r.listing_date       = vals[3].empty() ? 0 : std::stoll(vals[3]);
            r.adoption_fee_cents = vals[4].empty() ? 0 : std::stoi(vals[4]);
            r.description        = vals[5];
            r.rating             = vals[6].empty() ? 0.0f : std::stof(vals[6]);
            r.status             = vals[7];
            r.created_by         = vals[8].empty() ? 0 : std::stoll(vals[8]);
            r.adopted_at         = vals[9].empty() ? 0 : std::stoll(vals[9]);
            result.push_back(r);
        });
    return result;
}

int64_t AnimalRepository::InsertListing(int64_t animal_id, int64_t kennel_id,
                                         int adoption_fee_cents, int64_t now_unix,
                                         int64_t created_by) {
    static const std::string sql =
        "INSERT INTO adoptable_listings "
        "(animal_id, kennel_id, listing_date, adoption_fee_cents, status, created_by) "
        "VALUES (?, ?, ?, ?, 'active', ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(animal_id),
                     std::to_string(kennel_id),
                     std::to_string(now_unix),
                     std::to_string(adoption_fee_cents),
                     std::to_string(created_by)});
    return conn->LastInsertRowId();
}

void AnimalRepository::UpdateListingStatus(int64_t listing_id,
                                            const std::string& status) {
    static const std::string sql =
        "UPDATE adoptable_listings SET status = ? WHERE listing_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {status, std::to_string(listing_id)});
}

void AnimalRepository::Anonymize(int64_t animal_id, int64_t now_unix) {
    static const std::string sql =
        "UPDATE animals SET name='[anonymized]', breed=NULL, notes=NULL, "
        "  age_years=NULL, weight_lbs=NULL, anonymized_at=? "
        "WHERE animal_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(now_unix), std::to_string(animal_id)});
}

std::vector<AnimalRepository::RetentionCandidate>
AnimalRepository::ListRetentionCandidates(int64_t cutoff_unix) const {
    static const std::string sql =
        "SELECT animal_id, intake_at, CASE WHEN anonymized_at IS NOT NULL THEN 1 ELSE 0 END "
        "FROM animals WHERE intake_at < ?";
    std::vector<RetentionCandidate> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(cutoff_unix)},
        [&](const auto&, const auto& vals) {
            RetentionCandidate c;
            c.animal_id          = vals[0].empty() ? 0 : std::stoll(vals[0]);
            c.intake_at          = vals[1].empty() ? 0 : std::stoll(vals[1]);
            c.already_anonymized = vals[2] == "1";
            result.push_back(c);
        });
    return result;
}

bool AnimalRepository::DeleteForRetention(int64_t animal_id) {
    auto conn = db_.Acquire();
    try {
        conn->Exec("DELETE FROM adoptable_listings WHERE animal_id = ?",
                   {std::to_string(animal_id)});
        conn->Exec("DELETE FROM animals WHERE animal_id = ?",
                   {std::to_string(animal_id)});
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace shelterops::repositories
