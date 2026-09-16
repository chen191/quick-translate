#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>

#include <iostream>
#include <string>

namespace {
constexpr wchar_t kTestClass[] = L"QuickTranslateHotkeyTestWindow";
constexpr wchar_t kTranslatorClass[] = L"QuickTranslateCardWindow";
HWND g_edit = nullptr;
int g_phase = 0;
int g_exitCode = 1;
int g_copyAttempts = 0;

std::wstring ReadClipboard() {
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (OpenClipboard(nullptr)) {
            break;
        }
        Sleep(20);
    }
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        CloseClipboard();
        return {};
    }
    std::wstring result;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
            result = text;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return result;
}

bool ContainsCjk(const std::wstring& text) {
    for (const wchar_t ch : text) {
        if (ch >= 0x3400 && ch <= 0x9FFF) {
            return true;
        }
    }
    return false;
}

void PressCtrlE() {
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'E';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'E';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

bool ForceForeground(HWND window) {
    HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground
        ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD currentThread = GetCurrentThreadId();
    const bool attached = foregroundThread != 0 && foregroundThread != currentThread &&
                          AttachThreadInput(currentThread, foregroundThread, TRUE);
    BringWindowToTop(window);
    SetActiveWindow(window);
    SetForegroundWindow(window);
    SetFocus(g_edit);
    if (attached) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
    return GetForegroundWindow() == window;
}

LRESULT CALLBACK TestProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                L"A lightweight tool should stay out of the way.",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                12, 12, 500, 30, window, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_edit, EM_SETSEL, 0, -1);
            SetFocus(g_edit);
            SetTimer(window, 1, 700, nullptr);
            return 0;
        case WM_TIMER:
            if (g_phase == 0) {
                g_phase = 1;
                KillTimer(window, 1);
                if (!ForceForeground(window)) {
                    std::cout << "failed_to_focus_test_window\n";
                    g_exitCode = 2;
                    DestroyWindow(window);
                    return 0;
                }
                SendMessageW(g_edit, EM_SETSEL, 0, -1);
                PressCtrlE();
                SetTimer(window, 1, 500, nullptr);
            } else if (g_phase == 1) {
                KillTimer(window, 1);
                if (HWND translator = FindWindowW(kTranslatorClass, nullptr)) {
                    SendMessageW(translator, WM_COPY, 0, 0);
                }
                const std::wstring clipboard = ReadClipboard();
                if (ContainsCjk(clipboard)) {
                    g_exitCode = 0;
                    std::cout << "hotkey_integration_test=OK attempts="
                              << (g_copyAttempts + 1) << "\n";
                    DestroyWindow(window);
                } else if (++g_copyAttempts >= 30) {
                    std::cout << "hotkey_integration_test=FAILED timeout\n";
                    DestroyWindow(window);
                } else {
                    SetTimer(window, 1, 500, nullptr);
                }
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}
}  // namespace

int wmain() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = TestProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kTestClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&windowClass)) {
        return 2;
    }
    HWND window = CreateWindowExW(WS_EX_TOPMOST, kTestClass, L"Quick Translate Integration Test",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                  80, 80, 540, 90, nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 2;
    }
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return g_exitCode;
}
