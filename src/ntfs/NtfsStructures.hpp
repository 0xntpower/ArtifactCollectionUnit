// ntfs/NtfsStructures.hpp
#pragma once

#include <cstdint>
#include <array>

namespace ntbackup::ntfs {

#pragma pack(push, 1)

struct BootSector {
    std::array<uint8_t, 3> jump;
    std::array<char, 8> oemId;
    uint16_t bytesPerSector;
    uint8_t sectorsPerCluster;
    uint16_t reservedSectors;
    std::array<uint8_t, 5> unused1;
    uint8_t mediaDescriptor;
    std::array<uint8_t, 2> unused2;
    uint16_t sectorsPerTrack;
    uint16_t numberOfHeads;
    uint32_t hiddenSectors;
    std::array<uint8_t, 8> unused3;
    uint64_t totalSectors;
    uint64_t mftClusterNumber;
    uint64_t mftMirrorClusterNumber;
    int8_t clustersPerMftRecord;
    std::array<uint8_t, 3> unused4;
    int8_t clustersPerIndexBlock;
    std::array<uint8_t, 3> unused5;
    uint64_t volumeSerialNumber;
    uint32_t checksum;
};

static_assert(sizeof(BootSector) == 84, "BootSector size mismatch");

constexpr uint32_t kMftRecordSignature = 0x454C4946;  // "FILE"
constexpr uint32_t kDefaultMftRecordSize = 1024;
constexpr uint64_t kRootDirectoryRecordNumber = 5;

struct MftRecordHeader {
    uint32_t signature;
    uint16_t updateSequenceOffset;
    uint16_t updateSequenceSize;
    uint64_t logFileSequenceNumber;
    uint16_t sequenceNumber;
    uint16_t hardLinkCount;
    uint16_t firstAttributeOffset;
    uint16_t flags;
    uint32_t usedSize;
    uint32_t allocatedSize;
    uint64_t baseRecordReference;
    uint16_t nextAttributeId;
    uint16_t padding;
    uint32_t mftRecordNumber;
};

enum class MftRecordFlags : uint16_t {
    InUse = 0x0001,
    Directory = 0x0002
};

enum class AttributeType : uint32_t {
    StandardInformation = 0x10,
    AttributeList = 0x20,
    FileName = 0x30,
    ObjectId = 0x40,
    SecurityDescriptor = 0x50,
    VolumeName = 0x60,
    VolumeInformation = 0x70,
    Data = 0x80,
    IndexRoot = 0x90,
    IndexAllocation = 0xA0,
    Bitmap = 0xB0,
    ReparsePoint = 0xC0,
    End = 0xFFFFFFFF
};

struct AttributeHeader {
    AttributeType type;
    uint32_t length;
    uint8_t nonResident;
    uint8_t nameLength;
    uint16_t nameOffset;
    uint16_t flags;
    uint16_t attributeId;
};

struct ResidentAttributeHeader : AttributeHeader {
    uint32_t valueLength;
    uint16_t valueOffset;
    uint16_t indexedFlag;
};

struct NonResidentAttributeHeader : AttributeHeader {
    uint64_t startingVcn;
    uint64_t lastVcn;
    uint16_t dataRunsOffset;
    uint16_t compressionUnit;
    uint32_t padding;
    uint64_t allocatedSize;
    uint64_t dataSize;
    uint64_t initializedSize;
};

struct StandardInformationAttribute {
    uint64_t creationTime;
    uint64_t modificationTime;
    uint64_t mftModificationTime;
    uint64_t accessTime;
    uint32_t fileAttributes;
    // Extended fields exist but we only need timestamps
};

enum class FileNameNamespace : uint8_t {
    Posix = 0,
    Win32 = 1,
    Dos = 2,
    Win32AndDos = 3
};

struct FileNameAttribute {
    uint64_t parentDirectoryReference;
    uint64_t creationTime;
    uint64_t modificationTime;
    uint64_t mftModificationTime;
    uint64_t accessTime;
    uint64_t allocatedSize;
    uint64_t dataSize;
    uint32_t flags;
    uint32_t reparseValue;
    uint8_t nameLength;
    FileNameNamespace namespaceType;
    // Followed by wchar_t[nameLength]
};

#pragma pack(pop)

inline constexpr uint32_t GetMftRecordSize(int8_t clustersPerRecord,
                                            uint32_t bytesPerCluster) {
    if (clustersPerRecord > 0) {
        return static_cast<uint32_t>(clustersPerRecord) * bytesPerCluster;
    }
    return 1u << static_cast<uint32_t>(-clustersPerRecord);
}

inline constexpr uint64_t ExtractRecordNumber(uint64_t reference) {
    return reference & 0x0000FFFFFFFFFFFF;
}

inline constexpr uint16_t ExtractSequenceNumber(uint64_t reference) {
    return static_cast<uint16_t>(reference >> 48);
}

}  // namespace ntbackup::ntfs
