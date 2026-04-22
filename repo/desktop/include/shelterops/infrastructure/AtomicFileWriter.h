#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace shelterops::infrastructure {

// Writes data to `target_path` atomically using a write-temp-then-rename
// pattern. If the process crashes during write, the target is not corrupted;
// the temporary file is left behind and cleaned up on the next call.
class AtomicFileWriter {
public:
    // Writes `data` to `target_path` atomically.
    // Throws std::runtime_error on I/O failure.
    static void WriteAtomic(const std::string&         target_path,
                             const std::vector<uint8_t>& data);

    // Convenience overload for string content.
    static void WriteAtomic(const std::string& target_path,
                             const std::string& content);
};

} // namespace shelterops::infrastructure
