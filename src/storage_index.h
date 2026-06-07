#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct StorageRecordPath {
    int folder_index = 0;
    int file_index = 0;
    std::string filename;
    std::filesystem::path relative_path;
    std::filesystem::path absolute_path;
};

class StorageIndex {
  public:
    bool load_or_create(const std::filesystem::path &storage_root,
                        std::string &error_message);

    bool append_record(int folder_index, const std::string &filename,
                       std::string &error_message);

    bool save_atomic(std::string &error_message) const;

    StorageRecordPath next_record_path(std::string &error_message) const;

    const std::map<int, std::vector<std::string>> &folders() const;
    int maximum_file_num() const;
    std::uint64_t file_segmentation_size_bytes() const;

  private:
    bool load_index_file(const std::filesystem::path &path,
                         bool allow_unpadded_folders, bool &needs_rewrite,
                         std::string &error_message);
    bool scan_storage(std::string &error_message);
    bool advance_next_record(int folder_index, int file_index,
                             std::size_t folder_file_count,
                             std::string &error_message);

    std::filesystem::path storage_root_;
    std::filesystem::path index_path_;
    std::map<int, std::vector<std::string>> folders_;
    int next_folder_index_ = 0;
    int next_file_index_ = 0;
    int maximum_file_num_ = 50;
    std::string file_segmentation_size_ = "200MB";
    std::uint64_t file_segmentation_size_bytes_ =
        200ULL * 1024ULL * 1024ULL;
};
