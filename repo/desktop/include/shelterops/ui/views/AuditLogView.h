#pragma once
#if defined(_WIN32)
#include "shelterops/ui/controllers/AuditLogController.h"
#include "shelterops/shell/ClipboardHelper.h"
#include "shelterops/shell/SessionContext.h"
#include <cstdint>

namespace shelterops::ui::views {

// Audit Log view — append-only, searchable, masked for Auditor role.
class AuditLogView {
public:
    AuditLogView(controllers::AuditLogController& ctrl,
                 shell::ClipboardHelper&          clipboard,
                 shell::SessionContext&            session);

    bool Render(int64_t now_unix);

    bool IsOpen() const noexcept { return open_; }
    void Open()   noexcept { open_ = true; }
    void Close()  noexcept { open_ = false; }

private:
    void RenderFilterBar(int64_t now_unix);
    void RenderEventTable();
    void RenderExportBar(int64_t now_unix);

    controllers::AuditLogController& ctrl_;
    shell::ClipboardHelper&          clipboard_;
    shell::SessionContext&            session_;
    bool                             open_ = true;
};

} // namespace shelterops::ui::views
#endif // _WIN32
