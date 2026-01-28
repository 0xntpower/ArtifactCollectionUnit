// ntfs/MftParser.hpp
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "NtfsStructures.hpp"
#include "NtfsVolumeReader.hpp"
#include "DataRunReader.hpp"

namespace ntbackup::ntfs {

struct FileEntry {
    uint64_t recordNumber;
    uint64_t parentRecordNumber;
    std::wstring fileName;
    std::wstring extension;           // Lowercase with dot
    uint64_t fileSize;
    uint64_t creationTime;
    uint64_t modificationTime;
    bool isDirectory;
    bool isResident;
    std::vector<uint8_t> residentData;
    std::vector<DataRun> dataRuns;
};

class MftParser {
public:
    explicit MftParser(NtfsVolumeReader* volumeReader);

    using FileCallback = std::function<bool(const FileEntry&)>;

    // Enumerate all files, calls callback for each
    bool EnumerateFiles(const FileCallback& callback);

    // Get single file entry by record number
    std::optional<FileEntry> GetFileEntry(uint64_t recordNumber);

    // Read file data (handles resident and non-resident)
    std::optional<std::vector<uint8_t>> ReadFileData(const FileEntry& entry);

    // Reconstruct full path by walking parent references
    std::wstring ReconstructPath(uint64_t recordNumber);

private:
    std::optional<FileEntry> ParseMftRecord(const std::vector<uint8_t>& record,
                                             uint64_t recordNumber);

    const AttributeHeader* FindAttribute(const uint8_t* record, 
                                          AttributeType type,
                                          const wchar_t* name = nullptr) const;

    const AttributeHeader* FindNextAttribute(const uint8_t* record,
                                              const AttributeHeader* current,
                                              AttributeType type) const;

    void CacheDirectoryName(uint64_t recordNumber, const std::wstring& name,
                            uint64_t parentRecord);

    NtfsVolumeReader* volumeReader_;

    // Cache for path reconstruction
    struct DirectoryInfo {
        std::wstring name;
        uint64_t parentRecord;
    };
    std::unordered_map<uint64_t, DirectoryInfo> directoryCache_;
};

}  // namespace ntbackup::ntfs
