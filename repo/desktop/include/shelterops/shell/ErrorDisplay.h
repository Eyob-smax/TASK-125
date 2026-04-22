#pragma once
#include "shelterops/common/ErrorEnvelope.h"
#include <string>

namespace shelterops::shell {

// Maps ErrorEnvelope codes to user-friendly display strings.
// Never leaks stack traces, decrypted values, or internal error details.
class ErrorDisplay {
public:
    static std::string UserMessage(const common::ErrorEnvelope& envelope);

    // Convenience: map just the code.
    static std::string UserMessage(common::ErrorCode code,
                                    int retry_after_seconds = 0);
};

} // namespace shelterops::shell
