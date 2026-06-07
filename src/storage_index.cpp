#include "storage_index.h"

#include <climits>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <regex>
#include <sstream>
#include <system_error>

namespace {

constexpr int kDefaultMaximumFileNum = 50;
constexpr char kDefaultFileSegmentationSize[] = "200MB";
constexpr std::uint64_t kMegabyteBytes = 1024ULL * 1024ULL;
constexpr std::uint64_t kGigabyteBytes = 1024ULL * 1024ULL * 1024ULL;
constexpr int kRandomPathAttempts = 100;
constexpr int kMaximumIndexNumber = 999;
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

bool parse_padded_nonnegative_int(const std::string &value, int &result) {
    if (value.empty()) {
        return false;
    }

    const std::size_t first_digit = value.find_first_not_of('0');
    return parse_nonnegative_int(
        first_digit == std::string::npos ? "0" : value.substr(first_digit),
        result);
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

std::string format_file_index(int file_index);

bool parse_record_filename(const std::string &filename, int &file_index) {
    static const std::regex pattern(R"(^([0-9]{3,})_[a-z0-9]{6}\.ts$)");
    std::smatch match;
    if (!std::regex_match(filename, match, pattern)) {
        return false;
    }
    return parse_padded_nonnegative_int(match[1].str(), file_index) &&
           match[1].str() == format_file_index(file_index);
}

std::string format_file_index(int file_index) {
    std::ostringstream output;
    output << std::setfill('0') << std::setw(3) << file_index;
    return output.str();
}

bool parse_scanned_record_filename(const std::string &filename,
                                   int &file_index) {
    static const std::regex pattern(R"(^([0-9]{3,})_.+\.ts$)");
    std::smatch match;
    if (!std::regex_match(filename, match, pattern)) {
        return false;
    }
    return parse_padded_nonnegative_int(match[1].str(), file_index) &&
           match[1].str() == format_file_index(file_index);
}

std::string format_folder_index(int folder_index) {
    std::ostringstream output;
    output << std::setfill('0') << std::setw(3) << folder_index;
    return output.str();
}

std::string format_index_filename(int index_number) {
    std::ostringstream output;
    output << "index" << std::setfill('0') << std::setw(3) << index_number
           << ".yaml";
    return output.str();
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

bool find_latest_index(const std::filesystem::path &storage_root,
                       bool &found, int &index_number,
                       std::filesystem::path &index_path,
                       std::string &error_message) {
    static const std::regex pattern(R"(^index([0-9]{3})\.yaml$)");
    found = false;
    index_number = 0;
    index_path.clear();

    std::error_code error;
    std::filesystem::directory_iterator iterator(storage_root, error);
    const std::filesystem::directory_iterator end;
    if (error) {
        error_message = "failed to scan storage directory '" +
                        storage_root.string() + "': " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            error_message = "failed to scan storage directory '" +
                            storage_root.string() + "': " + error.message();
            return false;
        }

        std::smatch match;
        const std::string filename = iterator->path().filename().string();
        if (!std::regex_match(filename, match, pattern)) {
            continue;
        }

        int candidate = 0;
        if (!parse_padded_nonnegative_int(match[1].str(), candidate)) {
            continue;
        }
        if (!found || candidate > index_number) {
            found = true;
            index_number = candidate;
            index_path = iterator->path();
        }
    }
    if (error) {
        error_message = "failed to scan storage directory '" +
                        storage_root.string() + "': " + error.message();
        return false;
    }
    return true;
}

} // namespace

bool StorageIndex::load_or_create(const std::filesystem::path &storage_root,
                                  std::string &error_message) {
    error_message.clear();
    folders_.clear();
    next_folder_index_ = 0;
    next_file_index_ = 0;
    maximum_file_num_ = kDefaultMaximumFileNum;
    file_segmentation_size_ = kDefaultFileSegmentationSize;
    file_segmentation_size_bytes_ = 200 * kMegabyteBytes;
    storage_root_ = storage_root;
    index_path_.clear();

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

    bool found = false;
    int index_number = 0;
    if (!find_latest_index(storage_root_, found, index_number, index_path_,
                           error_message)) {
        return false;
    }

    if (found) {
        bool needs_rewrite = false;
        std::string load_error;
        if (load_index_file(index_path_, false, needs_rewrite, load_error)) {
            return !needs_rewrite || save_atomic(error_message);
        }

        if (index_number >= kMaximumIndexNumber) {
            error_message = load_error +
                            "; cannot create a recovery index after " +
                            format_index_filename(kMaximumIndexNumber);
            return false;
        }

        folders_.clear();
        next_folder_index_ = 0;
        next_file_index_ = 0;
        maximum_file_num_ = kDefaultMaximumFileNum;
        file_segmentation_size_ = kDefaultFileSegmentationSize;
        file_segmentation_size_bytes_ = 200 * kMegabyteBytes;
        index_path_ =
            storage_root_ / format_index_filename(index_number + 1);
        return scan_storage(error_message);
    }

    const std::filesystem::path legacy_yaml = storage_root_ / "index.yaml";
    const std::filesystem::path legacy_yml = storage_root_ / "index.yml";
    std::filesystem::path legacy_path;
    for (const auto &candidate : {legacy_yaml, legacy_yml}) {
        const bool exists = std::filesystem::exists(candidate, error);
        if (error) {
            error_message = "failed to inspect legacy storage index '" +
                            candidate.string() + "': " + error.message();
            return false;
        }
        if (exists) {
            legacy_path = candidate;
            break;
        }
    }

    index_path_ = storage_root_ / format_index_filename(0);
    if (!legacy_path.empty()) {
        bool needs_rewrite = false;
        std::string load_error;
        if (!load_index_file(legacy_path, true, needs_rewrite, load_error)) {
            maximum_file_num_ = kDefaultMaximumFileNum;
            file_segmentation_size_ = kDefaultFileSegmentationSize;
            file_segmentation_size_bytes_ = 200 * kMegabyteBytes;
        }
        folders_.clear();
        next_folder_index_ = 0;
        next_file_index_ = 0;
    }

    if (!scan_storage(error_message)) {
        return false;
    }
    if (next_folder_index_ == 0 && next_file_index_ == 0) {
        return save_atomic(error_message);
    }
    return true;
}

bool StorageIndex::load_index_file(const std::filesystem::path &path,
                                   bool allow_unpadded_folders,
                                   bool &needs_rewrite,
                                   std::string &error_message) {
    needs_rewrite = false;
    folders_.clear();
    next_folder_index_ = 0;
    next_file_index_ = 0;

    std::ifstream input(path);
    if (!input.is_open()) {
        error_message = "failed to open storage index: " + path.string();
        return false;
    }

    std::string line;
    if (!std::getline(input, line) || line != "version: 1") {
        error_message = index_error(path, "expected 'version: 1'");
        return false;
    }
    if (!std::getline(input, line)) {
        error_message = index_error(
            path, "expected 'maximum_file_num:' or 'folders:'");
        return false;
    }
    if (line == "folders:") {
        needs_rewrite = true;
    } else {
        constexpr char maximum_file_num_prefix[] = "maximum_file_num: ";
        if (line.rfind(maximum_file_num_prefix, 0) != 0 ||
            !parse_positive_int(line.substr(sizeof(maximum_file_num_prefix) - 1),
                                maximum_file_num_)) {
            error_message = index_error(
                path, "expected a positive 'maximum_file_num' value");
            return false;
        }

        constexpr char segmentation_size_prefix[] =
            "file_segmentatin_size: ";
        if (!std::getline(input, line) ||
            line.rfind(segmentation_size_prefix, 0) != 0) {
            error_message = index_error(
                path, "expected 'file_segmentatin_size: <size>'");
            return false;
        }
        file_segmentation_size_ =
            line.substr(sizeof(segmentation_size_prefix) - 1);
        if (!parse_file_segmentation_size(
                file_segmentation_size_, file_segmentation_size_bytes_)) {
            error_message = index_error(
                path,
                "file_segmentatin_size must be a positive MB or GB value");
            return false;
        }

        if (!std::getline(input, line) || line != "folders:") {
            error_message = index_error(path, "expected 'folders:'");
            return false;
        }
    }

    static const std::regex folder_pattern(R"(^  ([0-9]+):$)");
    static const std::regex record_pattern(R"(^    - (.+)$)");
    int current_folder = -1;
    bool has_records = false;
    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, folder_pattern)) {
            int folder_index = 0;
            const std::string folder_text = match[1].str();
            const bool parsed = allow_unpadded_folders
                                    ? parse_padded_nonnegative_int(folder_text,
                                                                   folder_index)
                                    : parse_padded_nonnegative_int(folder_text,
                                                                   folder_index) &&
                                          folder_text ==
                                              format_folder_index(folder_index);
            if (!parsed) {
                error_message = index_error(
                    path, "folder indexes must use zero-padded numbers");
                return false;
            }
            if (current_folder >= 0) {
                const auto &previous = folders_.at(current_folder);
                if (previous.empty()) {
                    error_message =
                        index_error(path, "folders may not be empty");
                    return false;
                }
            }
            if (has_records &&
                (next_file_index_ != 0 ||
                 folder_index != next_folder_index_)) {
                error_message = index_error(
                    path, "folder indexes must follow the recording sequence");
                return false;
            }
            if (!folders_
                     .emplace(folder_index, std::vector<std::string>{})
                     .second) {
                error_message = index_error(path, "duplicate folder index");
                return false;
            }
            if (allow_unpadded_folders &&
                folder_text != format_folder_index(folder_index)) {
                needs_rewrite = true;
            }
            current_folder = folder_index;
            continue;
        }

        if (std::regex_match(line, match, record_pattern) &&
            current_folder >= 0) {
            auto &records = folders_.at(current_folder);
            int file_index = 0;
            const std::string filename = match[1].str();
            if (!parse_record_filename(filename, file_index)) {
                error_message = index_error(
                    path, "invalid managed record filename: " + filename);
                return false;
            }
            if (has_records &&
                (current_folder != next_folder_index_ ||
                 file_index != next_file_index_)) {
                error_message = index_error(
                    path, "record filenames must use contiguous numeric indexes");
                return false;
            }
            records.push_back(filename);
            if (records.size() >
                static_cast<std::size_t>(maximum_file_num_)) {
                error_message = index_error(
                    path, "a folder contains more records than allowed by "
                          "maximum_file_num");
                return false;
            }
            has_records = true;
            if (!advance_next_record(current_folder, file_index,
                                     records.size(),
                                     error_message)) {
                error_message = index_error(path, error_message);
                return false;
            }
            continue;
        }

        error_message = index_error(path, "unexpected line: " + line);
        return false;
    }

    if (input.bad()) {
        error_message = "failed to read storage index: " + path.string();
        return false;
    }
    if (current_folder >= 0 && folders_.at(current_folder).empty()) {
        error_message = index_error(path, "folders may not be empty");
        return false;
    }
    return true;
}

bool StorageIndex::scan_storage(std::string &error_message) {
    error_message.clear();
    static const std::regex folder_pattern(R"(^[0-9]+$)");

    bool found_folder = false;
    int latest_folder = 0;
    std::vector<std::filesystem::path> latest_folder_paths;
    std::error_code error;
    std::filesystem::directory_iterator iterator(storage_root_, error);
    const std::filesystem::directory_iterator end;
    if (error) {
        error_message = "failed to scan storage directory '" +
                        storage_root_.string() + "': " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            error_message = "failed to scan storage directory '" +
                            storage_root_.string() + "': " + error.message();
            return false;
        }

        const std::string name = iterator->path().filename().string();
        if (!std::regex_match(name, folder_pattern)) {
            continue;
        }
        int folder_index = 0;
        const bool canonical_legacy =
            parse_nonnegative_int(name, folder_index);
        if (!canonical_legacy &&
            (!parse_padded_nonnegative_int(name, folder_index) ||
             name != format_folder_index(folder_index))) {
            continue;
        }
        const bool is_directory = iterator->is_directory(error);
        if (error) {
            error_message = "failed to inspect storage folder '" +
                            iterator->path().string() + "': " +
                            error.message();
            return false;
        }
        if (!is_directory) {
            continue;
        }
        if (!found_folder || folder_index > latest_folder) {
            found_folder = true;
            latest_folder = folder_index;
            latest_folder_paths.clear();
            latest_folder_paths.push_back(iterator->path());
        } else if (folder_index == latest_folder) {
            latest_folder_paths.push_back(iterator->path());
        }
    }
    if (error) {
        error_message = "failed to scan storage directory '" +
                        storage_root_.string() + "': " + error.message();
        return false;
    }

    if (!found_folder) {
        next_folder_index_ = 0;
        next_file_index_ = 0;
        return true;
    }

    bool found_file = false;
    int latest_file = 0;
    std::size_t latest_folder_file_count = 0;
    for (const auto &latest_folder_path : latest_folder_paths) {
        std::filesystem::directory_iterator file_iterator(latest_folder_path,
                                                           error);
        if (error) {
            error_message = "failed to scan storage folder '" +
                            latest_folder_path.string() + "': " +
                            error.message();
            return false;
        }
        for (; file_iterator != end; file_iterator.increment(error)) {
            if (error) {
                error_message = "failed to scan storage folder '" +
                                latest_folder_path.string() + "': " +
                                error.message();
                return false;
            }

            int file_index = 0;
            if (!parse_scanned_record_filename(
                    file_iterator->path().filename().string(), file_index)) {
                continue;
            }
            const bool is_regular_file = file_iterator->is_regular_file(error);
            if (error) {
                error_message = "failed to inspect managed record '" +
                                file_iterator->path().string() + "': " +
                                error.message();
                return false;
            }
            if (is_regular_file) {
                ++latest_folder_file_count;
                if (!found_file || file_index > latest_file) {
                    found_file = true;
                    latest_file = file_index;
                }
            }
        }
        if (error) {
            error_message = "failed to scan storage folder '" +
                            latest_folder_path.string() + "': " +
                            error.message();
            return false;
        }
    }

    next_folder_index_ = latest_folder;
    next_file_index_ = 0;
    if (!found_file) {
        return true;
    }
    return advance_next_record(latest_folder, latest_file,
                               latest_folder_file_count, error_message);
}

bool StorageIndex::advance_next_record(int folder_index, int file_index,
                                       std::size_t folder_file_count,
                                       std::string &error_message) {
    if (folder_file_count < static_cast<std::size_t>(maximum_file_num_)) {
        if (file_index == INT_MAX) {
            error_message = "managed record file index limit reached";
            return false;
        }
        next_folder_index_ = folder_index;
        next_file_index_ = file_index + 1;
        return true;
    }
    if (folder_index == INT_MAX) {
        error_message = "managed storage folder index limit reached";
        return false;
    }
    next_folder_index_ = folder_index + 1;
    next_file_index_ = 0;
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

    if (folder_index != next_folder_index_ ||
        filename_index != next_file_index_) {
        error_message = "managed record path does not use the next index";
        return false;
    }

    auto &records = folders_[folder_index];
    if (records.size() >= static_cast<std::size_t>(maximum_file_num_)) {
        error_message = "managed storage folder already contains the maximum "
                        "number of indexed files";
        return false;
    }
    records.push_back(filename);
    if (!advance_next_record(folder_index, filename_index, records.size(),
                             error_message)) {
        records.pop_back();
        if (records.empty()) {
            folders_.erase(folder_index);
        }
        return false;
    }
    return true;
}

bool StorageIndex::save_atomic(std::string &error_message) const {
    error_message.clear();
    if (index_path_.empty()) {
        error_message = "storage index path is not initialized";
        return false;
    }

    const std::filesystem::path temporary_path =
        index_path_.string() + ".tmp";
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
        output << "  " << format_folder_index(folder_index) << ":\n";
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
    result.folder_index = next_folder_index_;
    result.file_index = next_file_index_;

    const std::filesystem::path folder =
        storage_root_ / format_folder_index(result.folder_index);
    for (int attempt = 0; attempt < kRandomPathAttempts; ++attempt) {
        result.filename = format_file_index(result.file_index) + "_" +
                          random_suffix() + ".ts";
        result.relative_path =
            std::filesystem::path(format_folder_index(result.folder_index)) /
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
