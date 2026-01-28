// ntfs/NtfsVolumeReader.hpp
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "NtfsStructures.hpp"
#include "DataRunReader.hpp"

namespace ntbackup::ntfs {

class NtfsVolumeReader {
public:
    static std::unique_ptr<NtfsVolumeReader> Open(char driveLetter);

    ~NtfsVolumeReader();

    NtfsVolumeReader(const NtfsVolumeReader&) = delete;
    NtfsVolumeReader& operator=(const NtfsVolumeReader&) = delete;

    bool ReadClusters(uint64_t clusterNumber, uint32_t count, void* buffer) const;
    std::optional<std::vector<uint8_t>> ReadMftRecord(uint64_t recordNumber) const;

    // Read file data using data runs (handles fragmentation)
    std::optional<std::vector<uint8_t>> ReadDataRuns(const std::vector<DataRun>& runs,
                                                      uint64_t dataSize) const;

    [[nodiscard]] char DriveLetter() const { return driveLetter_; }
    [[nodiscard]] uint32_t BytesPerSector() const { return bootSector_.bytesPerSector; }
    [[nodiscard]] uint32_t BytesPerCluster() const { return bytesPerCluster_; }
    [[nodiscard]] uint32_t MftRecordSize() const { return mftRecordSize_; }

private:
    NtfsVolumeReader(HANDLE volumeHandle, char driveLetter);

    bool Initialize();
    bool ReadSectors(uint64_t sectorNumber, uint32_t count, void* buffer) const;
    bool ApplyFixups(uint8_t* record, uint32_t recordSize) const;
    bool ParseMftDataRuns();

    HANDLE volumeHandle_;
    char driveLetter_;
    BootSector bootSector_;
    uint32_t bytesPerCluster_;
    uint32_t mftRecordSize_;
    std::vector<DataRun> mftDataRuns_;  // $MFT can be fragmented
};

}  // namespace ntbackup::ntfs
