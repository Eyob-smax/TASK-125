#include "shelterops/services/CommandDispatcher.h"
#include "shelterops/services/AuthorizationService.h"
#include "shelterops/common/ErrorEnvelope.h"
#include "shelterops/infrastructure/UpdateManager.h"
#include <spdlog/spdlog.h>

namespace shelterops::services {

static domain::UserRole ParseRole(const std::string& s) {
    if (s == "administrator")      return domain::UserRole::Administrator;
    if (s == "operations_manager") return domain::UserRole::OperationsManager;
    if (s == "inventory_clerk")    return domain::UserRole::InventoryClerk;
    return domain::UserRole::Auditor;
}

CommandDispatcher::CommandDispatcher(
    BookingService&                  booking,
    InventoryService&                inventory,
    ReportService&                   reports,
    ExportService&                   exports,
    AlertService&                    alerts,
    repositories::SessionRepository& sessions,
    repositories::UserRepository&    users,
    infrastructure::RateLimiter&     rate_limiter,
    AuditService&                    audit,
    infrastructure::UpdateManager*   update_manager)
    : booking_(booking), inventory_(inventory), reports_(reports),
      exports_(exports), alerts_(alerts), sessions_(sessions),
      users_(users), rate_limiter_(rate_limiter), audit_(audit),
      update_manager_(update_manager) {}

// ---------------------------------------------------------------------------
// Middleware helpers
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

CommandResult CommandDispatcher::Dispatch(const CommandEnvelope& envelope,
                                           int64_t now_unix) {
    // Device fingerprint is mandatory for automation command execution.
    if (envelope.device_fingerprint.empty()) {
        common::ErrorEnvelope err{common::ErrorCode::Unauthorized,
                                   "Missing device fingerprint"};
        audit_.RecordSystemEvent("COMMAND_UNAUTHORIZED",
            "Command '" + envelope.command + "' rejected: missing device fingerprint",
            now_unix);
        return ErrorResult(err);
    }

    // 1. Verify session token.
    auto session = sessions_.FindById(envelope.session_token);
    if (!session || !session->is_active || session->expires_at < now_unix) {
        common::ErrorEnvelope err{common::ErrorCode::Unauthorized,
                                   "Invalid or expired session token"};
        audit_.RecordSystemEvent("COMMAND_UNAUTHORIZED",
            "Command '" + envelope.command + "' rejected: invalid session",
            now_unix);
        return ErrorResult(err);
    }
    if (session->device_fingerprint.empty() ||
        session->device_fingerprint != envelope.device_fingerprint) {
        common::ErrorEnvelope err{common::ErrorCode::Unauthorized,
                                   "Device fingerprint mismatch"};
        audit_.RecordSystemEvent("COMMAND_UNAUTHORIZED",
            "Command '" + envelope.command + "' rejected: device fingerprint mismatch",
            now_unix);
        return ErrorResult(err);
    }

    // 2. Rate limit.
    auto rl = rate_limiter_.TryAcquire(envelope.session_token);
    if (!rl.allowed) {
        CommandResult result;
        result.http_status = 429;
        result.body = {
            {"ok", false},
            {"error", {
                {"code", "RATE_LIMITED"},
                {"message", "Too many requests"},
                {"retry_after", rl.retry_after_seconds}
            }}
        };
        audit_.RecordSystemEvent("COMMAND_RATE_LIMITED",
            "Command '" + envelope.command + "' rate-limited",
            now_unix);
        return result;
    }

    // Build user context — look up real role from UserRepository.
    auto user_opt = users_.FindById(session->user_id);
    if (!user_opt) {
        common::ErrorEnvelope err{common::ErrorCode::Unauthorized,
                                   "Session user not found"};
        return ErrorResult(err);
    }
    UserContext ctx;
    ctx.user_id      = session->user_id;
    ctx.session_id   = session->session_id;
    ctx.role_string  = user_opt->role;
    ctx.role         = ParseRole(user_opt->role);

    // 3. Route to handler.
    CommandResult result;
    const std::string& cmd = envelope.command;

    if (cmd == "kennel.search") {
        result = HandleKennelSearch(envelope.body, ctx, now_unix);
    } else if (cmd == "booking.create") {
        result = HandleBookingCreate(envelope.body, ctx, now_unix);
    } else if (cmd == "booking.approve") {
        result = HandleBookingApprove(envelope.body, ctx, now_unix);
    } else if (cmd == "booking.cancel") {
        result = HandleBookingCancel(envelope.body, ctx, now_unix);
    } else if (cmd == "inventory.issue") {
        result = HandleInventoryIssue(envelope.body, ctx, now_unix);
    } else if (cmd == "inventory.receive") {
        result = HandleInventoryReceive(envelope.body, ctx, now_unix);
    } else if (cmd == "report.trigger") {
        result = HandleReportTrigger(envelope.body, ctx, now_unix);
    } else if (cmd == "report.status") {
        result = HandleReportStatus(envelope.body, ctx, now_unix);
    } else if (cmd == "export.request") {
        result = HandleExportRequest(envelope.body, ctx, now_unix);
    } else if (cmd == "alerts.list") {
        result = HandleAlertsList(envelope.body, ctx, now_unix);
    } else if (cmd == "alerts.dismiss") {
        result = HandleAlertsDismiss(envelope.body, ctx, now_unix);
    } else if (cmd == "update.import") {
        result = HandleUpdateImport(envelope.body, ctx, now_unix);
    } else {
        result = NotFound();
    }

    // 4. Mask response for Auditor role.
    if (ctx.role == domain::UserRole::Auditor && result.body.is_object()) {
        if (result.body.contains("data") && result.body["data"].is_object()) {
            auto& data = result.body["data"];
            for (auto it = data.begin(); it != data.end(); ++it) {
                if (it.value().is_string()) {
                    it.value() = FieldMasker::MaskField(
                        ctx.role, "command_response", it.key(),
                        it.value().get<std::string>());
                }
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

CommandResult CommandDispatcher::HandleKennelSearch(const nlohmann::json& body,
                                                     const UserContext& ctx,
                                                     int64_t now_unix) {
    domain::BookingSearchFilter filter;
    try {
        filter.window.from_unix    = body.value("check_in_at",  int64_t{0});
        filter.window.to_unix      = body.value("check_out_at", int64_t{0});
        filter.is_aggressive       = body.value("is_aggressive", false);
        filter.is_large_dog        = body.value("is_large_dog",  false);
        filter.min_rating          = body.value("min_rating",    0.0f);
        filter.max_nightly_price_cents = body.value("max_nightly_price_cents", 0);
        filter.only_bookable       = body.value("only_bookable", true);
        if (body.contains("zone_ids") && body["zone_ids"].is_array()) {
            for (const auto& z : body["zone_ids"])
                filter.zone_ids.push_back(z.get<int64_t>());
        }
    } catch (...) {
        return ErrorResult({common::ErrorCode::InvalidInput,
                            "Invalid kennel.search filter parameters"});
    }

    auto ranked = booking_.SearchAndRank(filter, ctx, now_unix);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : ranked) {
        nlohmann::json entry;
        entry["kennel_id"]     = r.kennel.kennel_id;
        entry["kennel_name"]   = r.kennel.name;
        entry["zone_id"]       = r.kennel.zone_id;
        entry["score"]         = r.score;
        entry["nightly_cents"] = r.kennel.nightly_price_cents;
        nlohmann::json reasons = nlohmann::json::array();
        for (const auto& reason : r.reasons) {
            nlohmann::json rj;
            rj["code"]   = reason.code;
            rj["detail"] = reason.detail;
            reasons.push_back(rj);
        }
        entry["reasons"] = reasons;
        arr.push_back(entry);
    }

    CommandResult result;
    result.http_status = 200;
    result.body = common::ErrorEnvelope::SuccessJson(arr);
    return result;
}

CommandResult CommandDispatcher::HandleBookingCreate(const nlohmann::json& body,
                                                      const UserContext& ctx,
                                                      int64_t now_unix) {
    auto denied = AuthorizationService::RequireWrite(ctx.role);
    if (denied) return ErrorResult(*denied);

    CreateBookingRequest req;
    try {
        req.kennel_id     = body.at("kennel_id").get<int64_t>();
        req.animal_id     = body.value("animal_id", int64_t{0});
        req.guest_name    = body.value("guest_name", "");
        req.check_in_at   = body.at("check_in_at").get<int64_t>();
        req.check_out_at  = body.at("check_out_at").get<int64_t>();
        req.special_requirements = body.value("special_requirements", "");
        req.idempotency_key      = body.value("idempotency_key", "");
        // Sensitive contact fields are encrypted inside BookingService before persistence.
        req.guest_phone_enc = body.value("guest_phone_enc", "");
        req.guest_email_enc = body.value("guest_email_enc", "");
    } catch (...) {
        return ErrorResult({common::ErrorCode::InvalidInput,
                            "Missing required booking fields"});
    }

    auto result_v = booking_.CreateBooking(req, ctx, now_unix);
    if (auto* err = std::get_if<common::ErrorEnvelope>(&result_v))
        return ErrorResult(*err);

    int64_t booking_id = std::get<int64_t>(result_v);
    CommandResult result;
    result.http_status = 201;
    result.body = common::ErrorEnvelope::SuccessJson({{"booking_id", booking_id}});
    return result;
}

CommandResult CommandDispatcher::HandleBookingApprove(const nlohmann::json& body,
                                                       const UserContext& ctx,
                                                       int64_t now_unix) {
    auto denied = AuthorizationService::RequireBookingApproval(ctx.role);
    if (denied) return ErrorResult(*denied);

    int64_t booking_id = 0;
    try { booking_id = body.at("booking_id").get<int64_t>(); }
    catch (...) { return ErrorResult({common::ErrorCode::InvalidInput,
                                      "booking_id required"}); }

    auto result_v = booking_.ApproveBooking(booking_id, ctx, now_unix);
    if (auto* err = std::get_if<common::ErrorEnvelope>(&result_v))
        return ErrorResult(*err);

    CommandResult result;
    result.http_status = 200;
    result.body = common::ErrorEnvelope::SuccessJson({{"booking_id", booking_id}});
    return result;
}

CommandResult CommandDispatcher::HandleBookingCancel(const nlohmann::json& body,
                                                      const UserContext& ctx,
                                                      int64_t now_unix) {
    auto denied = AuthorizationService::RequireWrite(ctx.role);
    if (denied) return ErrorResult(*denied);

    int64_t booking_id = 0;
    try { booking_id = body.at("booking_id").get<int64_t>(); }
    catch (...) { return ErrorResult({common::ErrorCode::InvalidInput,
                                      "booking_id required"}); }

    auto result_v = booking_.CancelBooking(booking_id, ctx, now_unix);
    if (auto* err = std::get_if<common::ErrorEnvelope>(&result_v))
        return ErrorResult(*err);

    CommandResult result;
    result.http_status = 200;
    result.body = common::ErrorEnvelope::SuccessJson({{"booking_id", booking_id}});
    return result;
}

CommandResult CommandDispatcher::HandleInventoryIssue(const nlohmann::json& body,
                                                       const UserContext& ctx,
                                                       int64_t now_unix) {
    auto denied = AuthorizationService::RequireInventoryAccess(ctx.role);
    if (denied) return ErrorResult(*denied);

    int64_t     item_id  = 0;
    int         quantity = 0;
    std::string reason;
    try {
        item_id  = body.at("item_id").get<int64_t>();
        quantity = body.at("quantity").get<int>();
        reason   = body.value("reason", "");
    } catch (...) {
        return ErrorResult({common::ErrorCode::InvalidInput,
                            "item_id and quantity required"});
    }

    if (auto err = inventory_.IssueUnits(item_id, quantity, reason, ctx, now_unix))
        return ErrorResult(*err);

    CommandResult result;
    result.http_status = 200;
    result.body = common::ErrorEnvelope::SuccessJson();
    return result;
}

CommandResult CommandDispatcher::HandleInventoryReceive(const nlohmann::json& body,
                                                         const UserContext& ctx,
                                                         int64_t now_unix) {
    auto denied = AuthorizationService::RequireInventoryAccess(ctx.role);
    if (denied) return ErrorResult(*denied);

    int64_t     item_id       = 0;
    int         quantity      = 0;
    std::string vendor;
    std::string lot_number;
    int         unit_cost_cents = 0;
    try {
        item_id       = body.at("item_id").get<int64_t>();
        quantity      = body.at("quantity").get<int>();
        vendor        = body.value("vendor", "");
        lot_number    = body.value("lot_number", "");
        unit_cost_cents = body.value("unit_cost_cents", 0);
    } catch (...) {
        return ErrorResult({common::ErrorCode::InvalidInput,
                            "item_id and quantity required"});
    }

    if (auto err = inventory_.ReceiveStock(item_id, quantity, vendor, lot_number,
                                           unit_cost_cents, ctx, now_unix))
        return ErrorResult(*err);

    CommandResult result;
    result.http_status = 200;
    result.body = common::ErrorEnvelope::SuccessJson();
    return result;
}

CommandResult CommandDispatcher::HandleReportTrigger(const nlohmann::json& body,
                                                      const UserContext& ctx,
                                                      int64_t now_unix) {
    if (auto denied = AuthorizationService::RequireReportTrigger(ctx.role))
        return ErrorResult(*denied);

    int64_t     report_id = 0;
    std::string filter_json;
    try {
        report_id   = body.at("report_id").get<int64_t>();
        filter_json = body.value("filter_json", "{}");
    } catch (...) {
        return ErrorResult({common::ErrorCode::InvalidInput, "report_id required"});
    }

    int64_t run_id = reports_.RunPipeline(report_id, filter_json,
                                           "manual", ctx, now_unix);
    if (run_id <= 0) {
        return ErrorResult({common::ErrorCode::NotFound, "report_id not found"});
    }

    CommandResult result;
    result.http_status = 202;
    result.body = common::ErrorEnvelope::SuccessJson({{"run_id", run_id}});
    return result;
}

CommandResult CommandDispatcher::HandleReportStatus(const nlohmann::json& body,
                                                     const UserContext& ctx,
                                                     int64_t /*now_unix*/) {
    // Spec §8.3: report.status is allowed for any authenticated role.
    if (auto denied = AuthorizationService::RequireReportAccess(ctx.role))
        return ErrorResult(*denied);

    int64_t report_id = 0;
    if (body.contains("run_id")) {
        int64_t run_id = 0;
        try { run_id = body.at("run_id").get<int64_t>(); }
        catch (...) { return ErrorResult({common::ErrorCode::InvalidInput,
                                          "run_id must be an integer"}); }

        auto run = reports_.FindRun(run_id);
        if (!run) {
            return ErrorResult({common::ErrorCode::NotFound, "run_id not found"});
        }

        CommandResult result;
        result.http_status = 200;
        result.body = common::ErrorEnvelope::SuccessJson({
            {"run_id", run->run_id},
            {"status", run->status},
            {"version_label", run->version_label},
            {"started_at", run->started_at},
            {"completed_at", run->completed_at},
            {"output_path", run->output_path},
            {"error_message", run->error_message}
        });
        return result;
    }

    try { report_id = body.at("report_id").get<int64_t>(); }
    catch (...) { return ErrorResult({common::ErrorCode::InvalidInput,
                                      "report_id or run_id required"}); }

    auto runs = reports_.ListRuns(report_id);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : runs) {
        nlohmann::json entry;
        entry["run_id"]       = r.run_id;
        entry["status"]       = r.status;
        entry["version_label"] = r.version_label;
        entry["started_at"]   = r.started_at;
        arr.push_back(entry);
    }

    CommandResult result;
    result.http_status = 200;
    result.body = common::ErrorEnvelope::SuccessJson(arr);
    return result;
}

CommandResult CommandDispatcher::HandleExportRequest(const nlohmann::json& body,
                                                      const UserContext& ctx,
                                                      int64_t now_unix) {
    int64_t     run_id = 0;
    std::string format;
    try {
        run_id = body.at("run_id").get<int64_t>();
        format = body.at("format").get<std::string>();
    } catch (...) {
        return ErrorResult({common::ErrorCode::InvalidInput,
                            "run_id and format required"});
    }

    auto export_result = exports_.RequestExport(run_id, format, ctx, now_unix);
    if (auto* err = std::get_if<common::ErrorEnvelope>(&export_result))
        return ErrorResult(*err);

    int64_t job_id = std::get<int64_t>(export_result);
    CommandResult result;
    result.http_status = 202;
    result.body = common::ErrorEnvelope::SuccessJson({{"job_id", job_id}});
    return result;
}

CommandResult CommandDispatcher::HandleAlertsList(const nlohmann::json& /*body*/,
                                                   const UserContext& /*ctx*/,
                                                   int64_t /*now_unix*/) {
    auto active = alerts_.ListActive();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& a : active) {
        nlohmann::json entry;
        entry["alert_id"]   = a.alert_id;
        entry["item_id"]    = a.item_id;
        entry["alert_type"] = a.alert_type;
        entry["triggered_at"] = a.triggered_at;
        arr.push_back(entry);
    }

    CommandResult result;
    result.http_status = 200;
    result.body = common::ErrorEnvelope::SuccessJson(arr);
    return result;
}

CommandResult CommandDispatcher::HandleAlertsDismiss(const nlohmann::json& body,
                                                      const UserContext& ctx,
                                                      int64_t now_unix) {
    int64_t alert_id = 0;
    try { alert_id = body.at("alert_id").get<int64_t>(); }
    catch (...) { return ErrorResult({common::ErrorCode::InvalidInput,
                                      "alert_id required"}); }

    if (auto err = alerts_.AcknowledgeAlert(alert_id, ctx, now_unix))
        return ErrorResult(*err);

    CommandResult result;
    result.http_status = 200;
    result.body = common::ErrorEnvelope::SuccessJson();
    return result;
}

CommandResult CommandDispatcher::HandleUpdateImport(const nlohmann::json& body,
                                                     const UserContext& ctx,
                                                     int64_t now_unix) {
    if (auto denied = AuthorizationService::RequireAdminPanel(ctx.role))
        return ErrorResult(*denied);

    std::string msi_path;
    try {
        msi_path = body.at("msi_path").get<std::string>();
    } catch (...) {
        return ErrorResult({common::ErrorCode::InvalidInput, "msi_path required"});
    }

    if (msi_path.empty()) {
        return ErrorResult({common::ErrorCode::InvalidInput, "msi_path must not be empty"});
    }

    if (!update_manager_) {
        return ErrorResult({common::ErrorCode::Internal,
                            "Update manager not initialized"});
    }

    bool ok = update_manager_->ImportPackage(msi_path, now_unix);
    const auto& pkg = update_manager_->Package();

    if (!ok) {
        auto err = update_manager_->LastError();
        if (err.message.empty()) err.message = "Package verification failed";
        // All ImportPackage failures are SIGNATURE_INVALID (400) at the command
        // surface — file-not-found, wrong extension, and bad signature are all
        // client-side validation errors. Only genuine internal errors return 500.
        if (err.code != common::ErrorCode::Internal) {
            err.code = common::ErrorCode::SignatureInvalid;
        }
        return ErrorResult(err);
    }

    CommandResult result;
    result.http_status = 200;
    result.body = common::ErrorEnvelope::SuccessJson({
        {"verified",           pkg ? pkg->signature_valid : false},
        {"signer_thumbprint",  pkg ? pkg->signer_thumbprint : ""},
        {"from_version",       update_manager_->Rollback().previous_version},
        {"to_version",         pkg ? pkg->version : ""},
        {"rollback_available", update_manager_->Rollback().rollback_available}
    });

    audit_.RecordSystemEvent("UPDATE_IMPORT",
        "Update package import requested by " + ctx.role_string + ": " + msi_path,
        now_unix);

    return result;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

CommandResult CommandDispatcher::ErrorResult(const common::ErrorEnvelope& env) {
    CommandResult result;
    result.http_status = common::HttpStatusForCode(env.code);
    result.body = env.ToJson();
    return result;
}

CommandResult CommandDispatcher::NotFound() {
    common::ErrorEnvelope env{common::ErrorCode::NotFound, "Unknown command"};
    return ErrorResult(env);
}

} // namespace shelterops::services
