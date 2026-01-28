// main.cpp
#include <cstdio>
#include <memory>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "core/BackupEngine.hpp"
#include "transport/StubTransport.hpp"

int wmain(int argc, wchar_t* argv[]) {
    using namespace ntbackup;

    std::wstring serverAddress = (argc > 1) ? argv[1] : L"192.168.1.100:9000";

    std::wprintf(L"[*] NTBackup - Raw NTFS Document Backup\n");
    std::wprintf(L"[*] Server: %ls\n", serverAddress.c_str());
    std::wprintf(L"[*] Extensions: .pdf, .docx\n\n");

    core::BackupConfig config{
        .targetExtensions = {L".pdf", L".docx"},
        .verbose = true,
        .maxFileSize = 100 * 1024 * 1024
    };

    auto transport = std::make_unique<transport::StubTransport>(serverAddress);
    core::BackupEngine engine(std::move(config), std::move(transport));

    auto stats = engine.Run([](const std::wstring& currentFile,
                               const core::BackupStats& stats) {
        if (stats.filesMatched % 50 == 0) {
            std::wprintf(L"\r[*] Scanned: %llu | Matched: %llu | Sent: %llu",
                        stats.filesScanned, stats.filesMatched, stats.filesSent);
        }
    });

    std::wprintf(L"\n\n[*] Complete!\n");
    std::wprintf(L"    Files scanned:     %llu\n", stats.filesScanned);
    std::wprintf(L"    Files matched:     %llu\n", stats.filesMatched);
    std::wprintf(L"    Files sent:        %llu\n", stats.filesSent);
    std::wprintf(L"    Files failed:      %llu\n", stats.filesFailed);
    std::wprintf(L"    Bytes transferred: %llu\n", stats.bytesTransferred);

    return 0;
}
