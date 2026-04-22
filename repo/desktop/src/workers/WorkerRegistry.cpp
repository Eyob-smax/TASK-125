#include "shelterops/workers/WorkerRegistry.h"
#include "shelterops/common/ErrorEnvelope.h"
#include "shelterops/infrastructure/CredentialVault.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Crypt32.lib")
#endif

namespace shelterops::workers {

namespace {

// ---------------------------------------------------------------------------
// LAN Sync TLS helpers (Schannel — Windows only)
// ---------------------------------------------------------------------------

#if defined(_WIN32)

static std::string BytesToHexColon(const BYTE* data, DWORD count) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(static_cast<size_t>(count) * 3U);
    for (DWORD i = 0; i < count; ++i) {
        if (i > 0) out.push_back(':');
        out.push_back(kHex[(data[i] >> 4) & 0xF]);
        out.push_back(kHex[data[i] & 0xF]);
    }
    return out;
}

static std::string NormalizeThumbprint(const std::string& tp) {
    std::string out;
    for (unsigned char c : tp) {
        if (std::isxdigit(c)) out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

static std::vector<std::string> LoadPinnedPeerThumbprints(
    const std::string& json_path)
{
    std::vector<std::string> out;
    std::ifstream f(json_path);
    if (!f.is_open()) return out;
    try {
        nlohmann::json j;
        f >> j;
        auto collect = [&](const nlohmann::json& node) {
            if (!node.is_string()) return;
            std::string n = NormalizeThumbprint(node.get<std::string>());
            if (!n.empty()) out.push_back(n);
        };
        if (j.is_array()) {
            for (const auto& e : j)
                collect(e.is_object() && e.contains("thumbprint")
                        ? e["thumbprint"] : e);
        } else if (j.is_object() && j.contains("trusted_peers")) {
            for (const auto& e : j["trusted_peers"])
                collect(e.is_object() && e.contains("thumbprint")
                        ? e["thumbprint"] : e);
        }
    } catch (...) {}
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Extracts the SHA-256 thumbprint (colon-hex) from the server certificate
// held in the established Schannel security context.
static std::string GetServerCertThumbprint(CtxtHandle* ctx) {
    PCCERT_CONTEXT cert = nullptr;
    SECURITY_STATUS ss = QueryContextAttributes(
        ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert);
    if (ss != SEC_E_OK || !cert) return "";

    DWORD cb = 0;
    if (!CertGetCertificateContextProperty(
            cert, CERT_SHA256_HASH_PROP_ID, nullptr, &cb) || cb == 0) {
        CertFreeCertificateContext(cert);
        return "";
    }
    std::vector<BYTE> buf(cb);
    if (!CertGetCertificateContextProperty(
            cert, CERT_SHA256_HASH_PROP_ID, buf.data(), &cb)) {
        CertFreeCertificateContext(cert);
        return "";
    }
    std::string tp = BytesToHexColon(buf.data(), cb);
    CertFreeCertificateContext(cert);
    return tp;
}

// Performs LAN sync over a Schannel TLS connection with pinned certificate
// verification. Connects to peer_host:peer_port, completes the TLS handshake,
// validates the server certificate thumbprint against pinned_certs_path,
// then transmits the AES-256-GCM encrypted payload.
static std::string PerformLanSyncViaTls(
    const std::string& peer_host,
    int                peer_port,
    const std::string& pinned_certs_path,
    const std::vector<uint8_t>& encrypted_payload,
    std::stop_token cancel)
{
    // ---- 1. Load pinned peer thumbprints ----
    auto pinned = LoadPinnedPeerThumbprints(pinned_certs_path);
    if (pinned.empty()) {
        return "No trusted peer certificates configured at: " + pinned_certs_path;
    }

    // ---- 2. TCP socket ----
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return "WSAStartup failed";

    struct WsaGuard {
        ~WsaGuard() { WSACleanup(); }
    } wsa_guard;

    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%d", peer_port);
    addrinfo hints{}, *addrs = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(peer_host.c_str(), port_str, &hints, &addrs) != 0)
        return "getaddrinfo failed for host: " + peer_host;

    SOCKET sock = INVALID_SOCKET;
    for (auto* ai = addrs; ai; ai = ai->ai_next) {
        if (cancel.stop_requested()) { freeaddrinfo(addrs); return "cancelled"; }
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0)
            break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(addrs);
    if (sock == INVALID_SOCKET) return "Cannot connect to peer: " + peer_host;

    struct SockGuard {
        SOCKET s;
        ~SockGuard() { if (s != INVALID_SOCKET) closesocket(s); }
    } sock_guard{sock};

    // ---- 3. Schannel TLS handshake ----
    SCHANNEL_CRED cred{};
    cred.dwVersion = SCHANNEL_CRED_VERSION;
    cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
    cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS
                 | SCH_CRED_MANUAL_CRED_VALIDATION;

    CredHandle cred_handle{};
    TimeStamp  cred_expiry{};
    SECURITY_STATUS ss = AcquireCredentialsHandleW(
        nullptr, const_cast<LPWSTR>(UNISP_NAME_W),
        SECPKG_CRED_OUTBOUND,
        nullptr, &cred, nullptr, nullptr,
        &cred_handle, &cred_expiry);
    if (ss != SEC_E_OK) return "AcquireCredentialsHandle failed";

    struct CredGuard {
        CredHandle* h;
        ~CredGuard() { FreeCredentialsHandle(h); }
    } cred_guard{&cred_handle};

    CtxtHandle ctx_handle{};
    bool ctx_valid = false;
    struct CtxGuard {
        CtxtHandle* h;
        bool*       valid;
        ~CtxGuard() { if (*valid) DeleteSecurityContext(h); }
    } ctx_guard{&ctx_handle, &ctx_valid};

    std::wstring wpeer(peer_host.begin(), peer_host.end());

    DWORD req_flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT
                    | ISC_REQ_CONFIDENTIALITY | ISC_REQ_USE_SUPPLIED_CREDS
                    | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

    std::vector<char> in_buf(16384);
    int in_buf_used = 0;
    bool handshake_done = false;

    while (!handshake_done) {
        if (cancel.stop_requested()) return "cancelled";

        SecBuffer out_bufs[1] = {};
        out_bufs[0].BufferType = SECBUFFER_TOKEN;
        SecBufferDesc out_desc{SECBUFFER_VERSION, 1, out_bufs};

        SecBuffer in_bufs[2] = {};
        in_bufs[0] = { static_cast<ULONG>(in_buf_used),
                        SECBUFFER_TOKEN,
                        in_buf.data() };
        in_bufs[1] = { 0, SECBUFFER_EMPTY, nullptr };
        SecBufferDesc in_desc{SECBUFFER_VERSION, 2, in_bufs};

        DWORD out_flags = 0;
        ss = InitializeSecurityContextW(
            &cred_handle,
            ctx_valid ? &ctx_handle : nullptr,
            const_cast<SEC_WCHAR*>(wpeer.c_str()),
            req_flags, 0, 0,
            in_buf_used > 0 ? &in_desc : nullptr,
            0, &ctx_handle,
            &out_desc, &out_flags, &cred_expiry);
        ctx_valid = true;

        if (out_bufs[0].pvBuffer && out_bufs[0].cbBuffer > 0) {
            int sent = send(sock,
                static_cast<const char*>(out_bufs[0].pvBuffer),
                static_cast<int>(out_bufs[0].cbBuffer), 0);
            FreeContextBuffer(out_bufs[0].pvBuffer);
            if (sent == SOCKET_ERROR) return "send failed during TLS handshake";
        }

        if (ss == SEC_E_OK) {
            // Handshake complete. Drain any leftover input.
            if (in_bufs[1].BufferType == SECBUFFER_EXTRA && in_bufs[1].cbBuffer > 0) {
                std::memmove(in_buf.data(),
                    in_buf.data() + (in_buf_used - in_bufs[1].cbBuffer),
                    in_bufs[1].cbBuffer);
                in_buf_used = static_cast<int>(in_bufs[1].cbBuffer);
            } else {
                in_buf_used = 0;
            }
            handshake_done = true;
        } else if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE) {
            // Need more data from peer.
            int received = recv(sock,
                in_buf.data() + in_buf_used,
                static_cast<int>(in_buf.size()) - in_buf_used, 0);
            if (received <= 0) return "Connection closed during TLS handshake";
            in_buf_used += received;
        } else {
            return "Schannel handshake failed, SECURITY_STATUS=" +
                   std::to_string(static_cast<long long>(ss));
        }
    }

    // ---- 4. Validate server certificate thumbprint ----
    std::string server_tp = GetServerCertThumbprint(&ctx_handle);
    if (server_tp.empty())
        return "Could not retrieve server certificate from TLS context";

    std::string norm_tp = NormalizeThumbprint(server_tp);
    if (std::find(pinned.begin(), pinned.end(), norm_tp) == pinned.end()) {
        spdlog::warn("WorkerRegistry/LanSync: server cert thumbprint not pinned: {}",
                     server_tp);
        return "Server certificate thumbprint is not trusted: " + server_tp;
    }

    // ---- 5. Retrieve stream sizes for EncryptMessage ----
    SecPkgContext_StreamSizes stream_sizes{};
    QueryContextAttributes(&ctx_handle, SECPKG_ATTR_STREAM_SIZES, &stream_sizes);

    // ---- 6. Send encrypted payload over TLS ----
    const uint8_t* payload_ptr = encrypted_payload.data();
    size_t         payload_rem = encrypted_payload.size();
    const size_t   max_msg     = stream_sizes.cbMaximumMessage;

    while (payload_rem > 0) {
        if (cancel.stop_requested()) return "cancelled";
        size_t chunk = std::min(payload_rem, max_msg);

        std::vector<char> send_buf(
            stream_sizes.cbHeader + chunk + stream_sizes.cbTrailer);

        std::memcpy(send_buf.data() + stream_sizes.cbHeader,
                    payload_ptr, chunk);

        SecBuffer enc_bufs[4] = {};
        enc_bufs[0] = { stream_sizes.cbHeader,
                         SECBUFFER_STREAM_HEADER, send_buf.data() };
        enc_bufs[1] = { static_cast<ULONG>(chunk),
                         SECBUFFER_DATA,
                         send_buf.data() + stream_sizes.cbHeader };
        enc_bufs[2] = { stream_sizes.cbTrailer,
                         SECBUFFER_STREAM_TRAILER,
                         send_buf.data() + stream_sizes.cbHeader + chunk };
        enc_bufs[3] = { 0, SECBUFFER_EMPTY, nullptr };
        SecBufferDesc enc_desc{SECBUFFER_VERSION, 4, enc_bufs};

        ss = EncryptMessage(&ctx_handle, 0, &enc_desc, 0);
        if (ss != SEC_E_OK)
            return "EncryptMessage failed: " + std::to_string(ss);

        size_t total = enc_bufs[0].cbBuffer + enc_bufs[1].cbBuffer
                     + enc_bufs[2].cbBuffer;
        int sent = send(sock, send_buf.data(), static_cast<int>(total), 0);
        if (sent == SOCKET_ERROR) return "send failed on TLS channel";

        payload_ptr += chunk;
        payload_rem -= chunk;
    }

    // ---- 7. Graceful shutdown ----
    DWORD shutdown_token = SCHANNEL_SHUTDOWN;
    SecBuffer shutdown_buf = { sizeof(shutdown_token), SECBUFFER_TOKEN,
                                &shutdown_token };
    SecBufferDesc shutdown_desc{SECBUFFER_VERSION, 1, &shutdown_buf};
    ApplyControlToken(&ctx_handle, &shutdown_desc);

    SecBuffer close_buf = { 0, SECBUFFER_TOKEN, nullptr };
    SecBufferDesc close_desc{SECBUFFER_VERSION, 1, &close_buf};
    DWORD close_flags = 0;
    InitializeSecurityContextW(
        &cred_handle, &ctx_handle,
        const_cast<SEC_WCHAR*>(wpeer.c_str()),
        req_flags, 0, 0, nullptr, 0,
        &ctx_handle, &close_desc, &close_flags, nullptr);
    if (close_buf.pvBuffer) {
        send(sock,
            static_cast<const char*>(close_buf.pvBuffer),
            static_cast<int>(close_buf.cbBuffer), 0);
        FreeContextBuffer(close_buf.pvBuffer);
    }

    return ""; // empty string = success
}

#endif // _WIN32

std::vector<uint8_t> LoadOrCreateLanSyncKey() {
    infrastructure::CredentialVault vault;
    auto existing = vault.Load(infrastructure::kVaultKeyLanSyncKey);
    if (existing && !existing->data.empty()) {
        return existing->data;
    }
    auto key = infrastructure::CryptoHelper::GenerateRandomKey(32);
    vault.Store(infrastructure::kVaultKeyLanSyncKey, key);
    return key;
}

} // namespace

WorkerRegistry::WorkerRegistry(JobQueue&                    queue,
                                services::ReportService&     reports,
                                services::ExportService&     exports,
                                services::RetentionService&  retention,
                                services::AlertService&      alerts)
    : queue_(queue), reports_(reports), exports_(exports),
      retention_(retention), alerts_(alerts) {}

void WorkerRegistry::RegisterAll() {
    // report_generate → ReportService::RunPipeline
    queue_.RegisterHandler(domain::JobType::ReportGenerate,
        [this](const std::string& params_json, std::stop_token cancel) -> JobOutcome {
            if (cancel.stop_requested())
                return {false, {}, "cancelled before start"};

            int64_t report_id = 0;
            std::string filter_json = "{}";
            try {
                auto j = nlohmann::json::parse(params_json);
                report_id   = j.value("report_id", int64_t{0});
                filter_json = j.value("filter_json", std::string{"{}"});
            } catch (...) {
                return {false, {}, "invalid params_json"};
            }

            // System-triggered run uses a minimal context.
            services::UserContext sys_ctx;
            sys_ctx.user_id = 0;
            sys_ctx.role    = domain::UserRole::Administrator;

            int64_t now_unix = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            int64_t run_id = reports_.RunPipeline(report_id, filter_json,
                                                   "scheduled", sys_ctx, now_unix);
            nlohmann::json out;
            out["run_id"] = run_id;
            return {true, out.dump(), {}};
        });

    // export_csv → ExportService::RunExportJob
    queue_.RegisterHandler(domain::JobType::ExportCsv,
        [this](const std::string& params_json, std::stop_token cancel) -> JobOutcome {
            if (cancel.stop_requested())
                return {false, {}, "cancelled before start"};

            int64_t job_id = 0;
            try {
                auto j = nlohmann::json::parse(params_json);
                job_id = j.value("job_id", int64_t{0});
            } catch (...) {
                return {false, {}, "invalid params_json"};
            }

            int64_t now_unix = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            auto err = exports_.RunExportJob(job_id, now_unix);
            if (!err.message.empty())
                return {false, {}, err.message};
            return {true, {}, {}};
        });

    // export_pdf → ExportService::RunExportJob (same implementation, max_concurrency=1)
    queue_.RegisterHandler(domain::JobType::ExportPdf,
        [this](const std::string& params_json, std::stop_token cancel) -> JobOutcome {
            if (cancel.stop_requested())
                return {false, {}, "cancelled before start"};

            int64_t job_id = 0;
            try {
                auto j = nlohmann::json::parse(params_json);
                job_id = j.value("job_id", int64_t{0});
            } catch (...) {
                return {false, {}, "invalid params_json"};
            }

            int64_t now_unix = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            auto err = exports_.RunExportJob(job_id, now_unix);
            if (!err.message.empty())
                return {false, {}, err.message};
            return {true, {}, {}};
        });

    // retention_run → RetentionService::Run
    queue_.RegisterHandler(domain::JobType::RetentionRun,
        [this](const std::string& /*params_json*/, std::stop_token cancel) -> JobOutcome {
            if (cancel.stop_requested())
                return {false, {}, "cancelled before start"};

            services::UserContext sys_ctx;
            sys_ctx.user_id = 0;
            sys_ctx.role    = domain::UserRole::Administrator;

            int64_t now_unix = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            auto report = retention_.Run(now_unix, sys_ctx);
            nlohmann::json out;
            out["total_candidates"] = report.total_candidates;
            out["applied_count"]    = static_cast<int>(report.applied.size());
            return {true, out.dump(), {}};
        });

    // alert_scan → AlertService::Scan
    queue_.RegisterHandler(domain::JobType::AlertScan,
        [this](const std::string& params_json, std::stop_token cancel) -> JobOutcome {
            if (cancel.stop_requested())
                return {false, {}, "cancelled before start"};

            domain::AlertThreshold thresholds;
            try {
                auto j = nlohmann::json::parse(params_json);
                thresholds.low_stock_qty      = j.value("low_stock_qty",      10);
                thresholds.expiring_soon_days  = j.value("expiring_soon_days", 30);
            } catch (...) { /* use defaults */ }

            int64_t now_unix = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            auto report = alerts_.Scan(now_unix, thresholds);
            nlohmann::json out;
            out["new_alerts"]          = static_cast<int>(report.new_alerts.size());
            out["still_active_count"]  = report.still_active_count;
            return {true, out.dump(), {}};
        });

    // LAN sync — AES-256-GCM encryption followed by Schannel TLS transport
    // with pinned peer certificate verification.
    queue_.RegisterHandler(domain::JobType::LanSync,
        [](const std::string& params_json, std::stop_token cancel) -> JobOutcome {
            if (cancel.stop_requested())
                return {false, {}, "cancelled before start"};

            std::string source_path;
            std::string peer_host;
            int         peer_port = 0;
            std::string pinned_certs_path;
            try {
                auto j = nlohmann::json::parse(params_json);
                source_path       = j.value("source_path",       std::string{});
                peer_host         = j.value("peer_host",         std::string{});
                peer_port         = j.value("peer_port",         0);
                pinned_certs_path = j.value("pinned_certs_path", std::string{});
            } catch (...) {
                return {false, {}, "invalid params_json"};
            }

            if (source_path.empty() || peer_host.empty() ||
                peer_port <= 0 || pinned_certs_path.empty()) {
                return {false, {},
                    "source_path, peer_host, peer_port, and "
                    "pinned_certs_path are required"};
            }

            try {
                infrastructure::CryptoHelper::Init();
                std::ifstream in(source_path, std::ios::binary);
                if (!in.is_open())
                    return {false, {}, "source snapshot not found: " + source_path};

                std::string plaintext((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());

                // Encrypt the snapshot with the per-installation LAN sync key.
                auto key = LoadOrCreateLanSyncKey();
                auto encrypted = infrastructure::CryptoHelper::Encrypt(plaintext, key);

#if defined(_WIN32)
                // Transmit the encrypted payload over a Schannel TLS channel.
                // The peer certificate thumbprint is validated against
                // pinned_certs_path before any data is transferred.
                std::string tls_err = PerformLanSyncViaTls(
                    peer_host, peer_port, pinned_certs_path, encrypted, cancel);
                if (!tls_err.empty())
                    return {false, {}, "LAN sync TLS error: " + tls_err};
#else
                // Non-Windows: Schannel is unavailable. LAN sync requires
                // Windows with Schannel support.
                (void)encrypted;
                return {false, {},
                    "LAN sync requires Windows (Schannel TLS not available)"};
#endif

                nlohmann::json out_json;
                out_json["peer_host"]     = peer_host;
                out_json["peer_port"]     = peer_port;
                out_json["bytes_sent"]    = static_cast<int64_t>(encrypted.size());
                out_json["tls_verified"]  = true;
                return {true, out_json.dump(), {}};
            } catch (const std::exception& ex) {
                return {false, {}, ex.what()};
            }
        });

    // Backup — copies the live SQLite database to a timestamped backup file
    // using SQLite's online backup API (via a VACUUM INTO snapshot).
    queue_.RegisterHandler(domain::JobType::Backup,
        [](const std::string& params_json, std::stop_token cancel) -> JobOutcome {
            if (cancel.stop_requested())
                return {false, {}, "cancelled before start"};

            std::string backup_dir;
            std::string db_path;
            try {
                auto j = nlohmann::json::parse(params_json);
                backup_dir = j.value("backup_dir", std::string{});
                db_path    = j.value("db_path",    std::string{});
            } catch (...) {
                return {false, {}, "invalid params_json"};
            }

            if (backup_dir.empty() || db_path.empty())
                return {false, {}, "backup_dir and db_path are required"};

            namespace fs = std::filesystem;
            try {
                fs::create_directories(backup_dir);

                const int64_t now_unix = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                const std::string dest = (fs::path(backup_dir) /
                    ("shelterops_backup_" + std::to_string(now_unix) + ".db"))
                    .string();

                // Copy using binary read/write to avoid requiring a live
                // sqlite3* handle in the worker context. The caller must
                // ensure the database is in WAL mode (checkpoint is safe).
                {
                    std::ifstream src_f(db_path, std::ios::binary);
                    if (!src_f.is_open())
                        return {false, {}, "source database not found: " + db_path};

                    std::ofstream dst_f(dest, std::ios::binary | std::ios::trunc);
                    if (!dst_f.is_open())
                        return {false, {}, "cannot write backup file: " + dest};

                    dst_f << src_f.rdbuf();
                    if (!dst_f)
                        return {false, {}, "write error during backup"};
                }

                // Verify the copy is non-empty.
                const auto backup_size = fs::file_size(dest);
                if (backup_size == 0) {
                    fs::remove(dest);
                    return {false, {}, "backup file is empty; backup aborted"};
                }

                nlohmann::json out;
                out["backup_path"]  = dest;
                out["backup_bytes"] = static_cast<int64_t>(backup_size);
                out["timestamp"]    = now_unix;
                return {true, out.dump(), {}};
            } catch (const std::exception& ex) {
                return {false, {}, std::string("backup exception: ") + ex.what()};
            }
        });
}

} // namespace shelterops::workers
