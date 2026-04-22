#include "shelterops/services/RetentionService.h"
#include <spdlog/spdlog.h>

namespace shelterops::services {

RetentionService::RetentionService(
    repositories::UserRepository&        users,
    repositories::BookingRepository&     bookings,
    repositories::AnimalRepository&      animals,
    repositories::InventoryRepository&   inventory,
    repositories::MaintenanceRepository& maintenance,
    repositories::AdminRepository&       admin,
    AuditService&                        audit)
    : users_(users), bookings_(bookings), animals_(animals), inventory_(inventory),
      maintenance_(maintenance), admin_(admin), audit_(audit) {}

RetentionReport RetentionService::Run(int64_t now_unix,
                                       const UserContext& user_ctx) {
    (void)user_ctx;
    RetentionReport report;

    // Load retention policies.
    auto policies = admin_.ListRetentionPolicies();

    // Build domain::RetentionRule list.
    std::vector<domain::RetentionRule> rules;
    for (const auto& p : policies) {
        domain::RetentionRule r;
        r.entity_type     = p.entity_type;
        r.retention_years = p.retention_years;
        r.action          = p.action;
        rules.push_back(r);
    }

    auto get_cutoff = [&](const std::string& entity_type) -> int64_t {
        for (const auto& r : rules) {
            if (r.entity_type == entity_type) {
                return now_unix - static_cast<int64_t>(r.retention_years) * 365 * 86400;
            }
        }
        // Default: 7 years.
        return now_unix - 7LL * 365 * 86400;
    };

    auto append_decision = [&](const domain::RetentionDecision& d) {
        RetentionDecisionApplied applied;
        applied.entity_id   = d.entity_id;
        applied.entity_type = d.entity_type;
        applied.action      = d.action;
        applied.reason      = d.reason;
        report.applied.push_back(applied);
    };

    auto record_event = [&](const std::string& entity, int64_t id,
                            domain::RetentionActionKind action,
                            const std::string& suffix) {
        const std::string action_name =
            (action == domain::RetentionActionKind::Delete) ? "delete" : "anonymize";
        audit_.RecordSystemEvent("RETENTION_APPLIED",
            entity + " " + std::to_string(id) +
            " action=" + action_name + " " + suffix,
            now_unix);
    };

    // --- Users ---
    {
        int64_t cutoff = get_cutoff("users");
        auto candidates_raw = users_.ListRetentionCandidates(cutoff);

        std::vector<domain::RetentionCandidate> candidates;
        for (const auto& c : candidates_raw) {
            domain::RetentionCandidate dc;
            dc.entity_id          = c.user_id;
            dc.entity_type        = "users";
            dc.created_at         = c.created_at;
            dc.already_anonymized = c.already_anonymized;
            candidates.push_back(dc);
        }

        auto decisions = domain::EvaluateRetention(candidates, rules, now_unix);
        for (const auto& d : decisions) {
            report.total_candidates++;
            bool deleted = false;
            if (d.action == domain::RetentionActionKind::Delete) {
                deleted = users_.DeleteForRetention(d.entity_id);
            }
            if (d.action == domain::RetentionActionKind::Anonymize || !deleted) {
                users_.Anonymize(d.entity_id, now_unix);
                record_event("User", d.entity_id, domain::RetentionActionKind::Anonymize,
                             deleted ? "" : "(delete-fallback)");
            } else {
                record_event("User", d.entity_id, domain::RetentionActionKind::Delete, "");
            }
            append_decision(d);
        }
    }

    // --- Bookings ---
    {
        int64_t cutoff = get_cutoff("bookings");
        auto candidates_raw = bookings_.ListRetentionCandidates(cutoff);

        std::vector<domain::RetentionCandidate> candidates;
        for (const auto& c : candidates_raw) {
            domain::RetentionCandidate dc;
            dc.entity_id          = c.booking_id;
            dc.entity_type        = "bookings";
            dc.created_at         = c.created_at;
            dc.already_anonymized = c.already_anonymized;
            candidates.push_back(dc);
        }

        auto decisions = domain::EvaluateRetention(candidates, rules, now_unix);
        for (const auto& d : decisions) {
            report.total_candidates++;
            bool deleted = false;
            if (d.action == domain::RetentionActionKind::Delete) {
                deleted = bookings_.DeleteForRetention(d.entity_id);
            }
            if (d.action == domain::RetentionActionKind::Anonymize || !deleted) {
                bookings_.AnonymizeForRetention(d.entity_id, now_unix);
                record_event("Booking", d.entity_id, domain::RetentionActionKind::Anonymize,
                             deleted ? "" : "(delete-fallback)");
            } else {
                record_event("Booking", d.entity_id, domain::RetentionActionKind::Delete, "");
            }
            append_decision(d);
        }
    }

    // --- Animals ---
    {
        int64_t cutoff = get_cutoff("animals");
        auto candidates_raw = animals_.ListRetentionCandidates(cutoff);

        std::vector<domain::RetentionCandidate> candidates;
        for (const auto& c : candidates_raw) {
            domain::RetentionCandidate dc;
            dc.entity_id          = c.animal_id;
            dc.entity_type        = "animals";
            dc.created_at         = c.intake_at;
            dc.already_anonymized = c.already_anonymized;
            candidates.push_back(dc);
        }

        auto decisions = domain::EvaluateRetention(candidates, rules, now_unix);
        for (const auto& d : decisions) {
            report.total_candidates++;
            bool deleted = false;
            if (d.action == domain::RetentionActionKind::Delete) {
                deleted = animals_.DeleteForRetention(d.entity_id);
            }
            if (d.action == domain::RetentionActionKind::Anonymize || !deleted) {
                animals_.Anonymize(d.entity_id, now_unix);
                record_event("Animal", d.entity_id, domain::RetentionActionKind::Anonymize,
                             deleted ? "" : "(delete-fallback)");
            } else {
                record_event("Animal", d.entity_id, domain::RetentionActionKind::Delete, "");
            }
            append_decision(d);
        }
    }

    // --- Inventory items ---
    {
        int64_t cutoff = get_cutoff("inventory_items");
        auto candidates_raw = inventory_.ListRetentionCandidates(cutoff);

        std::vector<domain::RetentionCandidate> candidates;
        for (const auto& c : candidates_raw) {
            domain::RetentionCandidate dc;
            dc.entity_id          = c.item_id;
            dc.entity_type        = "inventory_items";
            dc.created_at         = c.created_at;
            dc.already_anonymized = c.already_anonymized;
            candidates.push_back(dc);
        }

        auto decisions = domain::EvaluateRetention(candidates, rules, now_unix);
        for (const auto& d : decisions) {
            report.total_candidates++;
            bool deleted = false;
            if (d.action == domain::RetentionActionKind::Delete) {
                deleted = inventory_.DeleteForRetention(d.entity_id);
            }
            if (d.action == domain::RetentionActionKind::Anonymize || !deleted) {
                inventory_.Anonymize(d.entity_id, now_unix);
                record_event("InventoryItem", d.entity_id, domain::RetentionActionKind::Anonymize,
                             deleted ? "" : "(delete-fallback)");
            } else {
                record_event("InventoryItem", d.entity_id, domain::RetentionActionKind::Delete, "");
            }
            append_decision(d);
        }
    }

    return report;
}

} // namespace shelterops::services
