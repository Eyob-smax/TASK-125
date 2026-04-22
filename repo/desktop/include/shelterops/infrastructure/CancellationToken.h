#pragma once
#include <stop_token>
#include <stdexcept>
#include <memory>

namespace shelterops::infrastructure {

// Exception thrown by ThrowIfCancelled().
class OperationCancelledError : public std::runtime_error {
public:
    OperationCancelledError()
        : std::runtime_error("Operation was cancelled") {}
};

// Thin wrapper around std::stop_source / std::stop_token.
// Multiple CancellationToken instances sharing the same underlying source
// all reflect the same cancellation state.
class CancellationSource;

class CancellationToken {
public:
    explicit CancellationToken(std::stop_token token)
        : token_(std::move(token)) {}

    bool IsCancelled() const noexcept { return token_.stop_requested(); }

    void ThrowIfCancelled() const {
        if (IsCancelled())
            throw OperationCancelledError{};
    }

    const std::stop_token& StopToken() const noexcept { return token_; }

private:
    std::stop_token token_;
};

class CancellationSource {
public:
    CancellationSource() = default;

    // Cancel all tokens vended from this source.
    void Cancel() { source_.request_stop(); }

    bool IsCancelled() const noexcept { return source_.stop_requested(); }

    // Create a CancellationToken linked to this source.
    CancellationToken Token() { return CancellationToken{source_.get_token()}; }

private:
    std::stop_source source_;
};

} // namespace shelterops::infrastructure
