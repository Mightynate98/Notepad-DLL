// AcrilicNotepadLauncher.exe
// Starts only classic %WINDIR%\System32\notepad.exe and loads AcrilicNotepad.dll into that process.
// Build x64. This is a targeted launcher, not a system-wide injector.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <iostream>

static std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    return a.back() == L'\\' ? a + b : a + L"\\" + b;
}

int wmain() {
    wchar_t root[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, root);
    // Launcher is expected to live in C:\AcrilicNotepad.
    wchar_t launcherPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, launcherPath, MAX_PATH);
    std::wstring folder(launcherPath);
    const size_t slash = folder.find_last_of(L"\\/");
    if (slash != std::wstring::npos) folder.resize(slash);

    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring notepad = Join(Join(windows, L"System32"), L"notepad.exe");
    const std::wstring dll = Join(folder, L"AcrilicNotepad.dll");

    if (GetFileAttributesW(notepad.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"Classic Windows Notepad was not found.", L"AcrilicNotepad", MB_ICONERROR);
        return 1;
    }
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"AcrilicNotepad.dll was not found. Build the project first.",
                    L"AcrilicNotepad", MB_ICONERROR);
        return 1;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring command = L"\"" + notepad + L"\"";
    if (!CreateProcessW(notepad.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, folder.c_str(), &si, &pi)) {
        MessageBoxW(nullptr, L"Could not start classic Notepad.", L"AcrilicNotepad", MB_ICONERROR);
        return 1;
    }

    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(pi.hProcess, nullptr, bytes,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool injected = false;
    if (remotePath && WriteProcessMemory(pi.hProcess, remotePath, dll.c_str(), bytes, nullptr)) {
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        if (loadLibrary) {
            HANDLE remoteThread = CreateRemoteThread(pi.hProcess, nullptr, 0, loadLibrary,
                                                      remotePath, 0, nullptr);
            if (remoteThread) {
                WaitForSingleObject(remoteThread, 10000);
                DWORD exitCode = 0;
                if (GetExitCodeThread(remoteThread, &exitCode) && exitCode != 0) injected = true;
                CloseHandle(remoteThread);
            }
        }
    }
    if (remotePath) VirtualFreeEx(pi.hProcess, remotePath, 0, MEM_RELEASE);

    // Resume even if injection fails; never leave a suspended Notepad process behind.
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!injected) {
        MessageBoxW(nullptr,
            L"The DLL could not be loaded. Notepad will still open without the AcrilicNotepad effects.",
            L"AcrilicNotepad", MB_ICONWARNING);
    }
    return 0;
}
