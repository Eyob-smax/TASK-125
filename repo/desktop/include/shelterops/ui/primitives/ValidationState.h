#pragma once
#include <string>
#include <unordered_map>

namespace shelterops::ui::primitives {

// Per-form validation error map.
// Views call SetError() as they validate; HasErrors() gates submission.
class ValidationState {
public:
    void Clear();
    void SetError(const std::string& field, const std::string& message);
    void ClearField(const std::string& field);

    bool HasErrors()                      const noexcept;
    bool HasError(const std::string& field) const noexcept;

    // Returns the error message for `field`, or "" if none.
    const std::string& GetError(const std::string& field) const noexcept;

    // All messages joined by "; " — suitable for an error banner.
    std::string AllErrors() const;

private:
    std::unordered_map<std::string, std::string> errors_;
    static const std::string kEmpty_;
};

} // namespace shelterops::ui::primitives
