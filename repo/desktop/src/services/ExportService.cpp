#include "shelterops/services/ExportService.h"
#include "shelterops/infrastructure/AtomicFileWriter.h"
#include <spdlog/spdlog.h>
#include <sstream>

namespace shelterops::services {

ExportService::ExportService(repositories::ReportRepository& reports,
                              repositories::AdminRepository&  admin,
                              AuditService&                   audit,
                              std::string                     exports_dir)
    : reports_(reports), admin_(admin), audit_(audit),
      exports_dir_(std::move(exports_dir)) {}

ExportJobResult ExportService::RequestExport(int64_t run_id,
                                              const std::string& format,
                                              const UserContext& user_ctx,
                                              int64_t now_unix) {
    // Look up the run to get its report_type.
    auto run = reports_.FindRun(run_id);
    if (!run) {
        return common::ErrorEnvelope{common::ErrorCode::NotFound,
                                      "Report run not found"};
    }

    // Check permissions via AdminRepository (which reflects the live export_permissions table).
    auto role_str = [&]() -> std::string {
        switch (user_ctx.role) {
        case domain::UserRole::Administrator:    return "administrator";
        case domain::UserRole::OperationsManager: return "operations_manager";
        case domain::UserRole::InventoryClerk:   return "inventory_clerk";
        case domain::UserRole::Auditor:          return "auditor";
        default:                                  return "";
        }
    }();

    auto def = reports_.FindDefinition(run->report_id);
    const std::string report_type = def ? def->report_type : "";

    if (!admin_.CanExport(role_str, report_type, format)) {
        audit_.RecordSystemEvent("EXPORT_UNAUTHORIZED",
            "User " + std::to_string(user_ctx.user_id) +
            " attempted unauthorized export of " + report_type + " as " + format,
            now_unix);
        return common::ErrorEnvelope{common::ErrorCode::ExportUnauthorized,
                                      "Export not permitted for this role and report type"};
    }

    // PDF exports are capped at 1 concurrent job to prevent UI freeze.
    // CSV exports allow up to 2 concurrent jobs since they are CPU-light.
    const int max_concurrency = (format == "pdf") ? 1 : 2;

    int64_t job_id = reports_.InsertExportJob(run_id, format,
                                               user_ctx.user_id,
                                               max_concurrency, now_unix);

    audit_.RecordSystemEvent("EXPORT_REQUESTED",
        "Export job " + std::to_string(job_id) +
        " queued for run " + std::to_string(run_id) + " format=" + format,
        now_unix);

    return job_id;
}

common::ErrorEnvelope ExportService::RunExportJob(int64_t job_id, int64_t now_unix) {
    auto job = reports_.FindExportJob(job_id);
    if (!job) {
        return common::ErrorEnvelope{common::ErrorCode::NotFound,
                                      "Export job not found"};
    }

    reports_.UpdateExportJobStatus(job_id, "running", "", 0);

    auto run = reports_.FindRun(job->report_run_id);
    if (!run) {
        reports_.UpdateExportJobStatus(job_id, "failed", "", now_unix);
        return common::ErrorEnvelope{common::ErrorCode::NotFound,
                                      "Associated report run not found"};
    }

    // Generate output path from configurable exports directory.
    std::string output_path = exports_dir_ + "/" + run->version_label + "." + job->format;

    try {
        if (job->format == "csv") {
            // Write minimal CSV from snapshot data.
            auto snapshots = reports_.ListSnapshotsForRun(job->report_run_id);
            std::string csv = "metric_name,metric_value\n";
            for (const auto& s : snapshots) {
                csv += s.metric_name + "," + std::to_string(s.metric_value) + "\n";
            }
            infrastructure::AtomicFileWriter::WriteAtomic(output_path, csv);
        } else {
            // PDF: generate a minimal but structurally valid PDF/1.4 document.
            // Contains a single page with the report version label as text content.
            auto snapshots = reports_.ListSnapshotsForRun(job->report_run_id);
            std::string body_text = "Report: " + run->version_label + "\n";
            for (const auto& s : snapshots) {
                body_text += s.metric_name + ": " +
                             std::to_string(static_cast<long long>(s.metric_value)) + "\n";
            }

            // Build PDF stream content (BT/ET text block).
            std::string pdf_stream;
            pdf_stream += "BT\n/F1 12 Tf\n72 720 Td\n";
            // Split body_text into lines and render each with Td offset.
            std::istringstream ss(body_text);
            std::string line;
            bool first = true;
            while (std::getline(ss, line)) {
                if (!line.empty()) {
                    // Escape parentheses for PDF string literal.
                    std::string escaped;
                    for (char c : line) {
                        if (c == '(' || c == ')' || c == '\\') escaped += '\\';
                        escaped += c;
                    }
                    if (!first) pdf_stream += "0 -16 Td\n";
                    pdf_stream += "(" + escaped + ") Tj\n";
                    first = false;
                }
            }
            pdf_stream += "ET\n";

            int stream_len = static_cast<int>(pdf_stream.size());

            std::string pdf;
            pdf += "%PDF-1.4\n";
            // Object 1: Catalog
            size_t off1 = pdf.size();
            pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
            // Object 2: Pages
            size_t off2 = pdf.size();
            pdf += "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";
            // Object 3: Page
            size_t off3 = pdf.size();
            pdf += "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792]"
                   " /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n";
            // Object 4: Content stream
            size_t off4 = pdf.size();
            pdf += "4 0 obj\n<< /Length " + std::to_string(stream_len) + " >>\nstream\n";
            pdf += pdf_stream;
            pdf += "endstream\nendobj\n";
            // Object 5: Font
            size_t off5 = pdf.size();
            pdf += "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n";

            // Cross-reference table
            size_t xref_offset = pdf.size();
            pdf += "xref\n0 6\n";
            pdf += "0000000000 65535 f \n";
            auto fmt_off = [](size_t o) -> std::string {
                std::string s = std::to_string(o);
                while (s.size() < 10) s = "0" + s;
                return s + " 00000 n \n";
            };
            pdf += fmt_off(off1);
            pdf += fmt_off(off2);
            pdf += fmt_off(off3);
            pdf += fmt_off(off4);
            pdf += fmt_off(off5);

            pdf += "trailer\n<< /Size 6 /Root 1 0 R >>\n";
            pdf += "startxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";

            infrastructure::AtomicFileWriter::WriteAtomic(output_path, pdf);
        }
    } catch (const std::exception& ex) {
        spdlog::error("ExportService::RunExportJob: write failed: {}", ex.what());
        reports_.UpdateExportJobStatus(job_id, "failed", "", now_unix);
        audit_.RecordSystemEvent("EXPORT_FAILED",
            "Export job " + std::to_string(job_id) + " failed",
            now_unix);
        return common::ErrorEnvelope{common::ErrorCode::Internal,
                                      "An unexpected error occurred."};
    }

    reports_.UpdateExportJobStatus(job_id, "completed", output_path, now_unix);
    audit_.RecordSystemEvent("EXPORT_COMPLETED",
        "Export job " + std::to_string(job_id) +
        " completed: " + output_path,
        now_unix);

    return common::ErrorEnvelope{common::ErrorCode::Internal, ""};
}

std::vector<repositories::ExportJobRecord> ExportService::ListPending() const {
    return reports_.ListPendingExportJobs();
}

} // namespace shelterops::services
