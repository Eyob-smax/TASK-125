#pragma once
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/BookingService.h"    // for UserContext
#include "shelterops/common/ErrorEnvelope.h"
#include <variant>
#include <vector>
#include <string>
#include <cstdint>

namespace shelterops::services {

using ExportJobResult = std::variant<int64_t, common::ErrorEnvelope>;

class ExportService {
public:
    ExportService(repositories::ReportRepository& reports,
                  repositories::AdminRepository&  admin,
                  AuditService&                   audit,
                  std::string                     exports_dir = "exports");

    // Check export permission; on failure emit EXPORT_UNAUTHORIZED audit + error.
    // On success insert export_job in 'queued' state and return job_id.
    ExportJobResult RequestExport(int64_t run_id,
                                  const std::string& format,
                                  const UserContext& user_ctx,
                                  int64_t now_unix);

    // Execute the export job (called by a JobQueue worker).
    // Writes CSV/PDF content via AtomicFileWriter; updates job status.
    common::ErrorEnvelope RunExportJob(int64_t job_id, int64_t now_unix);

    // Returns queued export jobs for the JobQueue dispatcher.
    std::vector<repositories::ExportJobRecord> ListPending() const;

private:
    repositories::ReportRepository& reports_;
    repositories::AdminRepository&  admin_;
    AuditService&                   audit_;
    std::string                     exports_dir_;
};

} // namespace shelterops::services
