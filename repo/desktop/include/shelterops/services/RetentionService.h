#pragma once
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/AnimalRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/domain/RetentionPolicy.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/BookingService.h"    // for UserContext
#include <vector>
#include <string>
#include <cstdint>

namespace shelterops::services {

struct RetentionDecisionApplied {
    int64_t             entity_id = 0;
    std::string         entity_type;
    domain::RetentionActionKind action = domain::RetentionActionKind::Anonymize;
    std::string         reason;
};

struct RetentionReport {
    std::vector<RetentionDecisionApplied> applied;
    int                                   total_candidates = 0;
};

class RetentionService {
public:
    RetentionService(repositories::UserRepository&         users,
                     repositories::BookingRepository&      bookings,
                     repositories::AnimalRepository&       animals,
                     repositories::InventoryRepository&    inventory,
                     repositories::MaintenanceRepository&  maintenance,
                     repositories::AdminRepository&        admin,
                     AuditService&                         audit);

    // Run retention for all entity types. Loads rules from AdminRepository,
    // evaluates candidates with RetentionPolicy::EvaluateRetention.
    // audit_events is never touched.
    RetentionReport Run(int64_t now_unix, const UserContext& user_ctx);

private:
    repositories::UserRepository&        users_;
    repositories::BookingRepository&     bookings_;
    repositories::AnimalRepository&      animals_;
    repositories::InventoryRepository&   inventory_;
    repositories::MaintenanceRepository& maintenance_;
    repositories::AdminRepository&       admin_;
    AuditService&                        audit_;
};

} // namespace shelterops::services
