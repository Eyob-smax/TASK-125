#pragma once
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/domain/ReportStageGraph.h"
#include "shelterops/domain/ReportPipeline.h"
#include "shelterops/domain/Types.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/BookingService.h"    // for UserContext
#include "shelterops/common/ErrorEnvelope.h"
#include <vector>
#include <string>
#include <cstdint>

namespace shelterops::services {

class ReportService {
public:
    ReportService(repositories::ReportRepository&       reports,
                  repositories::KennelRepository&       kennels,
                  repositories::BookingRepository&      bookings,
                  repositories::InventoryRepository&    inventory,
                  repositories::MaintenanceRepository&  maintenance,
                  AuditService&                         audit,
                  std::string                           exports_dir = "exports");

    // Execute the four-stage pipeline (collect→cleanse→analyze→visualize).
    // Writes run record and per-metric snapshots. Returns run_id.
    // On any stage failure: marks run failed, sets anomaly_flags_json, emits audit.
    int64_t RunPipeline(int64_t report_id,
                        const std::string& filter_override_json,
                        const std::string& trigger_type,
                        const UserContext& user_ctx,
                        int64_t now_unix);

    // Compare two runs of the same report_id. Returns metric deltas.
    std::vector<domain::MetricDelta> CompareVersions(int64_t run_id_a,
                                                      int64_t run_id_b) const;

    std::vector<repositories::ReportRunRecord> ListRuns(int64_t report_id) const;
    std::optional<repositories::ReportRunRecord> FindRun(int64_t run_id) const;

    // Generates the version label for a run:
    // "<report_type>-<YYYYMMDD>-<NNN>" where NNN is zero-padded sequence.
    static std::string GenerateVersionLabel(int64_t report_id,
                                             const std::string& report_type,
                                             int64_t started_at_unix,
                                             int prior_runs_today);

private:
    repositories::ReportRepository&      reports_;
    repositories::KennelRepository&      kennels_;
    repositories::BookingRepository&     bookings_;
    repositories::InventoryRepository&   inventory_;
    repositories::MaintenanceRepository& maintenance_;
    AuditService&                        audit_;
    std::string                          exports_dir_;
};

} // namespace shelterops::services
