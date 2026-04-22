#pragma once
#include "shelterops/services/BookingService.h"
#include "shelterops/services/InventoryService.h"
#include "shelterops/services/ReportService.h"
#include "shelterops/services/ExportService.h"
#include "shelterops/services/AlertService.h"
#include "shelterops/services/AutomationAuthMiddleware.h"
#include "shelterops/repositories/SessionRepository.h"
#include "shelterops/infrastructure/RateLimiter.h"
#include "shelterops/infrastructure/UpdateManager.h"
#include "shelterops/services/FieldMasker.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/common/ErrorEnvelope.h"
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace shelterops::services {

struct CommandEnvelope {
    std::string command;            // e.g. "kennel.search"
    nlohmann::json body;
    std::string session_token;
    std::string device_fingerprint;
};

struct CommandResult {
    int            http_status = 200;
    nlohmann::json body;
};

// In-process command dispatcher — the internal contract for the future HTTP adapter.
// Middleware chain: verify headers → rate limit → authorize → execute → mask → audit.
//
// Supported commands:
//   kennel.search, booking.create, booking.approve, booking.cancel,
//   inventory.issue, inventory.receive, report.trigger, report.status,
//   export.request, alerts.list, alerts.dismiss, update.import
class CommandDispatcher {
public:
    CommandDispatcher(BookingService&                        booking,
                      InventoryService&                      inventory,
                      ReportService&                         reports,
                      ExportService&                         exports,
                      AlertService&                          alerts,
                      repositories::SessionRepository&       sessions,
                      repositories::UserRepository&          users,
                      infrastructure::RateLimiter&           rate_limiter,
                      AuditService&                          audit,
                      infrastructure::UpdateManager*         update_manager = nullptr);

    CommandResult Dispatch(const CommandEnvelope& envelope, int64_t now_unix);

private:
    CommandResult HandleKennelSearch(const nlohmann::json& body,
                                     const UserContext& ctx, int64_t now_unix);
    CommandResult HandleBookingCreate(const nlohmann::json& body,
                                      const UserContext& ctx, int64_t now_unix);
    CommandResult HandleBookingApprove(const nlohmann::json& body,
                                       const UserContext& ctx, int64_t now_unix);
    CommandResult HandleBookingCancel(const nlohmann::json& body,
                                      const UserContext& ctx, int64_t now_unix);
    CommandResult HandleInventoryIssue(const nlohmann::json& body,
                                       const UserContext& ctx, int64_t now_unix);
    CommandResult HandleInventoryReceive(const nlohmann::json& body,
                                         const UserContext& ctx, int64_t now_unix);
    CommandResult HandleReportTrigger(const nlohmann::json& body,
                                      const UserContext& ctx, int64_t now_unix);
    CommandResult HandleReportStatus(const nlohmann::json& body,
                                     const UserContext& ctx, int64_t now_unix);
    CommandResult HandleExportRequest(const nlohmann::json& body,
                                      const UserContext& ctx, int64_t now_unix);
    CommandResult HandleAlertsList(const nlohmann::json& body,
                                   const UserContext& ctx, int64_t now_unix);
    CommandResult HandleAlertsDismiss(const nlohmann::json& body,
                                      const UserContext& ctx, int64_t now_unix);
    CommandResult HandleUpdateImport(const nlohmann::json& body,
                                     const UserContext& ctx, int64_t now_unix);

    static CommandResult ErrorResult(const common::ErrorEnvelope& env);
    static CommandResult NotFound();

    BookingService&                  booking_;
    InventoryService&                inventory_;
    ReportService&                   reports_;
    ExportService&                   exports_;
    AlertService&                    alerts_;
    repositories::SessionRepository& sessions_;
    repositories::UserRepository&    users_;
    infrastructure::RateLimiter&     rate_limiter_;
    AuditService&                    audit_;
    infrastructure::UpdateManager*   update_manager_;
};

} // namespace shelterops::services
