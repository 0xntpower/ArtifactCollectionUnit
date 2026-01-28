// ntfs/NtfsVolumeReader.cpp
#include "NtfsVolumeReader.hpp"

#include <cstdio>
#include <cstring>

namespace ntbackup::ntfs {

namespace {

constexpr uint32_t kInitialSectorSize = 512;
constexpr uint32_t kClustersPerRead = 256;

}  // namespace

std::unique_ptr<NtfsVolumeReader> NtfsVolumeReader::Open(char driveLetter) {
    wchar_t volumePath[16];
    volumePath[0] = L'\\';
    volumePath[1] = L'\\';
    volumePath[2] = L'.';
    volumePath[3] = L'\\';
    volumePath[4] = static_cast<wchar_t>(driveLetter);
    volumePath[5] = L':';
    volumePath[6] = L'\0';

    HANDLE handle = CreateFileW(
        volumePath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "[NtfsVolumeReader] Failed to open %c: (error %lu)\n",
                    driveLetter, GetLastError());
        return nullptr;
    }

    auto reader = std::unique_ptr<NtfsVolumeReader>(
        new NtfsVolumeReader(handle, driveLetter)
    );

    if (!reader->Initialize()) {
        return nullptr;
    }

    return reader;
}

NtfsVolumeReader::NtfsVolumeReader(HANDLE volumeHandle, char driveLetter)
    : volumeHandle_(volumeHandle)
    , driveLetter_(driveLetter)
    , bootSector_{}
    , bytesPerCluster_(0)
    , mftRecordSize_(0) {
}

NtfsVolumeReader::~NtfsVolumeReader() {
    if (volumeHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(volumeHandle_);
    }
}

bool NtfsVolumeReader::Initialize() {
    // Read boot sector
    std::vector<uint8_t> sectorBuffer(kInitialSectorSize);
    
    LARGE_INTEGER offset;
    offset.QuadPart = 0;
    SetFilePointerEx(volumeHandle_, offset, nullptr, FILE_BEGIN);

    DWORD bytesRead = 0;
    if (!ReadFile(volumeHandle_, sectorBuffer.data(), kInitialSectorSize, &bytesRead, nullptr) ||
        bytesRead != kInitialSectorSize) {
        std::fprintf(stderr, "[NtfsVolumeReader] Failed to read boot sector\n");
        return false;
    }

    std::memcpy(&bootSector_, sectorBuffer.data(), sizeof(BootSector));

    // Validate NTFS
    if (std::memcmp(bootSector_.oemId.data(), "NTFS    ", 8) != 0) {
        std::fprintf(stderr, "[NtfsVolumeReader] Not an NTFS volume\n");
        return false;
    }

    bytesPerCluster_ = bootSector_.bytesPerSector * bootSector_.sectorsPerCluster;
    mftRecordSize_ = GetMftRecordSize(bootSector_.clustersPerMftRecord, bytesPerCluster_);

    std::fprintf(stderr, "[NtfsVolumeReader] %c: Bytes/cluster=%u, MFT record=%u bytes\n",
                driveLetter_, bytesPerCluster_, mftRecordSize_);

    // Parse $MFT's own data runs so we can read any MFT record
    if (!ParseMftDataRuns()) {
        std::fprintf(stderr, "[NtfsVolumeReader] Failed to parse $MFT data runs\n");
        return false;
    }

    return true;
}

bool NtfsVolumeReader::ParseMftDataRuns() {
    // Read $MFT (record 0) directly from the known starting cluster
    uint64_t mftStartByte = bootSector_.mftClusterNumber * bytesPerCluster_;

    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(mftStartByte);
    if (!SetFilePointerEx(volumeHandle_, offset, nullptr, FILE_BEGIN)) {
        return false;
    }

    // Align read to sector boundary
    uint32_t readSize = ((mftRecordSize_ + bootSector_.bytesPerSector - 1) / 
                         bootSector_.bytesPerSector) * bootSector_.bytesPerSector;

    std::vector<uint8_t> record(readSize);
    DWORD bytesRead = 0;
    if (!ReadFile(volumeHandle_, record.data(), readSize, &bytesRead, nullptr) ||
        bytesRead != readSize) {
        return false;
    }

    // Validate and apply fixups
    auto* header = reinterpret_cast<MftRecordHeader*>(record.data());
    if (header->signature != kMftRecordSignature) {
        return false;
    }

    if (!ApplyFixups(record.data(), mftRecordSize_)) {
        return false;
    }

    // Find $DATA attribute
    const uint8_t* pos = record.data() + header->firstAttributeOffset;
    const uint8_t* end = record.data() + mftRecordSize_;

    while (pos < end - sizeof(AttributeHeader)) {
        const auto* attr = reinterpret_cast<const AttributeHeader*>(pos);

        if (attr->type == AttributeType::End || attr->length == 0) {
            break;
        }

        if (attr->type == AttributeType::Data && attr->nameLength == 0) {
            if (attr->nonResident) {
                const auto* nonRes = reinterpret_cast<const NonResidentAttributeHeader*>(attr);
                const uint8_t* dataRunsPtr = pos + nonRes->dataRunsOffset;
                size_t maxRunLength = attr->length - nonRes->dataRunsOffset;

                mftDataRuns_ = DataRunReader::Parse(dataRunsPtr, maxRunLength);

                std::fprintf(stderr, "[NtfsVolumeReader] $MFT has %zu data runs\n",
                            mftDataRuns_.size());
                return !mftDataRuns_.empty();
            }
        }

        pos += attr->length;
    }

    return false;
}

bool NtfsVolumeReader::ReadSectors(uint64_t sectorNumber, uint32_t count, void* buffer) const {
    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(sectorNumber * bootSector_.bytesPerSector);

    if (!SetFilePointerEx(volumeHandle_, offset, nullptr, FILE_BEGIN)) {
        return false;
    }

    DWORD bytesToRead = count * bootSector_.bytesPerSector;
    DWORD bytesRead = 0;

    return ReadFile(volumeHandle_, buffer, bytesToRead, &bytesRead, nullptr) &&
           bytesRead == bytesToRead;
}

bool NtfsVolumeReader::ReadClusters(uint64_t clusterNumber, uint32_t count, void* buffer) const {
    uint64_t sectorNumber = clusterNumber * bootSector_.sectorsPerCluster;
    uint32_t sectorCount = count * bootSector_.sectorsPerCluster;
    return ReadSectors(sectorNumber, sectorCount, buffer);
}

std::optional<std::vector<uint8_t>> NtfsVolumeReader::ReadMftRecord(uint64_t recordNumber) const {
    // Calculate which byte offset within the MFT
    uint64_t byteOffset = recordNumber * mftRecordSize_;

    // Walk data runs to find the correct cluster
    uint64_t currentByteOffset = 0;

    for (const auto& run : mftDataRuns_) {
        uint64_t runBytes = run.clusterCount * bytesPerCluster_;

        if (byteOffset < currentByteOffset + runBytes) {
            // Record is in this run
            uint64_t offsetInRun = byteOffset - currentByteOffset;
            uint64_t clusterInRun = offsetInRun / bytesPerCluster_;
            uint64_t offsetInCluster = offsetInRun % bytesPerCluster_;

            if (run.clusterOffset == 0) {
                // Sparse run
                return std::nullopt;
            }

            uint64_t absoluteCluster = run.clusterOffset + clusterInRun;

            // Read enough clusters to cover the record
            uint32_t clustersNeeded = static_cast<uint32_t>(
                (offsetInCluster + mftRecordSize_ + bytesPerCluster_ - 1) / bytesPerCluster_
            );

            std::vector<uint8_t> clusterBuffer(clustersNeeded * bytesPerCluster_);
            if (!ReadClusters(absoluteCluster, clustersNeeded, clusterBuffer.data())) {
                return std::nullopt;
            }

            // Extract the record
            std::vector<uint8_t> record(mftRecordSize_);
            std::memcpy(record.data(), clusterBuffer.data() + offsetInCluster, mftRecordSize_);

            // Validate
            auto* header = reinterpret_cast<MftRecordHeader*>(record.data());
            if (header->signature != kMftRecordSignature) {
                return std::nullopt;
            }

            // Apply fixups (need non-const for this)
            if (!const_cast<NtfsVolumeReader*>(this)->ApplyFixups(record.data(), mftRecordSize_)) {
                return std::nullopt;
            }

            return record;
        }

        currentByteOffset += runBytes;
    }

    return std::nullopt;
}

std::optional<std::vector<uint8_t>> NtfsVolumeReader::ReadDataRuns(
    const std::vector<DataRun>& runs,
    uint64_t dataSize) const {

    if (runs.empty() || dataSize == 0) {
        return std::vector<uint8_t>{};
    }

    std::vector<uint8_t> result;
    result.reserve(static_cast<size_t>(dataSize));

    uint64_t bytesRemaining = dataSize;

    for (const auto& run : runs) {
        if (bytesRemaining == 0) {
            break;
        }

        uint64_t runBytes = run.clusterCount * bytesPerCluster_;
        uint64_t bytesToRead = (runBytes < bytesRemaining) ? runBytes : bytesRemaining;

        if (run.clusterOffset == 0) {
            // Sparse - fill with zeros
            result.resize(result.size() + static_cast<size_t>(bytesToRead), 0);
        } else {
            // Read in chunks
            uint64_t clustersInRun = run.clusterCount;
            uint64_t clusterOffset = 0;

            while (clusterOffset < clustersInRun && bytesRemaining > 0) {
                uint32_t toRead = static_cast<uint32_t>(
                    (clustersInRun - clusterOffset > kClustersPerRead) 
                        ? kClustersPerRead 
                        : (clustersInRun - clusterOffset)
                );

                std::vector<uint8_t> buffer(toRead * bytesPerCluster_);
                if (!ReadClusters(run.clusterOffset + clusterOffset, toRead, buffer.data())) {
                    return std::nullopt;
                }

                uint64_t usableBytes = toRead * bytesPerCluster_;
                if (usableBytes > bytesRemaining) {
                    usableBytes = bytesRemaining;
                }

                result.insert(result.end(), buffer.begin(), 
                             buffer.begin() + static_cast<size_t>(usableBytes));

                bytesRemaining -= usableBytes;
                clusterOffset += toRead;
            }
        }
    }

    result.resize(static_cast<size_t>(dataSize));  // Trim to exact size
    return result;
}

bool NtfsVolumeReader::ApplyFixups(uint8_t* record, uint32_t recordSize) const {
    auto* header = reinterpret_cast<MftRecordHeader*>(record);

    uint16_t* updateSequence = reinterpret_cast<uint16_t*>(
        record + header->updateSequenceOffset
    );

    uint16_t sequenceNumber = updateSequence[0];
    uint32_t sequenceCount = header->updateSequenceSize - 1;

    for (uint32_t i = 0; i < sequenceCount; ++i) {
        uint32_t sectorEnd = (i + 1) * bootSector_.bytesPerSector - 2;
        if (sectorEnd + 2 > recordSize) {
            break;
        }

        uint16_t* fixupLocation = reinterpret_cast<uint16_t*>(record + sectorEnd);
        if (*fixupLocation != sequenceNumber) {
            return false;
        }

        *fixupLocation = updateSequence[i + 1];
    }

    return true;
}

}  // namespace ntbackup::ntfs
