// transport/IFileTransport.hpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ntbackup::transport {

struct FileMetadata {
    std::wstring fileName;
    std::wstring fullPath;              // Reconstructed NTFS path
    std::wstring extension;
    uint64_t fileSize;
    uint64_t mftRecordNumber;
    uint64_t parentRecordNumber;
    char sourceDrive;
    uint64_t creationTime;              // FILETIME as uint64
    uint64_t modificationTime;
};

enum class TransportResult {
    Success,
    ConnectionFailed,
    TransferFailed,
    Cancelled
};

// Interface for file transport implementations
struct IFileTransport {
    virtual ~IFileTransport() = default;

    // Called once before any transfers begin
    virtual bool Initialize() = 0;

    // Called for each file to transfer
    // data may be empty for zero-byte files
    virtual TransportResult Send(const FileMetadata& metadata,
                                  const std::vector<uint8_t>& data) = 0;

    // Called once after all transfers complete
    virtual void Shutdown() = 0;
};

}  // namespace ntbackup::transport
