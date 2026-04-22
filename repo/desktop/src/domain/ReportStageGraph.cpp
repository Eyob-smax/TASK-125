#include "shelterops/domain/ReportStageGraph.h"

namespace shelterops::domain {

const std::vector<ReportStage>& AllStages() noexcept {
    static const std::vector<ReportStage> stages = {
        ReportStage::Collect,
        ReportStage::Cleanse,
        ReportStage::Analyze,
        ReportStage::Visualize
    };
    return stages;
}

std::optional<ReportStage> NextStage(ReportStage current) noexcept {
    switch (current) {
    case ReportStage::Collect:   return ReportStage::Cleanse;
    case ReportStage::Cleanse:   return ReportStage::Analyze;
    case ReportStage::Analyze:   return ReportStage::Visualize;
    case ReportStage::Visualize: return std::nullopt;
    default:                      return std::nullopt;
    }
}

std::string StageName(ReportStage stage) noexcept {
    switch (stage) {
    case ReportStage::Collect:   return "collect";
    case ReportStage::Cleanse:   return "cleanse";
    case ReportStage::Analyze:   return "analyze";
    case ReportStage::Visualize: return "visualize";
    default:                      return "unknown";
    }
}

} // namespace shelterops::domain
