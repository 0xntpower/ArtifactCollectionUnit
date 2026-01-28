// ntfs/DataRunReader.cpp
#include "DataRunReader.hpp"

namespace ntbackup::ntfs {

std::vector<DataRun> DataRunReader::Parse(const uint8_t* dataRuns, size_t maxLength) {
    std::vector<DataRun> runs;
    const uint8_t* pos = dataRuns;
    const uint8_t* end = dataRuns + maxLength;
    int64_t currentCluster = 0;  // Running offset for relative addressing

    while (pos < end && *pos != 0) {
        uint8_t header = *pos++;
        uint8_t lengthSize = header & 0x0F;
        uint8_t offsetSize = (header >> 4) & 0x0F;

        if (lengthSize == 0 || lengthSize > 8 || offsetSize > 8) {
            break;
        }

        if (pos + lengthSize + offsetSize > end) {
            break;
        }

        // Read cluster count (unsigned)
        uint64_t clusterCount = 0;
        for (uint8_t i = 0; i < lengthSize; ++i) {
            clusterCount |= static_cast<uint64_t>(*pos++) << (i * 8);
        }

        // Read cluster offset (signed, relative to previous run)
        int64_t clusterOffset = 0;
        if (offsetSize > 0) {
            for (uint8_t i = 0; i < offsetSize; ++i) {
                clusterOffset |= static_cast<int64_t>(*pos++) << (i * 8);
            }
            // Sign extend
            if (clusterOffset & (1LL << (offsetSize * 8 - 1))) {
                clusterOffset |= ~((1LL << (offsetSize * 8)) - 1);
            }
            currentCluster += clusterOffset;
        }

        DataRun run{};
        run.clusterCount = clusterCount;
        run.clusterOffset = (offsetSize > 0) ? static_cast<uint64_t>(currentCluster) : 0;
        runs.push_back(run);
    }

    return runs;
}

uint64_t DataRunReader::TotalClusters(const std::vector<DataRun>& runs) {
    uint64_t total = 0;
    for (const auto& run : runs) {
        total += run.clusterCount;
    }
    return total;
}

}  // namespace ntbackup::ntfs
