#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {

struct FileInfo {
    std::string path;
    std::filesystem::file_time_type mtime{};
    std::uintmax_t size_bytes = 0;
};

struct ScanConfig {
    bool recursive = true;
    bool only_txt = true;
    std::size_t max_files = 0; // 0 = no limit
};

class FileScanner {
public:
    explicit FileScanner(ScanConfig cfg = {});

    // Scan directory and return list of files
    std::vector<FileInfo> scan(const std::string& root_dir) const;

private:
    ScanConfig cfg_;

    bool accept_path_(const std::filesystem::directory_entry& de) const;
};

} // namespace core
