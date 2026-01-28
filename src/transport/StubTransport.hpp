// transport/StubTransport.hpp
#pragma once

#include "IFileTransport.hpp"

#include <cstdio>

namespace ntbackup::transport {

// Stub implementation - replace with actual transport later
// 
// Suggested implementations:
//   - Raw TCP socket with custom TLV protocol
//   - HTTPS POST to a receiving endpoint
//   - SMB/CIFS to a network share (native Windows)
//   - SFTP/SCP using libssh2
//   - gRPC bidirectional stream
//   - WebSocket with binary frames
//
class StubTransport : public IFileTransport {
public:
    explicit StubTransport(const std::wstring& serverAddress)
        : serverAddress_(serverAddress) {
    }

    bool Initialize() override {
        std::fwprintf(stderr, L"[StubTransport] Would connect to: %ls\n", 
                     serverAddress_.c_str());
        
        // TODO: Establish connection to home server
        // 
        // Example TCP socket initialization:
        //   WSADATA wsaData;
        //   WSAStartup(MAKEWORD(2, 2), &wsaData);
        //   socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        //   connect(socket_, ...);
        //
        // Example TLS setup:
        //   Use SChannel (Windows native) or OpenSSL
        //
        return true;
    }

    TransportResult Send(const FileMetadata& metadata,
                         const std::vector<uint8_t>& data) override {
        std::fwprintf(stderr, L"[StubTransport] Would send: %ls (%llu bytes)\n",
                     metadata.fullPath.c_str(), metadata.fileSize);
        
        // TODO: Implement actual file transfer
        //
        // Suggested protocol (TLV-style):
        //   1. Send header:
        //      - Magic bytes (4 bytes)
        //      - Metadata length (4 bytes)
        //      - File data length (8 bytes)
        //   2. Send metadata (JSON or binary struct):
        //      - fileName (UTF-8)
        //      - fullPath (UTF-8)
        //      - extension (UTF-8)
        //      - fileSize (uint64)
        //      - creationTime (uint64)
        //      - modificationTime (uint64)
        //      - sourceDrive (char)
        //      - mftRecordNumber (uint64)
        //   3. Stream file data in chunks (e.g., 64KB)
        //   4. Wait for server ACK
        //   5. Handle errors/retries
        //
        // Example chunk sending:
        //   constexpr size_t kChunkSize = 64 * 1024;
        //   for (size_t offset = 0; offset < data.size(); offset += kChunkSize) {
        //       size_t chunkLen = min(kChunkSize, data.size() - offset);
        //       send(socket_, data.data() + offset, chunkLen, 0);
        //   }

        (void)data;  // Suppress unused warning
        return TransportResult::Success;
    }

    void Shutdown() override {
        std::fwprintf(stderr, L"[StubTransport] Would disconnect\n");
        
        // TODO: Clean shutdown
        //   closesocket(socket_);
        //   WSACleanup();
    }

private:
    std::wstring serverAddress_;
    // SOCKET socket_ = INVALID_SOCKET;
};

}  // namespace ntbackup::transport
