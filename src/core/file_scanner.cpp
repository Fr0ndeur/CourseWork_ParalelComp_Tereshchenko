#include "file_scanner.h"

#include <algorithm>

namespace core {

FileScanner::FileScanner(ScanConfig cfg) : cfg_(cfg) {}

bool FileScanner::accept_path_(const std::filesystem::directory_entry& de) const {
    if (!de.is_regular_file()) return false;
    if (!cfg_.only_txt) return true;

    auto ext = de.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return ext == ".txt";
}

std::vector<FileInfo> FileScanner::scan(const std::string& root_dir) const {
    namespace fs = std::filesystem;

    std::vector<FileInfo> out;

    fs::path root(root_dir);
    if (!fs::exists(root)) return out;
    if (!fs::is_directory(root)) return out;

    auto push_entry = [&](const fs::directory_entry& de) {
        if (!accept_path_(de)) return;
        FileInfo fi;
        fi.path = de.path().string();
        fi.mtime = de.last_write_time();
        fi.size_bytes = de.file_size();
        out.push_back(std::move(fi));
    };

    if (cfg_.recursive) {
        for (const auto& de : fs::recursive_directory_iterator(root)) {
            if (cfg_.max_files && out.size() >= cfg_.max_files) break;
            push_entry(de);
        }
    } else {
        for (const auto& de : fs::directory_iterator(root)) {
            if (cfg_.max_files && out.size() >= cfg_.max_files) break;
            push_entry(de);
        }
    }

    // stable order for reproducibility
    std::sort(out.begin(), out.end(), [](const FileInfo& a, const FileInfo& b) {
        return a.path < b.path;
    });

    return out;
}

} // namespace core
