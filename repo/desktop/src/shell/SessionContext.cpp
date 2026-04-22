#include "shelterops/shell/SessionContext.h"

namespace shelterops::shell {

void SessionContext::Set(const services::UserContext& ctx) {
    std::lock_guard<std::mutex> lock(mu_);
    ctx_ = ctx;
}

void SessionContext::Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    ctx_.reset();
}

bool SessionContext::IsAuthenticated() const {
    std::lock_guard<std::mutex> lock(mu_);
    return ctx_.has_value();
}

services::UserContext SessionContext::Get() const {
    std::lock_guard<std::mutex> lock(mu_);
    return ctx_.value(); // throws std::bad_optional_access if not set
}

domain::UserRole SessionContext::CurrentRole() const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ctx_.has_value()) return domain::UserRole::Auditor;
    return ctx_->role;
}

} // namespace shelterops::shell
