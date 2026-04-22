#include "shelterops/ui/primitives/ValidationState.h"
#include <sstream>

namespace shelterops::ui::primitives {

const std::string ValidationState::kEmpty_;

void ValidationState::Clear() { errors_.clear(); }

void ValidationState::SetError(const std::string& field, const std::string& msg) {
    errors_[field] = msg;
}

void ValidationState::ClearField(const std::string& field) {
    errors_.erase(field);
}

bool ValidationState::HasErrors() const noexcept { return !errors_.empty(); }

bool ValidationState::HasError(const std::string& field) const noexcept {
    return errors_.count(field) > 0;
}

const std::string& ValidationState::GetError(const std::string& field) const noexcept {
    auto it = errors_.find(field);
    return it != errors_.end() ? it->second : kEmpty_;
}

std::string ValidationState::AllErrors() const {
    std::ostringstream oss;
    bool first = true;
    for (const auto& [_, msg] : errors_) {
        if (!first) oss << "; ";
        oss << msg;
        first = false;
    }
    return oss.str();
}

} // namespace shelterops::ui::primitives
