cmake_minimum_required(VERSION 3.28)

# vcpkg toolchain must be supplied via CMAKE_TOOLCHAIN_FILE:
#   cmake -B build -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
#     -DCMAKE_BUILD_TYPE=Release

project(ShelterOpsDesk
    VERSION 1.0.0
    DESCRIPTION "ShelterOps Desk Console - offline shelter operations"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

cmake_policy(SET CMP0135 NEW)

# ---------------------------------------------------------------------------
# Cross-platform dependencies (available on all platforms)
# ---------------------------------------------------------------------------
find_package(SQLite3          REQUIRED)
find_package(spdlog    CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(GTest     CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Windows-only dependencies (GUI layer)
# ---------------------------------------------------------------------------
if (WIN32)
    find_package(imgui  CONFIG REQUIRED)
    find_package(implot CONFIG REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Shared application library
# Compiled on all platforms; contains config, domain, services, repositories.
# No Win32/DX11/ImGui headers here — those live only in the GUI executable.
# ---------------------------------------------------------------------------
add_library(shelterops_lib STATIC
    src/AppConfig.cpp

    # Domain layer — pure logic, no I/O dependencies
    src/domain/BookingRules.cpp
    src/domain/InventoryRules.cpp
    src/domain/ReportPipeline.cpp
    src/domain/AlertRules.cpp
    src/domain/RetentionPolicy.cpp
    src/domain/RolePermissions.cpp

    # Infrastructure layer — cross-platform
    src/infrastructure/Database.cpp
    src/infrastructure/MigrationRunner.cpp
    src/infrastructure/CryptoHelper.cpp
    src/infrastructure/CredentialVault.cpp
    src/infrastructure/DeviceFingerprint.cpp
    src/infrastructure/RateLimiter.cpp
    src/infrastructure/AtomicFileWriter.cpp
    src/infrastructure/CrashCheckpoint.cpp
    # CancellationToken is header-only (std::stop_source/stop_token)

    # Common utilities — cross-platform
    src/common/ErrorEnvelope.cpp
    src/common/Validation.cpp
    src/common/SecretSafeLogger.cpp
    src/common/Uuid.cpp

    # Repository layer — cross-platform
    src/repositories/UserRepository.cpp
    src/repositories/SessionRepository.cpp
    src/repositories/AuditRepository.cpp
    src/repositories/ConfigRepository.cpp
    src/repositories/KennelRepository.cpp
    src/repositories/BookingRepository.cpp
    src/repositories/AnimalRepository.cpp
    src/repositories/InventoryRepository.cpp
    src/repositories/MaintenanceRepository.cpp
    src/repositories/ReportRepository.cpp
    src/repositories/SchedulerRepository.cpp
    src/repositories/AdminRepository.cpp

    # Domain layer extensions
    src/domain/BookingSearchFilter.cpp
    src/domain/BookingStateMachine.cpp
    src/domain/PriceRuleEngine.cpp
    src/domain/SchedulerGraph.cpp
    src/domain/ReportStageGraph.cpp

    # Service layer — cross-platform
    src/services/LockoutPolicy.cpp
    src/services/AuditService.cpp
    src/services/AuthService.cpp
    src/services/AuthorizationService.cpp
    src/services/FieldMasker.cpp
    src/services/AutomationAuthMiddleware.cpp
    src/services/BookingService.cpp
    src/services/InventoryService.cpp
    src/services/AlertService.cpp
    src/services/MaintenanceService.cpp
    src/services/ReportService.cpp
    src/services/ExportService.cpp
    src/services/SchedulerService.cpp
    src/services/RetentionService.cpp
    src/services/ConsentService.cpp
    src/services/AdminService.cpp
    src/services/CheckpointService.cpp
    src/services/CommandDispatcher.cpp

    # Worker layer — cross-platform
    src/workers/JobQueue.cpp
    src/workers/WorkerRegistry.cpp

    # Infrastructure additions — cross-platform
    src/infrastructure/BarcodeHandler.cpp

    # Shell — cross-platform core
    src/shell/SessionContext.cpp
    src/shell/ShellController.cpp
    src/shell/ErrorDisplay.cpp
    src/shell/TrayBadgeState.cpp
    src/shell/KeyboardShortcutHandler.cpp
    src/shell/ClipboardHelper.cpp

    # UI primitives — cross-platform (no ImGui)
    src/ui/primitives/TableSortState.cpp
    src/ui/primitives/ValidationState.cpp

    # UI controllers — cross-platform (no ImGui)
    src/ui/controllers/GlobalSearchController.cpp
    src/ui/controllers/KennelBoardController.cpp
    src/ui/controllers/ItemLedgerController.cpp
    src/ui/controllers/ReportsController.cpp
    src/ui/controllers/AppController.cpp
    src/ui/controllers/AdminPanelController.cpp
    src/ui/controllers/AuditLogController.cpp
    src/ui/controllers/AlertsPanelController.cpp
    src/ui/controllers/SchedulerPanelController.cpp
    src/ui/controllers/DiagnosticsController.cpp

    # Infrastructure additions — update/rollback
    src/infrastructure/UpdateManager.cpp
)

target_include_directories(shelterops_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(shelterops_lib PUBLIC
    SQLite::SQLite3
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    unofficial-sodium::sodium
)

target_compile_features(shelterops_lib PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(shelterops_lib PRIVATE /W4 /WX /utf-8)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(shelterops_lib PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# Main GUI executable — Windows only (Win32 + DirectX 11 + Dear ImGui)
# ---------------------------------------------------------------------------
if (WIN32)
    add_executable(ShelterOpsDesk WIN32
        src/main.cpp
        src/ui/views/LoginView.cpp
        src/ui/views/KennelBoardView.cpp
        src/ui/views/ItemLedgerView.cpp
        src/ui/views/ReportsView.cpp
        src/ui/views/AdminPanelView.cpp
        src/ui/views/AuditLogView.cpp
        src/ui/views/AlertsPanelView.cpp
        src/ui/views/SchedulerPanelView.cpp
        src/shell/DockspaceShell.cpp
        src/shell/TrayManager.cpp
        src/shell/ClipboardHelper.cpp
    )

    target_link_libraries(ShelterOpsDesk PRIVATE
        shelterops_lib
        imgui::imgui
        implot::implot
        d3d11.lib
        dxgi.lib
    )

    if (MSVC)
        target_compile_options(ShelterOpsDesk PRIVATE /W4 /WX /utf-8)
        set_target_properties(ShelterOpsDesk PROPERTIES
            VS_DPI_AWARE "PerMonitor"
        )
        target_sources(ShelterOpsDesk PRIVATE
            resources/ShelterOpsDesk.exe.manifest
        )
    endif()

    target_compile_definitions(ShelterOpsDesk PRIVATE
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:SHELTEROPS_DEBUG_LEAKS=1>
    )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(unit_tests)
add_subdirectory(api_tests)
