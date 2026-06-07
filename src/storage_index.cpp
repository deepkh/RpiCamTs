#include "storage_index.h"

#include <climits>
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <regex>
#include <system_error>

namespace {

constexpr int kDefaultMaximumFileNum = 10;
constexpr char kDefaultFileSegmentationSize[] = "1GB";
constexpr std::uint64_t kMegabyteBytes = 1024ULL * 1024ULL;
constexpr std::uint64_t kGigabyteBytes = 1024ULL * 1024ULL * 1024ULL;
constexpr int kRandomPathAttempts = 100;
constexpr char kRandomSuffixCharacters[] =
    "abcdefghijklmnopqrstuvwxyz0123456789";

bool parse_nonnegative_int(const std::string &value, int &result) {
    if (value.empty() || (value.size() > 1 && value.front() == '0')) {
        return false;
    }

    int parsed = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return false;
        }
        const int digit = character - '0';
        if (parsed > (INT_MAX - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }

    result = parsed;
    return true;
}

bool parse_positive_int(const std::string &value, int &result) {
    return parse_nonnegative_int(value, result) && result > 0;
}

bool parse_file_segmentation_size(const std::string &value,
                                  std::uint64_t &size_bytes) {
    static const std::regex pattern(R"(^([1-9][0-9]*)(MB|GB)$)");
    std::smatch match;
    if (!std::regex_match(value, match, pattern)) {
        return false;
    }

    std::uint64_t amount = 0;
    for (const char character : match[1].str()) {
        const std::uint64_t digit =
            static_cast<std::uint64_t>(character - '0');
        if (amount >
            (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        amount = amount * 10 + digit;
    }

    const std::uint64_t unit =
        match[2].str() == "MB" ? kMegabyteBytes : kGigabyteBytes;
    if (amount > std::numeric_limits<std::uint64_t>::max() / unit) {
        return false;
    }
    size_bytes = amount * unit;
    return true;
}

bool parse_record_filename(const std::string &filename, int &file_index) {
    static const std::regex pattern(R"(^([0-9]+)_[a-z0-9]{6}\.ts$)");
    std::smatch match;
    if (!std::regex_match(filename, match, pattern)) {
        return false;
    }
    return parse_nonnegative_int(match[1].str(), file_index);
}

std::string random_suffix() {
    thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<std::size_t> distribution(
        0, sizeof(kRandomSuffixCharacters) - 2);

    std::string suffix;
    suffix.reserve(6);
    for (int index = 0; index < 6; ++index) {
        suffix.push_back(kRandomSuffixCharacters[distribution(generator)]);
    }
    return suffix;
}

std::string index_error(const std::filesystem::path &path,
                        const std::string &detail) {
    return "invalid storage index '" + path.string() + "': " + detail;
}

} // namespace

bool StorageIndex::load_or_create(const std::filesystem::path &storage_root,
                                  std::string &error_message) {
    error_message.clear();
    folders_.clear();
    maximum_file_num_ = kDefaultMaximumFileNum;
    file_segmentation_size_ = kDefaultFileSegmentationSize;
    file_segmentation_size_bytes_ = kGigabyteBytes;
    storage_root_ = storage_root;
    index_path_ = storage_root_ / "index.yml";

    std::error_code error;
    if (std::filesystem::exists(storage_root_, error)) {
        if (error) {
            error_message = "failed to inspect storage directory '" +
                            storage_root_.string() + "': " + error.message();
            return false;
        }
        if (!std::filesystem::is_directory(storage_root_, error) || error) {
            error_message = "storage path is not a directory: " +
                            storage_root_.string();
            return false;
        }
    } else {
        std::filesystem::create_directories(storage_root_, error);
        if (error) {
            error_message = "failed to create storage directory '" +
                            storage_root_.string() + "': " + error.message();
            return false;
        }
    }

    const bool index_exists = std::filesystem::exists(index_path_, error);
    if (error) {
        error_message = "failed to inspect storage index '" +
                        index_path_.string() + "': " + error.message();
        return false;
    }
    if (!index_exists) {
        return save_atomic(error_message);
    }

    std::ifstream input(index_path_);
    if (!input.is_open()) {
        error_message = "failed to open storage index: " + index_path_.string();
        return false;
    }

    std::string line;
    if (!std::getline(input, line) || line != "version: 1") {
        error_message = index_error(index_path_, "expected 'version: 1'");
        return false;
    }
    bool legacy_format = false;
    if (!std::getline(input, line)) {
        error_message = index_error(
            index_path_, "expected 'maximum_file_num:' or 'folders:'");
        return false;
    }
    if (line == "folders:") {
        legacy_format = true;
    } else {
        constexpr char maximum_file_num_prefix[] = "maximum_file_num: ";
        if (line.rfind(maximum_file_num_prefix, 0) != 0 ||
            !parse_positive_int(line.substr(sizeof(maximum_file_num_prefix) - 1),
                                maximum_file_num_)) {
            error_message = index_error(
                index_path_, "expected a positive 'maximum_file_num' value");
            return false;
        }

        constexpr char segmentation_size_prefix[] =
            "file_segmentatin_size: ";
        if (!std::getline(input, line) ||
            line.rfind(segmentation_size_prefix, 0) != 0) {
            error_message = index_error(
                index_path_, "expected 'file_segmentatin_size: <size>'");
            return false;
        }
        file_segmentation_size_ =
            line.substr(sizeof(segmentation_size_prefix) - 1);
        if (!parse_file_segmentation_size(
                file_segmentation_size_, file_segmentation_size_bytes_)) {
            error_message = index_error(
                index_path_,
                "file_segmentatin_size must be a positive MB or GB value");
            return false;
        }

        if (!std::getline(input, line) || line != "folders:") {
            error_message = index_error(index_path_, "expected 'folders:'");
            return false;
        }
    }

    static const std::regex folder_pattern(R"(^  ([0-9]+):$)");
    static const std::regex record_pattern(R"(^    - (.+)$)");
    int current_folder = -1;
    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, folder_pattern)) {
            int folder_index = 0;
            if (!parse_nonnegative_int(match[1].str(), folder_index) ||
                folder_index != static_cast<int>(folders_.size())) {
                error_message = index_error(
                    index_path_, "folder indexes must be contiguous from zero");
                return false;
            }
            if (current_folder >= 0) {
                const auto &previous = folders_.at(current_folder);
                if (previous.empty()) {
                    error_message =
                        index_error(index_path_, "folders may not be empty");
                    return false;
                }
            }
            folders_.emplace(folder_index, std::vector<std::string>{});
            current_folder = folder_index;
            continue;
        }

        if (std::regex_match(line, match, record_pattern) &&
            current_folder >= 0) {
            auto &records = folders_.at(current_folder);
            int file_index = 0;
            const std::string filename = match[1].str();
            if (!parse_record_filename(filename, file_index) ||
                file_index != static_cast<int>(records.size())) {
                error_message = index_error(
                    index_path_,
                    "record filenames must use contiguous numeric indexes");
                return false;
            }
            if (records.size() >=
                static_cast<std::size_t>(maximum_file_num_)) {
                error_message = index_error(
                    index_path_, "a folder contains more records than allowed "
                                 "by maximum_file_num");
                return false;
            }
            records.push_back(filename);
            continue;
        }

        error_message = index_error(index_path_, "unexpected line: " + line);
        return false;
    }

    if (input.bad()) {
        error_message = "failed to read storage index: " + index_path_.string();
        return false;
    }
    if (current_folder >= 0 && folders_.at(current_folder).empty()) {
        error_message = index_error(index_path_, "folders may not be empty");
        return false;
    }

    if (legacy_format && !save_atomic(error_message)) {
        return false;
    }
    return true;
}

bool StorageIndex::append_record(int folder_index,
                                 const std::string &filename,
                                 std::string &error_message) {
    error_message.clear();

    int filename_index = 0;
    if (!parse_record_filename(filename, filename_index)) {
        error_message = "invalid managed record filename: " + filename;
        return false;
    }

    int expected_folder = 0;
    int expected_file = 0;
    if (!folders_.empty()) {
        const auto &last_folder = *folders_.rbegin();
        if (last_folder.second.size() <
            static_cast<std::size_t>(maximum_file_num_)) {
            expected_folder = last_folder.first;
            expected_file = static_cast<int>(last_folder.second.size());
        } else {
            expected_folder = last_folder.first + 1;
        }
    }

    if (folder_index != expected_folder || filename_index != expected_file) {
        error_message = "managed record path does not use the next index";
        return false;
    }

    auto &records = folders_[folder_index];
    if (records.size() >= static_cast<std::size_t>(maximum_file_num_)) {
        error_message = "managed storage folder already contains the maximum "
                        "number of files";
        return false;
    }
    records.push_back(filename);
    return true;
}

bool StorageIndex::save_atomic(std::string &error_message) const {
    error_message.clear();
    if (index_path_.empty()) {
        error_message = "storage index path is not initialized";
        return false;
    }

    const std::filesystem::path temporary_path =
        storage_root_ / "index.yml.tmp";
    std::ofstream output(temporary_path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        error_message = "failed to open temporary storage index: " +
                        temporary_path.string();
        return false;
    }

    output << "version: 1\n"
           << "maximum_file_num: " << maximum_file_num_ << '\n'
           << "file_segmentatin_size: " << file_segmentation_size_ << '\n'
           << "folders:\n";
    for (const auto &[folder_index, records] : folders_) {
        output << "  " << folder_index << ":\n";
        for (const std::string &filename : records) {
            output << "    - " << filename << '\n';
        }
    }
    output.flush();
    if (!output) {
        error_message = "failed to write temporary storage index: " +
                        temporary_path.string();
        output.close();
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
        return false;
    }
    output.close();
    if (!output) {
        error_message = "failed to close temporary storage index: " +
                        temporary_path.string();
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
        return false;
    }

    std::error_code error;
    std::filesystem::rename(temporary_path, index_path_, error);
    if (error) {
        error_message = "failed to replace storage index '" +
                        index_path_.string() + "': " + error.message();
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
        return false;
    }
    return true;
}

StorageRecordPath
StorageIndex::next_record_path(std::string &error_message) const {
    error_message.clear();
    StorageRecordPath result;

    if (!folders_.empty()) {
        const auto &last_folder = *folders_.rbegin();
        if (last_folder.second.size() <
            static_cast<std::size_t>(maximum_file_num_)) {
            result.folder_index = last_folder.first;
            result.file_index = static_cast<int>(last_folder.second.size());
        } else {
            result.folder_index = last_folder.first + 1;
        }
    }

    const std::filesystem::path folder =
        storage_root_ / std::to_string(result.folder_index);
    for (int attempt = 0; attempt < kRandomPathAttempts; ++attempt) {
        result.filename = std::to_string(result.file_index) + "_" +
                          random_suffix() + ".ts";
        result.relative_path =
            std::filesystem::path(std::to_string(result.folder_index)) /
            result.filename;
        result.absolute_path = folder / result.filename;

        std::error_code error;
        const bool exists = std::filesystem::exists(result.absolute_path, error);
        if (error) {
            error_message = "failed to inspect managed record path '" +
                            result.absolute_path.string() + "': " +
                            error.message();
            return {};
        }
        if (!exists) {
            return result;
        }
    }

    error_message = "failed to generate a unique managed record path after " +
                    std::to_string(kRandomPathAttempts) + " attempts";
    return {};
}

const std::map<int, std::vector<std::string>> &StorageIndex::folders() const {
    return folders_;
}

int StorageIndex::maximum_file_num() const { return maximum_file_num_; }

std::uint64_t StorageIndex::file_segmentation_size_bytes() const {
    return file_segmentation_size_bytes_;
}
