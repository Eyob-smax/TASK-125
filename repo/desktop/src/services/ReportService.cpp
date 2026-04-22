#include "shelterops/services/ReportService.h"
#include "shelterops/infrastructure/AtomicFileWriter.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace shelterops::services {

namespace {

static std::string Lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

static bool ContainsValue(const std::vector<int64_t>& values, int64_t v) {
    return std::find(values.begin(), values.end(), v) != values.end();
}

} // namespace

ReportService::ReportService(
    repositories::ReportRepository&      reports,
    repositories::KennelRepository&      kennels,
    repositories::BookingRepository&     bookings,
    repositories::InventoryRepository&   inventory,
    repositories::MaintenanceRepository& maintenance,
    AuditService&                        audit,
    std::string                          exports_dir)
    : reports_(reports), kennels_(kennels), bookings_(bookings),
      inventory_(inventory), maintenance_(maintenance), audit_(audit),
      exports_dir_(exports_dir.empty() ? "exports" : std::move(exports_dir)) {}

std::string ReportService::GenerateVersionLabel(int64_t report_id,
                                                 const std::string& report_type,
                                                 int64_t started_at_unix,
                                                 int prior_runs_today) {
    // Format: "<report_type>-<YYYYMMDD>-<NNN>"
    std::time_t t = static_cast<std::time_t>(started_at_unix);
    std::tm tm_val{};
#if defined(_WIN32)
    gmtime_s(&tm_val, &t);
#else
    gmtime_r(&t, &tm_val);
#endif

    char date_buf[16];
    std::strftime(date_buf, sizeof(date_buf), "%Y%m%d", &tm_val);

    std::ostringstream ss;
    ss << report_type << "-" << date_buf << "-"
         << std::setfill('0') << std::setw(3) << (prior_runs_today + 1);

    (void)report_id;
    return ss.str();
}

int64_t ReportService::RunPipeline(int64_t report_id,
                                    const std::string& filter_override_json,
                                    const std::string& trigger_type,
                                    const UserContext& user_ctx,
                                    int64_t now_unix) {
    // Load definition.
    auto def = reports_.FindDefinition(report_id);
    if (!def) {
        spdlog::error("ReportService::RunPipeline: unknown report_id={}", report_id);
        return -1;
    }

    const std::string filter_json = filter_override_json.empty()
        ? def->filter_json : filter_override_json;

    // Generate version label.
    int prior_count = reports_.CountRunsForReportOnDay(report_id, now_unix);
    std::string version_label = GenerateVersionLabel(
        report_id, def->report_type, now_unix, prior_count);

    // Insert run record.
    int64_t run_id = reports_.InsertRun(report_id, version_label, trigger_type,
                                         user_ctx.user_id, filter_json, now_unix);

    auto fail_run = [&](const std::string& error,
                         const std::string& anomaly_json = "") {
        reports_.UpdateRunStatus(run_id, "failed", error, anomaly_json);
        audit_.RecordSystemEvent("JOB_FAILED",
            "Report run " + std::to_string(run_id) + " failed: " + error,
            now_unix);
    };

    // -----------------------------------------------------------------------
    // Stage: COLLECT
    // -----------------------------------------------------------------------
    domain::StageResult collect_result;
    collect_result.ok = true;

    nlohmann::json filter_obj;
    try {
        filter_obj = nlohmann::json::parse(filter_json);
    } catch (...) {
        fail_run("Invalid filter JSON");
        return run_id;
    }

    int64_t from_unix = filter_obj.value("date_from_unix", (int64_t)0);
    int64_t to_unix   = filter_obj.value("date_to_unix",   now_unix);
    std::vector<int64_t> zone_ids;
    if (filter_obj.contains("zone_ids") && filter_obj["zone_ids"].is_array()) {
        for (const auto& z : filter_obj["zone_ids"]) {
            if (z.is_number_integer()) zone_ids.push_back(z.get<int64_t>());
        }
    }
    if (zone_ids.empty() && filter_obj.contains("zone_id") && filter_obj["zone_id"].is_number_integer()) {
        zone_ids.push_back(filter_obj["zone_id"].get<int64_t>());
    }

    const int64_t staff_owner_id = filter_obj.value("staff_owner_id",
        filter_obj.value("staff_owner", int64_t{0}));
    const std::string pet_type = Lower(filter_obj.value("pet_type", std::string{}));

    // -----------------------------------------------------------------------
    // Stage: CLEANSE + ANALYZE + VISUALIZE — per report type
    // -----------------------------------------------------------------------
    nlohmann::json metrics = nlohmann::json::object();
    std::vector<std::string> anomaly_flags;

    try {
        if (def->report_type == "occupancy") {
            // Collect occupancy points.
            auto all_kennels = kennels_.ListActiveKennels();
            if (!zone_ids.empty()) {
                all_kennels.erase(
                    std::remove_if(all_kennels.begin(), all_kennels.end(), [&](const auto& k) {
                        return !ContainsValue(zone_ids, k.zone_id);
                    }),
                    all_kennels.end());
            }
            int total_kennels = static_cast<int>(all_kennels.size());

            // Cleanse: require at least one kennel.
            if (total_kennels == 0) {
                anomaly_flags.push_back("no_kennels");
            }

            // Analyze: compute average occupancy.
            // For this snapshot we compute: occupied = kennels with active bookings now.
            domain::DateRange window{from_unix, to_unix};
            int occupied = 0;
            for (const auto& k : all_kennels) {
                auto overlapping = bookings_.ListOverlapping(k.kennel_id, window);
                if (staff_owner_id > 0 || !pet_type.empty()) {
                    std::vector<domain::ExistingBooking> filtered;
                    for (const auto& ob : overlapping) {
                        auto full = bookings_.FindById(ob.booking_id);
                        if (!full) continue;
                        if (staff_owner_id > 0 && full->created_by != staff_owner_id) continue;
                        if (!pet_type.empty()) {
                            const std::string req_l = Lower(full->special_requirements);
                            if (req_l.find(pet_type) == std::string::npos) continue;
                        }
                        filtered.push_back(ob);
                    }
                    overlapping.swap(filtered);
                }
                if (!overlapping.empty()) occupied++;
            }

            double occ_rate = domain::ComputeOccupancyRate(occupied, total_kennels);
            metrics["avg_occupancy_pct"] = occ_rate;
            metrics["total_kennels"]     = total_kennels;
            metrics["occupied_kennels"]  = occupied;

        } else if (def->report_type == "maintenance_response") {
            auto tickets = maintenance_.ListTicketsInRange(from_unix, to_unix);
            if (!zone_ids.empty() || staff_owner_id > 0) {
                tickets.erase(
                    std::remove_if(tickets.begin(), tickets.end(), [&](const auto& t) {
                        if (!zone_ids.empty() && !ContainsValue(zone_ids, t.zone_id)) return true;
                        if (staff_owner_id > 0 && t.assigned_to != staff_owner_id &&
                            t.created_by != staff_owner_id) return true;
                        return false;
                    }),
                    tickets.end());
            }
            std::vector<domain::MaintenanceResponsePoint> points;
            points.reserve(tickets.size());
            for (const auto& t : tickets) {
                domain::MaintenanceResponsePoint p;
                p.ticket_id  = t.ticket_id;
                p.created_at = t.created_at;
                if (t.first_action_at > 0) p.first_action_at = t.first_action_at;
                if (t.resolved_at > 0)     p.resolved_at     = t.resolved_at;
                points.push_back(p);
            }

            int unacked = 0;
            double avg_hours = domain::ComputeAvgMaintenanceResponseHours(points, &unacked);
            metrics["avg_response_hours"]   = avg_hours;
            metrics["unacknowledged_count"] = unacked;
            metrics["ticket_count"]         = static_cast<int>(points.size());

            if (unacked > 0) {
                anomaly_flags.push_back("unacknowledged_tickets:" + std::to_string(unacked));
            }

        } else if (def->report_type == "overdue_fees") {
            auto fees = bookings_.ListUnpaidFees(now_unix);
            if (staff_owner_id > 0 || !pet_type.empty() || !zone_ids.empty()) {
                std::unordered_map<int64_t, int64_t> kennel_zone;
                if (!zone_ids.empty()) {
                    for (const auto& k : kennels_.ListActiveKennels()) {
                        kennel_zone[k.kennel_id] = k.zone_id;
                    }
                }
                fees.erase(
                    std::remove_if(fees.begin(), fees.end(), [&](const auto& fee) {
                        auto bk = bookings_.FindById(fee.booking_id);
                        if (!bk) return true;
                        if (staff_owner_id > 0 && bk->created_by != staff_owner_id) return true;
                        if (!pet_type.empty()) {
                            if (Lower(bk->special_requirements).find(pet_type) == std::string::npos)
                                return true;
                        }
                        if (!zone_ids.empty()) {
                            auto it = kennel_zone.find(bk->kennel_id);
                            if (it == kennel_zone.end() || !ContainsValue(zone_ids, it->second))
                                return true;
                        }
                        return false;
                    }),
                    fees.end());
            }
            auto buckets = domain::ComputeOverdueFeeDistribution(fees, now_unix);

            int64_t total_overdue = 0;
            for (const auto& b : buckets) {
                total_overdue += b.total_cents;
                std::string key = std::to_string(b.min_days_inclusive) + "_to_" +
                    (b.max_days_inclusive < 0 ? "inf" : std::to_string(b.max_days_inclusive));
                metrics["bucket_count_" + key] = b.count;
                metrics["bucket_cents_" + key] = b.total_cents;
            }
            metrics["total_overdue_cents"] = total_overdue;

        } else if (def->report_type == "kennel_turnover") {
            // Kennel turnover: number of completed bookings / total kennel-days in window.
            domain::DateRange window{from_unix, to_unix};
            auto completed = bookings_.ListCompletedInRange(window);
            auto all_kennels = kennels_.ListActiveKennels();

            if (!zone_ids.empty()) {
                all_kennels.erase(
                    std::remove_if(all_kennels.begin(), all_kennels.end(), [&](const auto& k) {
                        return !ContainsValue(zone_ids, k.zone_id);
                    }),
                    all_kennels.end());
            }

            if (staff_owner_id > 0 || !pet_type.empty() || !zone_ids.empty()) {
                completed.erase(
                    std::remove_if(completed.begin(), completed.end(), [&](const auto& b) {
                        if (staff_owner_id > 0 && b.created_by != staff_owner_id) return true;
                        if (!pet_type.empty()) {
                            const std::string req_l = Lower(b.special_requirements);
                            if (req_l.find(pet_type) == std::string::npos) return true;
                        }
                        if (!zone_ids.empty()) {
                            auto it = std::find_if(all_kennels.begin(), all_kennels.end(),
                                [&](const auto& k) { return k.kennel_id == b.kennel_id; });
                            if (it == all_kennels.end()) return true;
                        }
                        return false;
                    }),
                    completed.end());
            }

            int  total_kennels = static_cast<int>(all_kennels.size());

            int64_t window_days = (to_unix > from_unix)
                ? ((to_unix - from_unix) / 86400) : 1;
            int64_t kennel_days = total_kennels * window_days;

            double turnover_rate = (kennel_days > 0)
                ? (static_cast<double>(completed.size()) / static_cast<double>(kennel_days))
                : 0.0;

            metrics["completed_stays"]  = static_cast<int>(completed.size());
            metrics["total_kennels"]    = total_kennels;
            metrics["window_days"]      = static_cast<int>(window_days);
            metrics["turnover_rate"]    = turnover_rate;

            if (completed.empty() && total_kennels > 0) {
                anomaly_flags.push_back("zero_turnover_in_window");
            }

        } else if (def->report_type == "inventory_summary") {
            auto items = inventory_.ListAllActiveItems();
            if (!pet_type.empty()) {
                items.erase(
                    std::remove_if(items.begin(), items.end(), [&](const auto& item) {
                        const std::string combined = Lower(item.name) + " " + Lower(item.description);
                        return combined.find(pet_type) == std::string::npos;
                    }),
                    items.end());
            }
            metrics["total_items"]  = static_cast<int>(items.size());
            int low = 0;
            for (const auto& item : items) {
                if (item.quantity == 0) low++;
            }
            metrics["zero_stock_items"] = low;

        } else {
            // Generic/custom: record row count only.
            metrics["report_type"] = def->report_type;
        }
    } catch (const std::exception& ex) {
        fail_run(std::string("Pipeline exception: ") + ex.what(),
                  "[\"pipeline_exception\"]");
        return run_id;
    }

    // Visualize: write output.
    std::string output_path = exports_dir_ + "/" + version_label + ".json";
    try {
        nlohmann::json out;
        out["run_id"]       = run_id;
        out["version"]      = version_label;
        out["report_type"]  = def->report_type;
        out["metrics"]      = metrics;
        out["anomaly_flags"] = anomaly_flags;
        infrastructure::AtomicFileWriter::WriteAtomic(output_path, out.dump(2));
    } catch (const std::exception& ex) {
        spdlog::warn("ReportService: output write failed ({}), continuing", ex.what());
        output_path = "";
    }

    // Persist per-metric snapshots.
    for (auto it = metrics.begin(); it != metrics.end(); ++it) {
        double val = 0.0;
        if (it->is_number()) val = it->get<double>();
        reports_.InsertSnapshot(run_id, it.key(), val, "{}", now_unix);
    }

    // Build anomaly_flags_json.
    std::string anom_json;
    if (!anomaly_flags.empty()) {
        nlohmann::json anom = anomaly_flags;
        anom_json = anom.dump();
    }

    if (!anomaly_flags.empty()) {
        reports_.UpdateRunStatus(run_id, "completed", "", anom_json);
    }
    reports_.FinalizeRun(run_id, output_path,
                          static_cast<int>(metrics.size()), now_unix);

    audit_.RecordSystemEvent("REPORT_RUN_COMPLETED",
        "Report " + std::to_string(report_id) +
        " run " + std::to_string(run_id) +
        " completed (" + version_label + ")",
        now_unix);

    return run_id;
}

std::vector<domain::MetricDelta> ReportService::CompareVersions(
    int64_t run_id_a, int64_t run_id_b) const {
    auto snaps_a = reports_.ListSnapshotsForRun(run_id_a);
    auto snaps_b = reports_.ListSnapshotsForRun(run_id_b);

    std::vector<std::pair<std::string, double>> before, after;
    for (const auto& s : snaps_a) before.emplace_back(s.metric_name, s.metric_value);
    for (const auto& s : snaps_b) after.emplace_back(s.metric_name, s.metric_value);

    return domain::ComputeVersionDelta(before, after);
}

std::vector<repositories::ReportRunRecord> ReportService::ListRuns(
    int64_t report_id) const {
    return reports_.ListRunsForReport(report_id);
}

std::optional<repositories::ReportRunRecord> ReportService::FindRun(int64_t run_id) const {
    return reports_.FindRun(run_id);
}

} // namespace shelterops::services
