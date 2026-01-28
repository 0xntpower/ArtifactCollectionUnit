// ntfs/MftParser.cpp
#include "MftParser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>

namespace ntbackup::ntfs {

namespace {

constexpr uint64_t kMaxMftRecords = 50'000'000;
constexpr uint32_t kMaxPathDepth = 256;

std::wstring ExtractExtension(const std::wstring& fileName) {
    size_t dotPos = fileName.rfind(L'.');
    if (dotPos == std::wstring::npos || dotPos == 0) {
        return L"";
    }

    std::wstring ext = fileName.substr(dotPos);
    for (auto& c : ext) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return ext;
}

}  // namespace

MftParser::MftParser(NtfsVolumeReader* volumeReader)
    : volumeReader_(volumeReader) {
}

bool MftParser::EnumerateFiles(const FileCallback& callback) {
    uint64_t consecutiveEmpty = 0;

    for (uint64_t i = 0; i < kMaxMftRecords; ++i) {
        auto recordOpt = volumeReader_->ReadMftRecord(i);

        if (!recordOpt) {
            ++consecutiveEmpty;
            if (consecutiveEmpty > 10000) {
                // Likely past end of MFT
                break;
            }
            continue;
        }

        consecutiveEmpty = 0;

        auto entryOpt = ParseMftRecord(*recordOpt, i);
        if (!entryOpt) {
            continue;
        }

        // Cache directories for path reconstruction
        if (entryOpt->isDirectory) {
            CacheDirectoryName(i, entryOpt->fileName, entryOpt->parentRecordNumber);
        }

        if (!callback(*entryOpt)) {
            return true;  // Callback requested stop
        }
    }

    return true;
}

std::optional<FileEntry> MftParser::GetFileEntry(uint64_t recordNumber) {
    auto recordOpt = volumeReader_->ReadMftRecord(recordNumber);
    if (!recordOpt) {
        return std::nullopt;
    }
    return ParseMftRecord(*recordOpt, recordNumber);
}

std::optional<FileEntry> MftParser::ParseMftRecord(const std::vector<uint8_t>& record,
                                                    uint64_t recordNumber) {
    const auto* header = reinterpret_cast<const MftRecordHeader*>(record.data());

    if ((header->flags & static_cast<uint16_t>(MftRecordFlags::InUse)) == 0) {
        return std::nullopt;
    }

    FileEntry entry{};
    entry.recordNumber = recordNumber;
    entry.isDirectory = (header->flags & static_cast<uint16_t>(MftRecordFlags::Directory)) != 0;

    // Get timestamps from $STANDARD_INFORMATION
    const auto* stdInfoAttr = FindAttribute(record.data(), AttributeType::StandardInformation);
    if (stdInfoAttr != nullptr) {
        const auto* resident = reinterpret_cast<const ResidentAttributeHeader*>(stdInfoAttr);
        const auto* stdInfo = reinterpret_cast<const StandardInformationAttribute*>(
            reinterpret_cast<const uint8_t*>(stdInfoAttr) + resident->valueOffset
        );
        entry.creationTime = stdInfo->creationTime;
        entry.modificationTime = stdInfo->modificationTime;
    }

    // Find best $FILE_NAME (prefer Win32 or Win32+DOS namespace)
    const auto* fileNameAttr = FindAttribute(record.data(), AttributeType::FileName);
    if (fileNameAttr == nullptr) {
        return std::nullopt;
    }

    const AttributeHeader* bestNameAttr = fileNameAttr;
    const AttributeHeader* current = fileNameAttr;

    while ((current = FindNextAttribute(record.data(), current, AttributeType::FileName)) != nullptr) {
        const auto* resident = reinterpret_cast<const ResidentAttributeHeader*>(current);
        const auto* fnData = reinterpret_cast<const FileNameAttribute*>(
            reinterpret_cast<const uint8_t*>(current) + resident->valueOffset
        );

        if (fnData->namespaceType == FileNameNamespace::Win32 ||
            fnData->namespaceType == FileNameNamespace::Win32AndDos) {
            bestNameAttr = current;
            break;
        }

        if (fnData->namespaceType == FileNameNamespace::Posix) {
            bestNameAttr = current;
        }
    }

    // Extract filename from best attribute
    const auto* nameResident = reinterpret_cast<const ResidentAttributeHeader*>(bestNameAttr);
    const auto* fileNameData = reinterpret_cast<const FileNameAttribute*>(
        reinterpret_cast<const uint8_t*>(bestNameAttr) + nameResident->valueOffset
    );

    entry.parentRecordNumber = ExtractRecordNumber(fileNameData->parentDirectoryReference);

    const wchar_t* namePtr = reinterpret_cast<const wchar_t*>(
        reinterpret_cast<const uint8_t*>(fileNameData) + sizeof(FileNameAttribute)
    );
    entry.fileName = std::wstring(namePtr, fileNameData->nameLength);
    entry.extension = ExtractExtension(entry.fileName);

    // Find unnamed $DATA attribute (main file data)
    const auto* dataAttr = FindAttribute(record.data(), AttributeType::Data);

    // Skip named data streams, find the default one
    while (dataAttr != nullptr && dataAttr->nameLength > 0) {
        dataAttr = FindNextAttribute(record.data(), dataAttr, AttributeType::Data);
    }

    if (dataAttr != nullptr) {
        if (dataAttr->nonResident == 0) {
            // Resident data
            const auto* resident = reinterpret_cast<const ResidentAttributeHeader*>(dataAttr);
            entry.isResident = true;
            entry.fileSize = resident->valueLength;

            const uint8_t* dataPtr = reinterpret_cast<const uint8_t*>(dataAttr) +
                                     resident->valueOffset;
            entry.residentData.assign(dataPtr, dataPtr + resident->valueLength);
        } else {
            // Non-resident data
            const auto* nonResident = reinterpret_cast<const NonResidentAttributeHeader*>(dataAttr);
            entry.isResident = false;
            entry.fileSize = nonResident->dataSize;

            const uint8_t* dataRunsPtr = reinterpret_cast<const uint8_t*>(dataAttr) +
                                         nonResident->dataRunsOffset;
            size_t maxRunLength = dataAttr->length - nonResident->dataRunsOffset;

            entry.dataRuns = DataRunReader::Parse(dataRunsPtr, maxRunLength);
        }
    }

    return entry;
}

const AttributeHeader* MftParser::FindAttribute(const uint8_t* record,
                                                 AttributeType type,
                                                 const wchar_t* name) const {
    const auto* header = reinterpret_cast<const MftRecordHeader*>(record);
    const uint8_t* pos = record + header->firstAttributeOffset;
    const uint8_t* end = record + volumeReader_->MftRecordSize();

    while (pos < end - sizeof(AttributeHeader)) {
        const auto* attr = reinterpret_cast<const AttributeHeader*>(pos);

        if (attr->type == AttributeType::End || attr->length == 0) {
            break;
        }

        if (attr->type == type) {
            if (name == nullptr) {
                return attr;
            }

            // Check name match
            if (attr->nameLength > 0) {
                const wchar_t* attrName = reinterpret_cast<const wchar_t*>(pos + attr->nameOffset);
                if (std::wcsncmp(attrName, name, attr->nameLength) == 0) {
                    return attr;
                }
            }
        }

        pos += attr->length;
    }

    return nullptr;
}

const AttributeHeader* MftParser::FindNextAttribute(const uint8_t* record,
                                                     const AttributeHeader* current,
                                                     AttributeType type) const {
    const auto* header = reinterpret_cast<const MftRecordHeader*>(record);
    const uint8_t* pos = reinterpret_cast<const uint8_t*>(current) + current->length;
    const uint8_t* end = record + volumeReader_->MftRecordSize();

    while (pos < end - sizeof(AttributeHeader)) {
        const auto* attr = reinterpret_cast<const AttributeHeader*>(pos);

        if (attr->type == AttributeType::End || attr->length == 0) {
            break;
        }

        if (attr->type == type) {
            return attr;
        }

        pos += attr->length;
    }

    return nullptr;
}

std::optional<std::vector<uint8_t>> MftParser::ReadFileData(const FileEntry& entry) {
    if (entry.isResident) {
        return entry.residentData;
    }

    if (entry.dataRuns.empty()) {
        return std::vector<uint8_t>{};
    }

    return volumeReader_->ReadDataRuns(entry.dataRuns, entry.fileSize);
}

void MftParser::CacheDirectoryName(uint64_t recordNumber, const std::wstring& name,
                                    uint64_t parentRecord) {
    directoryCache_[recordNumber] = DirectoryInfo{name, parentRecord};
}

std::wstring MftParser::ReconstructPath(uint64_t recordNumber) {
    std::vector<std::wstring> components;
    uint64_t current = recordNumber;
    uint32_t depth = 0;

    while (current != kRootDirectoryRecordNumber && depth < kMaxPathDepth) {
        auto it = directoryCache_.find(current);
        if (it == directoryCache_.end()) {
            // Try to load this record
            auto entryOpt = GetFileEntry(current);
            if (!entryOpt) {
                break;
            }
            CacheDirectoryName(current, entryOpt->fileName, entryOpt->parentRecordNumber);
            it = directoryCache_.find(current);
        }

        if (it != directoryCache_.end()) {
            components.push_back(it->second.name);
            current = it->second.parentRecord;
        } else {
            break;
        }

        ++depth;
    }

    // Build path from root
    std::wstring path;
    path += static_cast<wchar_t>(volumeReader_->DriveLetter());
    path += L':';

    for (auto it = components.rbegin(); it != components.rend(); ++it) {
        path += L'\\';
        path += *it;
    }

    return path;
}

}  // namespace ntbackup::ntfs
