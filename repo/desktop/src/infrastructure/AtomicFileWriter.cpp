#include "shelterops/infrastructure/AtomicFileWriter.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace shelterops::infrastructure {

void AtomicFileWriter::WriteAtomic(const std::string& target_path,
                                    const std::vector<uint8_t>& data) {
    std::string tmp_path = target_path + ".tmp";

    // Write to temp file.
    {
        std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            throw std::runtime_error(
                "AtomicFileWriter: cannot open temp file: " + tmp_path);
        }
        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
        if (!f.good()) {
            throw std::runtime_error(
                "AtomicFileWriter: write failed to temp file: " + tmp_path);
        }
    } // flush + close

    // Atomic rename.
    std::error_code ec;
    fs::rename(tmp_path, target_path, ec);
    if (ec) {
        fs::remove(tmp_path, ec); // best-effort cleanup
        throw std::runtime_error(
            "AtomicFileWriter: rename failed from " + tmp_path +
            " to " + target_path + ": " + ec.message());
    }
}

void AtomicFileWriter::WriteAtomic(const std::string& target_path,
                                    const std::string& content) {
    std::vector<uint8_t> data(content.begin(), content.end());
    WriteAtomic(target_path, data);
}

} // namespace shelterops::infrastructure
