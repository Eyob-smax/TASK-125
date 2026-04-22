#pragma once
#include "shelterops/workers/JobQueue.h"
#include "shelterops/services/ReportService.h"
#include "shelterops/services/ExportService.h"
#include "shelterops/services/RetentionService.h"
#include "shelterops/services/AlertService.h"
#include "shelterops/services/BookingService.h"    // for UserContext
#include <cstdint>

namespace shelterops::workers {

// Binds service-layer handlers to a JobQueue instance.
// Unimplemented types are registered with a clean INTERNAL error return.
class WorkerRegistry {
public:
    WorkerRegistry(JobQueue&                    queue,
                   services::ReportService&     reports,
                   services::ExportService&     exports,
                   services::RetentionService&  retention,
                   services::AlertService&      alerts);

    // Registers all handlers. Call before queue.Start().
    void RegisterAll();

private:
    JobQueue&                   queue_;
    services::ReportService&    reports_;
    services::ExportService&    exports_;
    services::RetentionService& retention_;
    services::AlertService&     alerts_;
};

} // namespace shelterops::workers
