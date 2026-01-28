# NTBackup - Raw NTFS Document Backup Tool

A C++20 Windows backup utility that reads files directly from the NTFS Master File Table (MFT), bypassing standard filesystem APIs. This approach minimizes interference from security software that hooks standard file access APIs.

## Features

- Direct NTFS MFT parsing (no filesystem API hooks)
- Handles fragmented files via data run chaining
- Full path reconstruction from MFT parent references
- Configurable file extension filtering
- Pluggable transport layer for sending files to backup server
- Progress reporting during scan

## Requirements

- Windows 10/11 (64-bit)
- Visual Studio 2022 with C++20 support
- CMake 3.20+
- Administrator privileges (required for raw volume access)

## Building

### Using CMake + Visual Studio

```powershell
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Using Developer Command Prompt

```batch
cl /std:c++20 /EHsc /O2 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE ^
   main.cpp ^
   ntfs\DataRunReader.cpp ^
   ntfs\NtfsVolumeReader.cpp ^
   ntfs\MftParser.cpp ^
   core\BackupEngine.cpp ^
   /Fe:ntbackup.exe ^
   kernel32.lib
```

## Usage

```powershell
# Run as Administrator
.\ntbackup.exe [server_address]

# Examples
.\ntbackup.exe 192.168.1.100:9000
.\ntbackup.exe backup.local:8080
```

## Implementing the Transport Layer

The `StubTransport` class in `transport/StubTransport.hpp` is a placeholder. To actually send files to your home server, implement the `IFileTransport` interface:

```cpp
struct IFileTransport {
    virtual bool Initialize() = 0;
    virtual TransportResult Send(const FileMetadata& metadata,
                                  const std::vector<uint8_t>& data) = 0;
    virtual void Shutdown() = 0;
};
```

Suggested implementations:
- **Raw TCP** with TLV framing
- **gRPC** bidirectional streaming
- **HTTPS POST** to a receiving endpoint
- **WebSocket** with binary frames

## Configuration

Edit `main.cpp` to change:

```cpp
core::BackupConfig config{
    .targetExtensions = {L".pdf", L".docx"},  // File types to backup
    .verbose = true,                           // Log each file
    .maxFileSize = 100 * 1024 * 1024          // Skip files > 100MB
};
```

## How It Works

1. **Volume Access**: Opens raw volume handle (`\\.\C:`) with `GENERIC_READ`
2. **Boot Sector**: Reads NTFS boot sector to find MFT location and cluster size
3. **MFT Parsing**: Walks the Master File Table, parsing each 1024-byte record
4. **Attribute Extraction**: Extracts `$FILE_NAME`, `$STANDARD_INFORMATION`, and `$DATA` attributes
5. **Data Reading**: For non-resident files, follows data runs to read actual file content
6. **Path Reconstruction**: Walks parent directory references to build full paths
7. **Transport**: Sends matching files to configured backup server
