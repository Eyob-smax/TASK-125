#pragma once
#include <optional>
#include <string>
#include <vector>

namespace shelterops::domain {

enum class ReportStage {
    Collect,
    Cleanse,
    Analyze,
    Visualize
};

struct StageResult {
    bool                     ok            = true;
    std::vector<std::string> anomaly_flags;   // non-empty means anomalies detected
};

// Returns the ordered sequence of all report stages.
const std::vector<ReportStage>& AllStages() noexcept;

// Returns the next stage after `current`, or nullopt if `current` is the last.
std::optional<ReportStage> NextStage(ReportStage current) noexcept;

// Returns the string name of the stage for logging.
std::string StageName(ReportStage stage) noexcept;

} // namespace shelterops::domain
