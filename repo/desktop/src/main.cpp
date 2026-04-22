#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "shelterops/AppConfig.h"
#include "shelterops/Version.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/MigrationRunner.h"
#include "shelterops/infrastructure/CredentialVault.h"
#include "shelterops/infrastructure/RateLimiter.h"
#include "shelterops/infrastructure/CrashCheckpoint.h"
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/repositories/SessionRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/AnimalRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/SchedulerRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/AuthService.h"
#include "shelterops/services/BookingService.h"
#include "shelterops/services/InventoryService.h"
#include "shelterops/services/AlertService.h"
#include "shelterops/services/MaintenanceService.h"
#include "shelterops/services/ReportService.h"
#include "shelterops/services/ExportService.h"
#include "shelterops/services/SchedulerService.h"
#include "shelterops/services/RetentionService.h"
#include "shelterops/services/ConsentService.h"
#include "shelterops/services/AdminService.h"
#include "shelterops/services/CheckpointService.h"
#include "shelterops/services/CommandDispatcher.h"
#include "shelterops/workers/JobQueue.h"
#include "shelterops/workers/WorkerRegistry.h"
#include "shelterops/shell/SessionContext.h"
#include "shelterops/shell/ShellController.h"
#include "shelterops/shell/DockspaceShell.h"
#include "shelterops/shell/TrayManager.h"
#include "shelterops/shell/TrayBadgeState.h"
#include "shelterops/shell/ClipboardHelper.h"
#include "shelterops/shell/KeyboardShortcutHandler.h"
#include "shelterops/shell/LoginView.h"
#include "shelterops/ui/controllers/AppController.h"
#include "shelterops/ui/controllers/KennelBoardController.h"
#include "shelterops/ui/controllers/ItemLedgerController.h"
#include "shelterops/ui/controllers/ReportsController.h"
#include "shelterops/ui/controllers/GlobalSearchController.h"
#include "shelterops/ui/controllers/AdminPanelController.h"
#include "shelterops/ui/controllers/AuditLogController.h"
#include "shelterops/ui/controllers/AlertsPanelController.h"
#include "shelterops/ui/controllers/SchedulerPanelController.h"

#include <memory>
#include <vector>
#include <chrono>
#include <filesystem>

// ---------------------------------------------------------------------------
// DX11 device state
// ---------------------------------------------------------------------------
static ID3D11Device*           g_pd3dDevice           = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext    = nullptr;
static IDXGISwapChain*         g_pSwapChain           = nullptr;
static UINT                    g_ResizeWidth           = 0;
static UINT                    g_ResizeHeight          = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView  = nullptr;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// Win32 window procedure
// ---------------------------------------------------------------------------
// Forward-declared so WndProc can forward tray messages.
static shelterops::shell::TrayManager* g_TrayMgr = nullptr;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    if (g_TrayMgr && g_TrayMgr->HandleMessage(msg, wParam, lParam))
        return 0;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth  = static_cast<UINT>(LOWORD(lParam));
        g_ResizeHeight = static_cast<UINT>(HIWORD(lParam));
        return 0;
    case WM_CLOSE:
        if (g_TrayMgr && g_TrayMgr->IsInstalled()) {
            g_TrayMgr->MinimizeToTray();
            return 0;
        }
        break;
    case shelterops::shell::TrayManager::WM_TRAY_EXIT:
        DestroyWindow(hWnd);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Logging — config loaded first to honour log_dir and log_level from shelterops.json.
    shelterops::AppConfig cfg_early = shelterops::AppConfig::LoadOrDefault("shelterops.json");
    std::filesystem::create_directories(cfg_early.log_dir);
    std::filesystem::create_directories(cfg_early.exports_dir);
    std::filesystem::create_directories(cfg_early.update_metadata_dir);
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink    = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
        cfg_early.log_dir + "/shelterops.log", 0, 0);
    auto logger = std::make_shared<spdlog::logger>(
        "shelterops",
        spdlog::sinks_init_list{console_sink, file_sink});
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::from_str(cfg_early.log_level));
    spdlog::info("{} v{} starting", shelterops::APP_NAME, shelterops::VERSION_STRING);

    shelterops::AppConfig cfg = cfg_early;
    spdlog::info("Config loaded: db={} tray={} automation={}",
        cfg.db_path, cfg.start_in_tray, cfg.automation_endpoint_enabled);

    // --- Security + Infrastructure bootstrap ---
    shelterops::infrastructure::CryptoHelper::Init();

    // Credential vault (DPAPI on Windows; file fallback on Linux CI)
    auto vault = std::make_unique<shelterops::infrastructure::CredentialVault>();

    // Database and migration runner
    std::filesystem::create_directories(
        std::filesystem::path(cfg.db_path).parent_path().empty()
        ? "." : std::filesystem::path(cfg.db_path).parent_path());
    auto database = std::make_unique<shelterops::infrastructure::Database>(cfg.db_path);
    shelterops::infrastructure::MigrationRunner migrator(*database);
    auto mig_result = migrator.Run("database");
    if (!mig_result.success) {
        spdlog::critical("Migration failed [{}]: {}",
                         mig_result.error_script, mig_result.error_message);
        return 1;
    }

    // Core repositories
    auto user_repo        = std::make_unique<shelterops::repositories::UserRepository>(*database);
    auto session_repo     = std::make_unique<shelterops::repositories::SessionRepository>(*database);
    auto audit_repo       = std::make_unique<shelterops::repositories::AuditRepository>(*database);
    auto kennel_repo      = std::make_unique<shelterops::repositories::KennelRepository>(*database);
    auto booking_repo     = std::make_unique<shelterops::repositories::BookingRepository>(*database);
    auto animal_repo      = std::make_unique<shelterops::repositories::AnimalRepository>(*database);
    auto inventory_repo   = std::make_unique<shelterops::repositories::InventoryRepository>(*database);
    auto maintenance_repo = std::make_unique<shelterops::repositories::MaintenanceRepository>(*database);
    auto report_repo      = std::make_unique<shelterops::repositories::ReportRepository>(*database);
    auto scheduler_repo   = std::make_unique<shelterops::repositories::SchedulerRepository>(*database);
    auto admin_repo       = std::make_unique<shelterops::repositories::AdminRepository>(*database);

    // Core services
    auto audit_svc  = std::make_unique<shelterops::services::AuditService>(*audit_repo);
    shelterops::infrastructure::CryptoHelper crypto_helper;
    auto auth_svc   = std::make_unique<shelterops::services::AuthService>(
        *user_repo, *session_repo, *audit_svc, crypto_helper);

    auto booking_svc     = std::make_unique<shelterops::services::BookingService>(
        *kennel_repo, *booking_repo, *admin_repo, *vault, *audit_svc);
    auto inventory_svc   = std::make_unique<shelterops::services::InventoryService>(
        *inventory_repo, *audit_svc);
    auto alert_svc       = std::make_unique<shelterops::services::AlertService>(
        *inventory_repo, *audit_svc);
    auto maintenance_svc = std::make_unique<shelterops::services::MaintenanceService>(
        *maintenance_repo, *audit_svc);
    auto report_svc      = std::make_unique<shelterops::services::ReportService>(
        *report_repo, *kennel_repo, *booking_repo, *inventory_repo, *maintenance_repo, *audit_svc,
        cfg.exports_dir);
    auto export_svc      = std::make_unique<shelterops::services::ExportService>(
        *report_repo, *admin_repo, *audit_svc, cfg.exports_dir);

    // Worker pool is created before SchedulerService so on-demand enqueues can submit immediately.
    auto job_queue      = std::make_unique<shelterops::workers::JobQueue>(2);

    auto scheduler_svc   = std::make_unique<shelterops::services::SchedulerService>(
        *scheduler_repo, *audit_svc, job_queue.get());
    auto retention_svc   = std::make_unique<shelterops::services::RetentionService>(
        *user_repo, *booking_repo, *animal_repo, *inventory_repo, *maintenance_repo, *admin_repo, *audit_svc);
    auto consent_svc     = std::make_unique<shelterops::services::ConsentService>(
        *admin_repo, *audit_svc);
    auto admin_svc       = std::make_unique<shelterops::services::AdminService>(
        *admin_repo, *audit_svc);

    auto crash_checkpoint = std::make_unique<shelterops::infrastructure::CrashCheckpoint>(*database);
    auto checkpoint_svc   = std::make_unique<shelterops::services::CheckpointService>(*crash_checkpoint);

    auto rate_limiter  = std::make_unique<shelterops::infrastructure::RateLimiter>(
        cfg.automation_rate_limit_rpm);
    auto update_manager = std::make_unique<shelterops::infrastructure::UpdateManager>(
        cfg.update_metadata_dir, cfg.trusted_publishers_path);
    auto dispatcher    = std::make_unique<shelterops::services::CommandDispatcher>(
        *booking_svc, *inventory_svc, *report_svc, *export_svc, *alert_svc,
        *session_repo, *user_repo, *rate_limiter, *audit_svc, update_manager.get());

    // Worker handlers are registered up-front.
    auto worker_registry = std::make_unique<shelterops::workers::WorkerRegistry>(
        *job_queue, *report_svc, *export_svc, *retention_svc, *alert_svc);
    worker_registry->RegisterAll();
    // job_queue->Start() is intentionally deferred.

    spdlog::info("Business engine constructed ({} services, {} worker threads configured)",
        11, 2);

    // Shell context + controller
    auto session_ctx = std::make_unique<shelterops::shell::SessionContext>();
    auto shell_ctrl  = std::make_unique<shelterops::shell::ShellController>(
        *auth_svc, *user_repo, *session_ctx);
    shell_ctrl->OnBootstrapComplete();

    // UI controllers — cross-platform state holders
    auto kennel_board_ctrl = std::make_unique<shelterops::ui::controllers::KennelBoardController>(
        *booking_svc, *kennel_repo);
    auto item_ledger_ctrl  = std::make_unique<shelterops::ui::controllers::ItemLedgerController>(
        *inventory_svc, *inventory_repo);
    auto reports_ctrl      = std::make_unique<shelterops::ui::controllers::ReportsController>(
        *report_svc, *export_svc, *report_repo);
    auto global_search_ctrl = std::make_unique<shelterops::ui::controllers::GlobalSearchController>(
        *kennel_repo, *booking_repo, *inventory_repo, *report_repo, *audit_repo);

    // Tray badge state (cross-platform data, Win32 tray rendering below)
    shelterops::shell::TrayBadgeState tray_badge_state;

    auto admin_panel_ctrl = std::make_unique<shelterops::ui::controllers::AdminPanelController>(
        *admin_svc, *booking_svc, *consent_svc, *admin_repo, *booking_repo);
    auto audit_log_ctrl = std::make_unique<shelterops::ui::controllers::AuditLogController>(
        *audit_repo);
    auto alerts_panel_ctrl = std::make_unique<shelterops::ui::controllers::AlertsPanelController>(
        *alert_svc, tray_badge_state);
    auto scheduler_panel_ctrl = std::make_unique<shelterops::ui::controllers::SchedulerPanelController>(
        *scheduler_svc, *scheduler_repo);

    auto app_ctrl = std::make_unique<shelterops::ui::controllers::AppController>(
        *kennel_board_ctrl, *item_ledger_ctrl, *reports_ctrl, *global_search_ctrl,
        *admin_panel_ctrl, *audit_log_ctrl, *alerts_panel_ctrl, *scheduler_panel_ctrl,
        *checkpoint_svc, *session_ctx);

    // Restore previous session window layout if available.
    {
        int64_t now_init = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        if (app_ctrl->RestoreCheckpoint(now_init))
            spdlog::info("AppController: checkpoint restored");
    }

    spdlog::info("UI controllers constructed");

    // Win32 window class
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"ShelterOpsDeskConsole";
    RegisterClassExW(&wc);

    const int w = cfg.default_window_width;
    const int h = cfg.default_window_height;
    HWND hwnd = CreateWindowExW(
        0L, wc.lpszClassName, L"ShelterOps Desk Console",
        WS_OVERLAPPEDWINDOW, 100, 100, w, h,
        nullptr, nullptr, hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        spdlog::critical("D3D11 device creation failed");
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    ShowWindow(hwnd, cfg.start_in_tray ? SW_HIDE : SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Tray manager — installed after window creation
    auto tray_mgr = std::make_unique<shelterops::shell::TrayManager>(hwnd);
    tray_mgr->Install();
    if (cfg.start_in_tray) tray_mgr->MinimizeToTray();
    tray_mgr->SetOpenAlertsCallback([&app_ctrl]() {
        app_ctrl->OpenWindow(shelterops::shell::WindowId::AlertsPanel);
        app_ctrl->SetActiveWindow(shelterops::shell::WindowId::AlertsPanel);
    });
    tray_mgr->SetOpenLowStockCallback([&app_ctrl]() {
        app_ctrl->OpenWindow(shelterops::shell::WindowId::AlertsPanel);
        app_ctrl->SetActiveWindow(shelterops::shell::WindowId::AlertsPanel);
    });
    tray_mgr->SetOpenExpirationCallback([&app_ctrl]() {
        app_ctrl->OpenWindow(shelterops::shell::WindowId::AlertsPanel);
        app_ctrl->SetActiveWindow(shelterops::shell::WindowId::AlertsPanel);
    });

    // ImGui context with docking + multi-viewport
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename  = "shelterops_layout.ini";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    spdlog::info("ImGui initialized (docking={} viewports={})",
        !!(io.ConfigFlags & ImGuiConfigFlags_DockingEnable),
        !!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable));

    const ImVec4 clear_color = {0.08f, 0.08f, 0.10f, 1.0f};

    shelterops::shell::LoginView login_view(*shell_ctrl);
    bool show_login = true;

    // DockspaceShell owns all authenticated window rendering.
    auto dockspace_shell = std::make_unique<shelterops::shell::DockspaceShell>(
        *app_ctrl, *session_ctx, *shell_ctrl, tray_badge_state);

    // Wire tray manager for WndProc forwarding.
    g_TrayMgr = tray_mgr.get();

    // Start worker pool now that UI progress display is ready.
    job_queue->Start();
    spdlog::info("JobQueue started");

    const int64_t app_start_unix = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // Alert scan interval: every 60 seconds.
    int64_t last_alert_scan = 0;
    int64_t last_scheduler_dispatch = 0;
    int64_t last_lan_sync_enqueue = 0;
    // LAN sync cadence: every 5 minutes (300 s). Only active when lan_sync_enabled
    // and peer_host is configured; gated on an authenticated session so the
    // system UserContext used for the job run is consistent with the audit log.
    constexpr int64_t kLanSyncIntervalSecs = 300;

    // Main loop
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(
                0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        int64_t now_unix = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        // Periodic alert scan to keep tray badge current.
        if (session_ctx->IsAuthenticated() && now_unix - last_alert_scan >= 60) {
            last_alert_scan = now_unix;
            domain::AlertThreshold thresholds{
                cfg.low_stock_days_threshold, cfg.expiration_alert_days};
            alert_svc->Scan(now_unix, thresholds);
            tray_badge_state.Update(inventory_repo->ListActiveAlerts());
            tray_mgr->UpdateBadge(tray_badge_state.TotalBadgeCount());
        }

        // Periodic scheduler dispatch for due background jobs.
        if (now_unix - last_scheduler_dispatch >= 15) {
            last_scheduler_dispatch = now_unix;
            scheduler_svc->DispatchDueJobs(now_unix);
        }

        // Periodic LAN sync: submit a job using AppConfig TLS peer settings.
        // Snapshot of db_path is created by SubmitLanSync before enqueue.
        if (cfg.lan_sync_enabled && !cfg.lan_sync_peer_host.empty() &&
            session_ctx->IsAuthenticated() &&
            now_unix - last_lan_sync_enqueue >= kLanSyncIntervalSecs) {
            last_lan_sync_enqueue = now_unix;
            shelterops::services::UserContext sys_ctx;
            sys_ctx.user_id = 0;
            sys_ctx.role    = shelterops::domain::UserRole::Administrator;
            scheduler_svc->SubmitLanSync(
                cfg.db_path,
                cfg.lan_sync_peer_host,
                cfg.lan_sync_peer_port,
                cfg.lan_sync_pinned_certs_path,
                sys_ctx,
                now_unix);
        }

        if (show_login &&
            shell_ctrl->CurrentState() != shelterops::shell::ShellState::ShellReady) {
            show_login = login_view.Render(now_unix);
            if (!show_login) {
                // Just authenticated — open default windows.
                app_ctrl->OpenWindow(shelterops::shell::WindowId::KennelBoard);
                app_ctrl->OpenWindow(shelterops::shell::WindowId::ItemLedger);
                kennel_board_ctrl->Refresh(session_ctx->Get(), now_unix);
                item_ledger_ctrl->Refresh(now_unix);
            }
        } else {
            bool keep_running = dockspace_shell->Render(now_unix, app_start_unix);
            if (!keep_running) {
                // Capture checkpoint before exit.
                app_ctrl->CaptureCheckpoint(now_unix);
                done = true;
            }
            // Re-show login if shell signed out.
            if (shell_ctrl->CurrentState() == shelterops::shell::ShellState::LoginRequired) {
                show_login = true;
            }
        }

        ImGui::Render();
        g_pd3dDeviceContext->ClearRenderTargetView(
            g_mainRenderTargetView, &clear_color.x);
        g_pd3dDeviceContext->OMSetRenderTargets(
            1, &g_mainRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        g_pSwapChain->Present(1, 0);
    }

    spdlog::info("Shutting down");
    g_TrayMgr = nullptr;
    job_queue->Stop();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);
    return 0;
}

// ---------------------------------------------------------------------------
// D3D11 helpers
// ---------------------------------------------------------------------------
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd                     = {};
    sd.BufferCount                              = 2;
    sd.BufferDesc.Width                         = 0;
    sd.BufferDesc.Height                        = 0;
    sd.BufferDesc.Format                        = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator         = 60;
    sd.BufferDesc.RefreshRate.Denominator       = 1;
    sd.Flags                                    = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                              = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                             = hWnd;
    sd.SampleDesc.Count                         = 1;
    sd.Windowed                                 = TRUE;
    sd.SwapEffect                               = DXGI_SWAP_EFFECT_DISCARD;

    UINT createFlags = 0;
#ifdef SHELTEROPS_DEBUG_LEAKS
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL       featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[] = {D3D_FEATURE_LEVEL_11_0};

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createFlags, featureLevelArray, 1,
        D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            createFlags, featureLevelArray, 1,
            D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain        = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice        = nullptr; }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}
