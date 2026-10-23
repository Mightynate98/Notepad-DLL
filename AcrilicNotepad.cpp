// AcrilicNotepad.dll - targeted prototype for classic Windows 10 22H2 Notepad.
// Build x64. This is source code; it has not been compiled or tested on Windows here.
// Do not load this DLL into unrelated processes.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace {
enum AccentState {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

struct AccentPolicy {
    int state;
    int flags;
    int gradientColor; // AABBGGRR for SetWindowCompositionAttribute
    int animationId;
};

struct WindowCompositionAttributeData {
    int attribute;
    PVOID data;
    SIZE_T size;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*);
constexpr int WCA_ACCENT_POLICY = 19;
constexpr UINT_PTR SUBCLASS_ID = 0x4E41504C;

HMODULE g_module = nullptr;
HICON g_customIcon = nullptr;
HWND g_mainWindow = nullptr;
HWND g_editWindow = nullptr;
bool g_dark = false;
int g_effect = 2;
COLORREF g_textColor = RGB(32, 32, 32);
COLORREF g_backgroundColor = RGB(255, 255, 255);

std::wstring ModuleDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}

std::wstring ConfigPath() {
    return ModuleDir() + L"\\config.ini";
}

int ReadInt(const wchar_t* section, const wchar_t* key, int fallback) {
    wchar_t value[32]{};
    GetPrivateProfileStringW(section, key, L"", value, ARRAYSIZE(value), ConfigPath().c_str());
    return value[0] ? _wtoi(value) : fallback;
}

bool ReadBool(const wchar_t* section, const wchar_t* key, bool fallback) {
    wchar_t value[32]{};
    GetPrivateProfileStringW(section, key, fallback ? L"true" : L"false",
        value, ARRAYSIZE(value), ConfigPath().c_str());
    return _wcsicmp(value, L"true") == 0 || wcscmp(value, L"1") == 0;
}

bool IsSystemDark() {
    DWORD value = 1;
    DWORD bytes = sizeof(value);
    const LSTATUS result = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &bytes);
    return result == ERROR_SUCCESS && value == 0;
}

COLORREF ReadRgb(const wchar_t* section, const wchar_t* key, int fallback) {
    int value = ReadInt(section, key, fallback);
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    return static_cast<COLORREF>(value);
}

int ReadAlpha(const wchar_t* section, int fallback) {
    int value = ReadInt(section, L"a", fallback);
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    return value;
}

DWORD MakeAccentColor(bool dark) {
    const wchar_t* section = dark ? L"dark" : L"light";
    const int fallback = dark ? 0 : 255;
    const BYTE r = static_cast<BYTE>(ReadRgb(section, L"r", fallback));
    const BYTE g = static_cast<BYTE>(ReadRgb(section, L"g", fallback));
    const BYTE b = static_cast<BYTE>(ReadRgb(section, L"b", fallback));
    const BYTE a = static_cast<BYTE>(ReadAlpha(section, dark ? 50 : 200));
    return (static_cast<DWORD>(a) << 24) |
           (static_cast<DWORD>(b) << 16) |
           (static_cast<DWORD>(g) << 8) |
           static_cast<DWORD>(r);
}

void RefreshThemeColors() {
    g_dark = IsSystemDark();
    g_textColor = g_dark ? RGB(232, 232, 232) : RGB(32, 32, 32);
    g_backgroundColor = g_dark ? RGB(32, 32, 32) : RGB(255, 255, 255);
    g_effect = ReadInt(L"config", L"effect", 2);
    if (g_effect < 1 || g_effect > 3) g_effect = 2;
}

void ApplyAccent(HWND hwnd) {
    if (!IsWindow(hwnd)) return;

    using SetDwmAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto dwmSetAttribute = reinterpret_cast<SetDwmAttributeFn>(
        GetProcAddress(GetModuleHandleW(L"dwmapi.dll"), "DwmSetWindowAttribute"));
    if (dwmSetAttribute) {
        // These attributes are undocumented/version-dependent on Windows 10; failure is okay.
        BOOL dark = g_dark ? TRUE : FALSE;
        dwmSetAttribute(hwnd, 19, &dark, sizeof(dark));
        dwmSetAttribute(hwnd, 20, &dark, sizeof(dark));
    }

    auto setComposition = reinterpret_cast<SetWindowCompositionAttributeFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    if (!setComposition) return;

    AccentPolicy policy{};
    switch (g_effect) {
        case 1: policy.state = ACCENT_ENABLE_TRANSPARENTGRADIENT; break; // clear
        case 2: policy.state = ACCENT_ENABLE_BLURBEHIND; break;          // blur
        case 3: policy.state = ACCENT_ENABLE_ACRYLICBLURBEHIND; break;   // acrylic
        default: policy.state = ACCENT_ENABLE_BLURBEHIND; break;
    }
    policy.flags = 2;
    policy.gradientColor = static_cast<int>(MakeAccentColor(g_dark));

    WindowCompositionAttributeData data{};
    data.attribute = WCA_ACCENT_POLICY;
    data.data = &policy;
    data.size = sizeof(policy);
    setComposition(hwnd, &data);
}

void ApplyIcon(HWND hwnd) {
    const std::wstring path = ModuleDir() + L"\\icon\\notepad.ico";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    if (!g_customIcon) {
        g_customIcon = static_cast<HICON>(LoadImageW(
            nullptr, path.c_str(), IMAGE_ICON, 0, 0,
            LR_LOADFROMFILE | LR_DEFAULTSIZE));
    }
    if (g_customIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_customIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_customIcon));
    }
}

LRESULT CALLBACK MainSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                  LPARAM lParam, UINT_PTR subclassId, DWORD_PTR) {
    switch (message) {
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, g_textColor);
            SetBkMode(dc, TRANSPARENT);
            // A hollow brush can allow the parent backdrop to show through on some controls.
            // Classic EDIT controls may still paint an opaque background; this is not guaranteed.
            return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            RefreshThemeColors();
            ApplyAccent(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            if (g_editWindow) InvalidateRect(g_editWindow, nullptr, TRUE);
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, MainSubclassProc, subclassId);
            if (g_mainWindow == hwnd) g_mainWindow = nullptr;
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void ConfigureNotepadWindow(HWND hwnd) {
    if (!IsWindow(hwnd)) return;
    g_mainWindow = hwnd;
    RefreshThemeColors();
    ApplyAccent(hwnd);
    ApplyIcon(hwnd);

    if (!GetWindowSubclass(hwnd, MainSubclassProc, SUBCLASS_ID, nullptr)) {
        SetWindowSubclass(hwnd, MainSubclassProc, SUBCLASS_ID, 0);
    }

    // Classic Notepad uses a standard EDIT control on the Windows 10 22H2 implementation.
    // If no standard EDIT child exists, don't guess or subclass an unrelated child.
    HWND edit = FindWindowExW(hwnd, nullptr, L"Edit", nullptr);
    if (edit) {
        g_editWindow = edit;
        // This removes themed styling from this control where supported; it is not a custom
        // scrollbar renderer and is intentionally not presented as one.
        SetWindowTheme(edit, L"", L"");
        InvalidateRect(edit, nullptr, TRUE);
    }
}

DWORD WINAPI Worker(void*) {
    // The DLL is loaded only into the explicitly launched classic Notepad process.
    for (int i = 0; i < 100 && !g_mainWindow; ++i) {
        HWND hwnd = FindWindowW(L"Notepad", nullptr);
        if (hwnd && GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId()) {
            ConfigureNotepadWindow(hwnd);
            break;
        }
        // FindWindow's owning process is checked below by enumerating current-process windows.
        struct FindData { DWORD pid; HWND hwnd; } data{ GetCurrentProcessId(), nullptr };
        EnumWindows([](HWND candidate, LPARAM parameter) -> BOOL {
            auto* find = reinterpret_cast<FindData*>(parameter);
            DWORD pid = 0;
            GetWindowThreadProcessId(candidate, &pid);
            wchar_t cls[64]{};
            if (pid == find->pid && GetClassNameW(candidate, cls, ARRAYSIZE(cls)) &&
                wcscmp(cls, L"Notepad") == 0) {
                find->hwnd = candidate;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&data));
        if (data.hwnd) {
            ConfigureNotepadWindow(data.hwnd);
            break;
        }
        Sleep(100);
    }

    while (g_mainWindow && IsWindow(g_mainWindow)) {
        Sleep(1500);
        RefreshThemeColors();
        ApplyAccent(g_mainWindow);
        ApplyIcon(g_mainWindow);
    }
    return 0;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_customIcon) {
            DestroyIcon(g_customIcon);
            g_customIcon = nullptr;
        }
    }
    return TRUE;
}
