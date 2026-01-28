// core/BackupEngine.hpp
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

#include "../transport/IFileTransport.hpp"

namespace ntbackup::core {

struct BackupConfig {
    std::unordered_set<std::wstring> targetExtensions;  // Lowercase with dot
    bool verbose = false;
    uint64_t maxFileSize = 100 * 1024 * 1024;           // Skip files larger than this
};

struct BackupStats {
    uint64_t filesScanned = 0;
    uint64_t filesMatched = 0;
    uint64_t filesSent = 0;
    uint64_t filesFailed = 0;
    uint64_t bytesTransferred = 0;
};

class BackupEngine {
public:
    BackupEngine(BackupConfig config, std::unique_ptr<transport::IFileTransport> transport);

    using ProgressCallback = std::function<void(const std::wstring& currentFile,
                                                 const BackupStats& stats)>;

    BackupStats Run(const ProgressCallback& onProgress = nullptr);

private:
    void ScanDrive(char driveLetter, const ProgressCallback& onProgress);
    bool IsDriveNtfs(char driveLetter) const;

    BackupConfig config_;
    std::unique_ptr<transport::IFileTransport> transport_;
    BackupStats stats_;
};

}  // namespace ntbackup::core
