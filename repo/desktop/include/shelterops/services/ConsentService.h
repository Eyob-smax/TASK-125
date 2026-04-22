#pragma once
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/BookingService.h"    // for UserContext
#include <vector>
#include <string>
#include <cstdint>

namespace shelterops::services {

class ConsentService {
public:
    ConsentService(repositories::AdminRepository& admin,
                   AuditService&                  audit);

    int64_t RecordConsent(const std::string& entity_type, int64_t entity_id,
                           const std::string& consent_type,
                           const UserContext& user_ctx, int64_t now_unix);

    void WithdrawConsent(int64_t consent_id,
                         const UserContext& user_ctx, int64_t now_unix);

    std::vector<repositories::ConsentRecord> ListConsentsFor(
        const std::string& entity_type, int64_t entity_id) const;

private:
    repositories::AdminRepository& admin_;
    AuditService&                  audit_;
};

} // namespace shelterops::services
