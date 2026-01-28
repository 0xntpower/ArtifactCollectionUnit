// ntfs/DataRunReader.hpp
#pragma once

#include <cstdint>
#include <vector>

namespace ntbackup::ntfs {

struct DataRun {
    uint64_t clusterOffset;     // Absolute cluster number (0 = sparse)
    uint64_t clusterCount;
};

class DataRunReader {
public:
    // Parse data runs from raw bytes, returns list of runs
    static std::vector<DataRun> Parse(const uint8_t* dataRuns, size_t maxLength);

    // Calculate total logical size in clusters
    static uint64_t TotalClusters(const std::vector<DataRun>& runs);
};

}  // namespace ntbackup::ntfs
