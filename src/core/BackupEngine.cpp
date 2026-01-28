// core/BackupEngine.cpp
#include "BackupEngine.hpp"

#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "../ntfs/MftParser.hpp"
#include "../ntfs/NtfsVolumeReader.hpp"

namespace ntbackup::core {

BackupEngine::BackupEngine(BackupConfig config,
                           std::unique_ptr<transport::IFileTransport> transport)
    : config_(std::move(config))
    , transport_(std::move(transport))
    , stats_{} {
}

BackupStats BackupEngine::Run(const ProgressCallback& onProgress) {
    stats_ = {};

    if (!transport_->Initialize()) {
        std::fprintf(stderr, "[BackupEngine] Failed to initialize transport\n");
        return stats_;
    }

    // Enumerate fixed NTFS drives
    for (char drive = 'A'; drive <= 'Z'; ++drive) {
        wchar_t rootPath[4] = {static_cast<wchar_t>(drive), L':', L'\\', L'\0'};
        UINT driveType = GetDriveTypeW(rootPath);

        if (driveType == DRIVE_FIXED && IsDriveNtfs(drive)) {
            std::fprintf(stderr, "[BackupEngine] Scanning drive %c:\n", drive);
            ScanDrive(drive, onProgress);
        }
    }

    transport_->Shutdown();
    return stats_;
}

bool BackupEngine::IsDriveNtfs(char driveLetter) const {
    wchar_t rootPath[4] = {static_cast<wchar_t>(driveLetter), L':', L'\\', L'\0'};
    wchar_t fsName[16] = {};

    if (GetVolumeInformationW(rootPath, nullptr, 0, nullptr, nullptr, nullptr,
                              fsName, 16)) {
        return wcscmp(fsName, L"NTFS") == 0;
    }
    return false;
}

void BackupEngine::ScanDrive(char driveLetter, const ProgressCallback& onProgress) {
    auto volumeReader = ntfs::NtfsVolumeReader::Open(driveLetter);
    if (!volumeReader) {
        std::fprintf(stderr, "[BackupEngine] Failed to open volume %c:\n", driveLetter);
        return;
    }

    ntfs::MftParser parser(volumeReader.get());

    parser.EnumerateFiles([&](const ntfs::FileEntry& entry) -> bool {
        ++stats_.filesScanned;

        // Skip directories and empty files
        if (entry.isDirectory || entry.fileSize == 0) {
            return true;
        }

        // Skip oversized files
        if (entry.fileSize > config_.maxFileSize) {
            return true;
        }

        // Check extension
        if (config_.targetExtensions.find(entry.extension) == config_.targetExtensions.end()) {
            return true;
        }

        ++stats_.filesMatched;

        // Reconstruct path
        std::wstring fullPath = parser.ReconstructPath(entry.parentRecordNumber);
        fullPath += L'\\';
        fullPath += entry.fileName;

        if (onProgress) {
            onProgress(fullPath, stats_);
        }

        // Read file data
        auto dataOpt = parser.ReadFileData(entry);
        if (!dataOpt) {
            ++stats_.filesFailed;
            if (config_.verbose) {
                std::fwprintf(stderr, L"[BackupEngine] Failed to read: %ls\n",
                             fullPath.c_str());
            }
            return true;
        }

        // Build metadata
        transport::FileMetadata metadata{};
        metadata.fileName = entry.fileName;
        metadata.fullPath = fullPath;
        metadata.extension = entry.extension;
        metadata.fileSize = entry.fileSize;
        metadata.mftRecordNumber = entry.recordNumber;
        metadata.parentRecordNumber = entry.parentRecordNumber;
        metadata.sourceDrive = driveLetter;
        metadata.creationTime = entry.creationTime;
        metadata.modificationTime = entry.modificationTime;

        // Send to server
        auto result = transport_->Send(metadata, *dataOpt);
        if (result == transport::TransportResult::Success) {
            ++stats_.filesSent;
            stats_.bytesTransferred += dataOpt->size();
            if (config_.verbose) {
                std::fwprintf(stderr, L"[BackupEngine] Sent: %ls\n", fullPath.c_str());
            }
        } else {
            ++stats_.filesFailed;
            if (config_.verbose) {
                std::fwprintf(stderr, L"[BackupEngine] Failed to send: %ls\n",
                             fullPath.c_str());
            }
        }

        return true;
    });
}

}  // namespace ntbackup::core
