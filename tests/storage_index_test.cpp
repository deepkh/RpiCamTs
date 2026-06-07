#include "storage_index.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace {

class TestDirectory {
  public:
    explicit TestDirectory(const std::string &name) {
        path_ = std::filesystem::temp_directory_path() /
                ("rpicamts-storage-index-" + std::to_string(::getpid()) +
                 "-" + name);
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::create_directories(path_);
    }

    ~TestDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_text(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    require(output.is_open(), "failed to create test file: " + path.string());
    output << text;
    require(static_cast<bool>(output),
            "failed to write test file: " + path.string());
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path);
    require(input.is_open(), "failed to read test file: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::string index_with_record(int folder, const std::string &filename) {
    std::string folder_text = std::to_string(folder);
    folder_text.insert(0, 3 - std::min<std::size_t>(3, folder_text.size()),
                       '0');
    return "version: 1\n"
           "maximum_file_num: 50\n"
           "file_segmentatin_size: 200MB\n"
           "folders:\n"
           "  " +
           folder_text + ":\n    - " + filename + "\n";
}

void test_fresh_storage_uses_numbered_index_and_padded_folder() {
    TestDirectory root("fresh");
    StorageIndex index;
    std::string error;
    require(index.load_or_create(root.path(), error), error);
    require(std::filesystem::exists(root.path() / "index000.yaml"),
            "fresh storage did not create index000.yaml");

    const StorageRecordPath next = index.next_record_path(error);
    require(error.empty(), error);
    require(next.folder_index == 0 && next.file_index == 0,
            "fresh storage did not start at record 000/000");
    require(next.relative_path.parent_path() == "000",
            "fresh storage folder was not zero-padded");

    require(index.append_record(next.folder_index, next.filename, error),
            error);
    require(index.save_atomic(error), error);
    const std::string saved = read_text(root.path() / "index000.yaml");
    require(saved.find("  000:\n    - 000_") != std::string::npos,
            "saved index did not use a padded folder key");
}

void test_latest_numbered_index_is_selected() {
    TestDirectory root("latest");
    write_text(root.path() / "index003.yaml",
               index_with_record(2, "004_aaaaaa.ts"));
    write_text(root.path() / "index005.yaml",
               index_with_record(7, "012_bbbbbb.ts"));

    StorageIndex index;
    std::string error;
    require(index.load_or_create(root.path(), error), error);
    const StorageRecordPath next = index.next_record_path(error);
    require(error.empty(), error);
    require(next.folder_index == 7 && next.file_index == 13,
            "the highest numbered index was not selected");
}

void test_corrupt_index_recovers_from_storage_scan() {
    TestDirectory root("recover");
    write_text(root.path() / "index000.yaml", "corrupt\n");
    write_text(root.path() / "000" / "000_aaaaaa.ts", "");
    write_text(root.path() / "000" / "003_bbbbbb.ts", "");
    write_text(root.path() / "003" / "002_cccccccc.ts", "");
    write_text(root.path() / "009" / "009_ddddddd.ts", "");

    StorageIndex index;
    std::string error;
    require(index.load_or_create(root.path(), error), error);
    const StorageRecordPath next = index.next_record_path(error);
    require(error.empty(), error);
    require(next.folder_index == 9 && next.file_index == 10,
            "recovery scan did not select record 009/010");
    require(next.relative_path.parent_path() == "009",
            "recovery folder was not zero-padded");

    require(index.append_record(next.folder_index, next.filename, error),
            error);
    require(index.save_atomic(error), error);
    require(read_text(root.path() / "index000.yaml") == "corrupt\n",
            "corrupted index was modified");

    const std::filesystem::path replacement = root.path() / "index001.yaml";
    require(std::filesystem::exists(replacement),
            "recovery did not create index001.yaml");
    const std::string saved = read_text(replacement);
    require(saved.find("  009:\n    - 010_") != std::string::npos,
            "replacement index did not contain the recovered next record");
    require(saved.find("009_ddddddd.ts") == std::string::npos,
            "replacement index copied historical records");

    StorageIndex restarted;
    require(restarted.load_or_create(root.path(), error), error);
    const StorageRecordPath after_restart = restarted.next_record_path(error);
    require(error.empty(), error);
    require(after_restart.folder_index == 9 && after_restart.file_index == 11,
            "replacement index did not restart at the following record");
}

void test_recovery_rolls_to_the_next_folder() {
    TestDirectory root("folder-rollover");
    write_text(root.path() / "index004.yaml", "corrupt\n");
    for (int file_index = 0; file_index < 50; ++file_index) {
        std::string filename = std::to_string(file_index);
        filename.insert(
            0, 3 - std::min<std::size_t>(3, filename.size()), '0');
        write_text(root.path() / "009" / (filename + "_aaaaaa.ts"), "");
    }

    StorageIndex index;
    std::string error;
    require(index.load_or_create(root.path(), error), error);
    const StorageRecordPath next = index.next_record_path(error);
    require(error.empty(), error);
    require(next.folder_index == 10 && next.file_index == 0,
            "recovery did not roll a full folder to 010/000");
    require(next.relative_path.parent_path() == "010",
            "rolled folder was not zero-padded");
}

void test_sparse_high_file_index_continues_from_highest_number() {
    TestDirectory root("sparse-high-index");
    write_text(root.path() / "index000.yaml",
               index_with_record(0, "000_aaaaaa.ts"));
    write_text(root.path() / "index009.yaml", "corrupt\n");
    write_text(root.path() / "000" / "000_aaaaaa.ts", "");
    write_text(root.path() / "000" / "010_bbbbbb.ts", "");
    write_text(root.path() / "003" / "010_gggggg.ts", "");
    write_text(root.path() / "003" / "100_hhhhhh.ts", "");

    StorageIndex index;
    std::string error;
    require(index.load_or_create(root.path(), error), error);
    const StorageRecordPath next = index.next_record_path(error);
    require(error.empty(), error);
    require(next.folder_index == 3 && next.file_index == 101,
            "sparse recovery did not continue after file index 100");
    require(next.relative_path.parent_path() == "003",
            "sparse recovery folder was not zero-padded");

    require(index.append_record(next.folder_index, next.filename, error),
            error);
    require(index.save_atomic(error), error);
    const std::string saved = read_text(root.path() / "index010.yaml");
    require(saved.find("  003:\n    - 101_") != std::string::npos,
            "index010.yaml did not record file index 101");

    StorageIndex restarted;
    require(restarted.load_or_create(root.path(), error), error);
    const StorageRecordPath after_restart = restarted.next_record_path(error);
    require(error.empty(), error);
    require(after_restart.folder_index == 3 &&
                after_restart.file_index == 102,
            "file indexes above maximum_file_num failed after restart");
}

void test_legacy_folder_names_are_scanned() {
    TestDirectory root("legacy-folder");
    write_text(root.path() / "index.yml", "corrupt\n");
    write_text(root.path() / "9" / "009_aaaaaa.ts", "");

    StorageIndex index;
    std::string error;
    require(index.load_or_create(root.path(), error), error);
    const StorageRecordPath next = index.next_record_path(error);
    require(error.empty(), error);
    require(next.folder_index == 9 && next.file_index == 10,
            "legacy folder name was not included in the recovery scan");
    require(next.relative_path.parent_path() == "009",
            "new output did not switch to a padded folder name");
}

void test_corrupt_index_999_reports_exhaustion() {
    TestDirectory root("exhausted");
    write_text(root.path() / "index999.yaml", "corrupt\n");

    StorageIndex index;
    std::string error;
    require(!index.load_or_create(root.path(), error),
            "corrupt index999.yaml unexpectedly recovered");
    require(error.find("cannot create a recovery index after index999.yaml") !=
                std::string::npos,
            "index exhaustion error was not descriptive");
}

} // namespace

int main() {
    try {
        test_fresh_storage_uses_numbered_index_and_padded_folder();
        test_latest_numbered_index_is_selected();
        test_corrupt_index_recovers_from_storage_scan();
        test_recovery_rolls_to_the_next_folder();
        test_sparse_high_file_index_continues_from_highest_number();
        test_legacy_folder_names_are_scanned();
        test_corrupt_index_999_reports_exhaustion();
    } catch (const std::exception &error) {
        std::cerr << "storage_index_test: " << error.what() << '\n';
        return 1;
    }

    std::cout << "storage_index_test: passed\n";
    return 0;
}
