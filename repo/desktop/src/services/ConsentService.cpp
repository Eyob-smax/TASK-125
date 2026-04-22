#include "shelterops/services/ConsentService.h"

namespace shelterops::services {

ConsentService::ConsentService(repositories::AdminRepository& admin,
                                AuditService& audit)
    : admin_(admin), audit_(audit) {}

int64_t ConsentService::RecordConsent(const std::string& entity_type,
                                       int64_t entity_id,
                                       const std::string& consent_type,
                                       const UserContext& user_ctx,
                                       int64_t now_unix) {
    (void)user_ctx;
    int64_t consent_id = admin_.InsertConsent(entity_type, entity_id,
                                               consent_type, now_unix);
    audit_.RecordSystemEvent("CONSENT_RECORDED",
        entity_type + " " + std::to_string(entity_id) +
        " consent=" + consent_type,
        now_unix);
    return consent_id;
}

void ConsentService::WithdrawConsent(int64_t consent_id,
                                      const UserContext& user_ctx,
                                      int64_t now_unix) {
    (void)user_ctx;
    admin_.WithdrawConsent(consent_id, now_unix);
    audit_.RecordSystemEvent("CONSENT_WITHDRAWN",
        "Consent " + std::to_string(consent_id) + " withdrawn",
        now_unix);
}

std::vector<repositories::ConsentRecord> ConsentService::ListConsentsFor(
    const std::string& entity_type, int64_t entity_id) const {
    return admin_.ListConsentsFor(entity_type, entity_id);
}

} // namespace shelterops::services
