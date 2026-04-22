#pragma once
#include "shelterops/infrastructure/Database.h"
#include "shelterops/domain/Types.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::repositories {

struct AnimalRecord {
    int64_t     animal_id     = 0;
    std::string name;
    domain::AnimalSpecies species = domain::AnimalSpecies::Other;
    std::string breed;
    float       age_years     = 0.0f;
    float       weight_lbs    = 0.0f;
    bool        is_aggressive = false;
    bool        is_large_dog  = false;
    int64_t     intake_at     = 0;    // IMMUTABLE
    std::string intake_type;
    domain::AnimalStatus status = domain::AnimalStatus::Intake;
    std::string notes;
    int64_t     created_by    = 0;
    int64_t     anonymized_at = 0;    // 0 = not anonymized
};

struct NewAnimalParams {
    std::string name;
    domain::AnimalSpecies species = domain::AnimalSpecies::Other;
    std::string breed;
    float       age_years     = 0.0f;
    float       weight_lbs    = 0.0f;
    bool        is_aggressive = false;
    bool        is_large_dog  = false;
    std::string intake_type;
    std::string notes;
    int64_t     created_by    = 0;
};

struct AdoptableListingRecord {
    int64_t     listing_id         = 0;
    int64_t     animal_id          = 0;
    int64_t     kennel_id          = 0;
    int64_t     listing_date       = 0;    // IMMUTABLE
    int         adoption_fee_cents = 0;
    std::string description;
    float       rating             = 0.0f;
    std::string status;
    int64_t     created_by         = 0;
    int64_t     adopted_at         = 0;
};

class AnimalRepository {
public:
    explicit AnimalRepository(infrastructure::Database& db);

    // Insert a new animal; intake_at is set from now_unix. Returns animal_id.
    int64_t Insert(const NewAnimalParams& params, int64_t now_unix);

    std::optional<AnimalRecord> FindById(int64_t animal_id) const;

    void UpdateStatus(int64_t animal_id, domain::AnimalStatus status);

    // Returns active adoptable listings for the given kennel.
    std::vector<AdoptableListingRecord> ListAdoptableByKennel(int64_t kennel_id) const;

    // Insert a new adoptable listing. Returns listing_id.
    int64_t InsertListing(int64_t animal_id, int64_t kennel_id,
                           int adoption_fee_cents, int64_t now_unix,
                           int64_t created_by);

    void UpdateListingStatus(int64_t listing_id, const std::string& status);

    // Anonymize: replace PII with placeholders, set anonymized_at.
    void Anonymize(int64_t animal_id, int64_t now_unix);

    // Returns candidates for retention evaluation.
    struct RetentionCandidate {
        int64_t animal_id;
        int64_t intake_at;
        bool    already_anonymized;
    };
    std::vector<RetentionCandidate> ListRetentionCandidates(int64_t cutoff_unix) const;
    bool DeleteForRetention(int64_t animal_id);

private:
    static AnimalRecord RowToRecord(const std::vector<std::string>& vals);
    static domain::AnimalSpecies ParseSpecies(const std::string& s);
    static domain::AnimalStatus  ParseStatus(const std::string& s);
    static std::string SpeciesToString(domain::AnimalSpecies s);
    static std::string StatusToString(domain::AnimalStatus s);

    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
