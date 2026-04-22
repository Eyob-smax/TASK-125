#include <gtest/gtest.h>
#include "shelterops/infrastructure/AtomicFileWriter.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace shelterops::infrastructure;

class AtomicFileWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = fs::temp_directory_path() / "shelterops_afw_test";
        fs::create_directories(test_dir_);
    }
    void TearDown() override {
        fs::remove_all(test_dir_);
    }
    fs::path test_dir_;
};

TEST_F(AtomicFileWriterTest, CreatesFileWithContent) {
    auto path = (test_dir_ / "out.txt").string();
    AtomicFileWriter::WriteAtomic(path, "hello world");
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    EXPECT_EQ(ss.str(), "hello world");
}

TEST_F(AtomicFileWriterTest, ConsecutiveWritesReplace) {
    auto path = (test_dir_ / "out.txt").string();
    AtomicFileWriter::WriteAtomic(path, "first");
    AtomicFileWriter::WriteAtomic(path, "second");
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    EXPECT_EQ(ss.str(), "second");
}

TEST_F(AtomicFileWriterTest, WriteBytesOverload) {
    auto path = (test_dir_ / "bin.bin").string();
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    AtomicFileWriter::WriteAtomic(path, data);
    EXPECT_TRUE(fs::exists(path));
    EXPECT_EQ(fs::file_size(path), 3u);
}

TEST_F(AtomicFileWriterTest, TempFileRemovedOnSuccess) {
    auto path = (test_dir_ / "final.txt").string();
    AtomicFileWriter::WriteAtomic(path, "data");
    std::string tmp = path + ".tmp";
    EXPECT_FALSE(fs::exists(tmp)); // temp file should be gone after rename
}

TEST_F(AtomicFileWriterTest, ThrowsOnInvalidDirectory) {
    // Writing to a non-existent directory should fail.
    std::string bad_path = (test_dir_ / "nonexistent" / "file.txt").string();
    EXPECT_THROW(AtomicFileWriter::WriteAtomic(bad_path, "x"), std::runtime_error);
}
