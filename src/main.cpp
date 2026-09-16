#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "selection.h"
#include "translation.h"
#include "util.h"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

namespace {
constexpr wchar_t kWindowClass[] = L"QuickTranslateCardWindow";
constexpr wchar_t kMutexName[] = L"Local\\QuickTranslateSingleInstance";
constexpr wchar_t kProductName[] = L"轻译";
constexpr int kCardWidth = 460;
constexpr int kCardHeight = 246;
constexpr int kHotkeyId = 1;
constexpr UINT kTrayId = 1;
constexpr UINT_PTR kHideTimer = 1;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTranslationDoneMessage = WM_APP + 2;
constexpr UINT kShowExistingMessage = WM_APP + 3;
constexpr UINT kShowCommand = 2002;
constexpr UINT kCopyCommand = 2003;
constexpr UINT kExitCommand = 2004;

struct TranslationPayload {
    std::wstring source;
    std::wstring translation;
    std::wstring error;
};

struct WorkItem {
    HWND targetWindow = nullptr;
    HWND sourceWindow = nullptr;
};

HWND g_window = nullptr;
HANDLE g_mutex = nullptr;
NOTIFYICONDATAW g_tray{};
HFONT g_titleFont = nullptr;
HFONT g_labelFont = nullptr;
HFONT g_sourceFont = nullptr;
HFONT g_resultFont = nullptr;
HFONT g_footerFont = nullptr;
std::atomic_bool g_busy{false};
std::atomic_bool g_shuttingDown{false};
std::wstring g_sourceText;
std::wstring g_resultText;
std::wstring g_footerText;
bool g_error = false;
bool g_hasTranslation = false;
bool g_loading = false;
UINT g_taskbarCreatedMessage = 0;
ULONGLONG g_hideAt = 0;

std::wstring ExecutableDirectory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const auto slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : full.substr(0, slash);
}

HFONT CreateUiFont(int points, int weight) {
    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void CreateFonts() {
    g_titleFont = CreateUiFont(11, FW_SEMIBOLD);
    g_labelFont = CreateUiFont(9, FW_SEMIBOLD);
    g_sourceFont = CreateUiFont(10, FW_NORMAL);
    g_resultFont = CreateUiFont(13, FW_MEDIUM);
    g_footerFont = CreateUiFont(9, FW_NORMAL);
}

void DeleteFonts() {
    for (HFONT font : {g_titleFont, g_labelFont, g_sourceFont, g_resultFont, g_footerFont}) {
        if (font) {
            DeleteObject(font);
        }
    }
}

RECT WorkAreaForPoint(POINT point) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(monitor, &info)) {
        return info.rcWork;
    }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

void PositionCardNear(POINT cursor) {
    const RECT work = WorkAreaForPoint(cursor);
    int x = cursor.x + 18;
    int y = cursor.y + 24;
    if (x + kCardWidth > work.right) {
        x = cursor.x - kCardWidth - 18;
    }
    if (y + kCardHeight > work.bottom) {
        y = cursor.y - kCardHeight - 22;
    }
    x = std::clamp(x, static_cast<int>(work.left + 8),
                   static_cast<int>(work.right - kCardWidth - 8));
    y = std::clamp(y, static_cast<int>(work.top + 8),
                   static_cast<int>(work.bottom - kCardHeight - 8));
    SetWindowPos(g_window, HWND_TOPMOST, x, y, kCardWidth, kCardHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void ShowCard(POINT point, UINT hideAfterMs) {
    PositionCardNear(point);
    InvalidateRect(g_window, nullptr, FALSE);
    KillTimer(g_window, kHideTimer);
    g_hideAt = 0;
    if (hideAfterMs > 0) {
        g_hideAt = GetTickCount64() + hideAfterMs;
        SetTimer(g_window, kHideTimer, hideAfterMs, nullptr);
    }
}

void ShowWelcome() {
    if (g_busy) {
        g_sourceText = L"翻译任务正在进行";
        g_resultText = L"请稍候…";
        g_footerText = L"完成后会自动显示结果";
        g_error = false;
        g_hasTranslation = false;
        g_loading = true;
        POINT cursor{};
        GetCursorPos(&cursor);
        ShowCard(cursor, 0);
        return;
    }
    g_sourceText = L"轻译已在后台运行";
    g_resultText = L"选中任意英文，然后按 Ctrl + E";
    g_footerText = L"全局热键已启用 · 右键托盘图标可退出";
    g_error = false;
    g_hasTranslation = false;
    g_loading = false;
    POINT cursor{};
    GetCursorPos(&cursor);
    ShowCard(cursor, 7000);
}

void PaintCard(HWND window) {
    PAINTSTRUCT paint{};
    HDC target = BeginPaint(window, &paint);
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, kCardWidth, kCardHeight);
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);

    RECT full{0, 0, kCardWidth, kCardHeight};
    HBRUSH background = CreateSolidBrush(RGB(250, 252, 253));
    FillRect(memory, &full, background);
    DeleteObject(background);

    HBRUSH topBand = CreateSolidBrush(RGB(239, 248, 247));
    RECT headerBackground{0, 0, kCardWidth, 48};
    FillRect(memory, &headerBackground, topBand);
    DeleteObject(topBand);

    HBRUSH accent = CreateSolidBrush(RGB(38, 166, 154));
    RECT accentBar{0, 0, 5, kCardHeight};
    FillRect(memory, &accentBar, accent);
    DeleteObject(accent);

    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, RGB(25, 55, 60));
    SelectObject(memory, g_titleFont);
    RECT titleRect{20, 14, 370, 39};
    DrawTextW(memory, L"轻译   EN → 中文", -1, &titleRect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    SetTextColor(memory, RGB(112, 132, 135));
    SelectObject(memory, g_sourceFont);
    RECT closeRect{420, 10, 448, 40};
    DrawTextW(memory, L"×", -1, &closeRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    SetTextColor(memory, RGB(98, 119, 122));
    SelectObject(memory, g_labelFont);
    RECT sourceLabel{20, 57, 80, 76};
    DrawTextW(memory, L"原文", -1, &sourceLabel, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SetTextColor(memory, RGB(76, 88, 91));
    SelectObject(memory, g_sourceFont);
    RECT sourceRect{70, 55, 438, 101};
    DrawTextW(memory, g_sourceText.c_str(), -1, &sourceRect,
              DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);

    HPEN divider = CreatePen(PS_SOLID, 1, RGB(224, 231, 232));
    HGDIOBJ oldPen = SelectObject(memory, divider);
    MoveToEx(memory, 20, 108, nullptr);
    LineTo(memory, 440, 108);
    SelectObject(memory, oldPen);
    DeleteObject(divider);

    SetTextColor(memory, g_error ? RGB(196, 74, 64) : RGB(38, 148, 138));
    SelectObject(memory, g_labelFont);
    RECT resultLabel{20, 119, 80, 140};
    DrawTextW(memory, g_error ? L"提示" : L"译文", -1, &resultLabel,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SetTextColor(memory, g_error ? RGB(154, 57, 50) : RGB(25, 43, 47));
    SelectObject(memory, g_resultFont);
    RECT resultRect{70, 116, 438, 204};
    DrawTextW(memory, g_resultText.c_str(), -1, &resultRect,
              DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);

    HBRUSH footerBackground = CreateSolidBrush(RGB(244, 247, 248));
    RECT footerBackgroundRect{5, 214, kCardWidth, kCardHeight};
    FillRect(memory, &footerBackgroundRect, footerBackground);
    DeleteObject(footerBackground);
    SetTextColor(memory, RGB(117, 132, 135));
    SelectObject(memory, g_footerFont);
    RECT footerRect{20, 218, 440, 242};
    DrawTextW(memory, g_footerText.c_str(), -1, &footerRect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    BitBlt(target, 0, 0, kCardWidth, kCardHeight, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(window, &paint);
}

void AddTrayIcon() {
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = kTrayId;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = kTrayMessage;
    g_tray.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
    wcscpy_s(g_tray.szTip, L"轻译 - Ctrl + E 翻译选中英文");
    Shell_NotifyIconW(NIM_ADD, &g_tray);
}

void RemoveTrayIcon() {
    if (g_tray.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &g_tray);
        g_tray = {};
    }
}

DWORD WINAPI TranslationWorker(void* parameter) {
    std::unique_ptr<WorkItem> work(static_cast<WorkItem*>(parameter));
    std::unique_ptr<TranslationPayload> payload(new TranslationPayload());

    SelectionResult selection = CaptureSelectedText(work->sourceWindow);
    if (!selection.ok) {
        payload->error = std::move(selection.error);
    } else {
        payload->source = std::move(selection.text);
        if (payload->source.size() > 1000) {
            payload->error = L"当前版本一次最多翻译 1000 个字符，请缩短选区。";
        } else if (!LooksLikeEnglish(payload->source)) {
            payload->error = L"所选内容不像英文，请重新选择包含英文的文本。";
        } else {
            TranslationResult translated = TranslateEnglishToChinese(payload->source);
            if (translated.ok) {
                payload->translation = std::move(translated.text);
            } else {
                payload->error = std::move(translated.error);
            }
        }
    }

    if (!g_shuttingDown && PostMessageW(work->targetWindow, kTranslationDoneMessage,
                                        0, reinterpret_cast<LPARAM>(payload.get()))) {
        payload.release();
    }
    return 0;
}

void StartTranslation(HWND sourceWindow) {
    if (g_busy.exchange(true)) {
        g_footerText = L"正在处理上一段文字，请稍候…";
        InvalidateRect(g_window, nullptr, FALSE);
        return;
    }

    g_sourceText = L"正在读取当前选区…";
    g_resultText = L"正在翻译，请稍候";
    g_footerText = L"不会保存选中文字或翻译历史";
    g_error = false;
    g_hasTranslation = false;
    g_loading = true;
    POINT cursor{};
    GetCursorPos(&cursor);
    ShowCard(cursor, 0);

    std::unique_ptr<WorkItem> work(new WorkItem{g_window, sourceWindow});
    HANDLE thread = CreateThread(nullptr, 0, TranslationWorker, work.get(), 0, nullptr);
    if (!thread) {
        g_busy = false;
        g_loading = false;
        g_error = true;
        g_resultText = L"无法启动翻译任务。";
        g_footerText = Win32ErrorMessage(GetLastError());
        InvalidateRect(g_window, nullptr, FALSE);
        g_hideAt = GetTickCount64() + 10000;
        SetTimer(g_window, kHideTimer, 10000, nullptr);
        return;
    }
    work.release();
    CloseHandle(thread);
}

void ShowContextMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kShowCommand, L"显示使用提示");
    AppendMenuW(menu, MF_STRING | (g_hasTranslation ? 0 : MF_GRAYED), kCopyCommand, L"复制当前译文");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitCommand, L"退出轻译");
    SetForegroundWindow(g_window);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        point.x, point.y, 0, g_window, nullptr);
    DestroyMenu(menu);
    PostMessageW(g_window, WM_NULL, 0, 0);

    if (command == kShowCommand) {
        ShowWelcome();
    } else if (command == kCopyCommand && g_hasTranslation) {
        if (CopyTextToClipboard(g_window, g_resultText)) {
            g_footerText = L"已复制译文";
            InvalidateRect(g_window, nullptr, FALSE);
        }
    } else if (command == kExitCommand) {
        DestroyWindow(g_window);
    }
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarCreatedMessage != 0 && message == g_taskbarCreatedMessage) {
        AddTrayIcon();
        return 0;
    }
    switch (message) {
        case WM_PAINT:
            PaintCard(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONUP: {
            const int x = static_cast<short>(LOWORD(lParam));
            const int y = static_cast<short>(HIWORD(lParam));
            if (x >= 410 && y <= 48) {
                ShowWindow(window, SW_HIDE);
            } else if (g_hasTranslation && y >= 108 && y <= 214) {
                if (CopyTextToClipboard(window, g_resultText)) {
                    g_footerText = L"已复制译文";
                    InvalidateRect(window, nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_COPY:
            if (g_hasTranslation && CopyTextToClipboard(window, g_resultText)) {
                g_footerText = L"已复制译文";
                InvalidateRect(window, nullptr, FALSE);
                return TRUE;
            }
            return FALSE;
        case WM_RBUTTONUP: {
            POINT point{};
            GetCursorPos(&point);
            ShowContextMenu(point);
            return 0;
        }
        case WM_HOTKEY:
            if (wParam == kHotkeyId) {
                StartTranslation(GetForegroundWindow());
            }
            return 0;
        case WM_TIMER:
            if (wParam == kHideTimer && !g_loading && g_hideAt != 0 &&
                GetTickCount64() >= g_hideAt) {
                KillTimer(window, kHideTimer);
                g_hideAt = 0;
                ShowWindow(window, SW_HIDE);
            }
            return 0;
        case kTrayMessage:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                POINT point{};
                GetCursorPos(&point);
                ShowContextMenu(point);
            } else if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
                ShowWelcome();
            }
            return 0;
        case kTranslationDoneMessage: {
            std::unique_ptr<TranslationPayload> payload(
                reinterpret_cast<TranslationPayload*>(lParam));
            g_busy = false;
            g_loading = false;
            if (!payload->error.empty()) {
                g_sourceText = payload->source.empty() ? L"未能读取选区" : payload->source;
                g_resultText = payload->error;
                g_footerText = L"请重新选择文字后按 Ctrl + E";
                g_error = true;
                g_hasTranslation = false;
            } else {
                g_sourceText = payload->source;
                g_resultText = payload->translation;
                g_footerText = L"单击译文即可复制 · 20 秒后自动隐藏";
                g_error = false;
                g_hasTranslation = true;
            }
            InvalidateRect(window, nullptr, FALSE);
            KillTimer(window, kHideTimer);
            g_hideAt = GetTickCount64() + 20000;
            SetTimer(window, kHideTimer, 20000, nullptr);
            return 0;
        }
        case kShowExistingMessage:
            ShowWelcome();
            return 0;
        case WM_DESTROY:
            g_shuttingDown = true;
            KillTimer(window, kHideTimer);
            UnregisterHotKey(window, kHotkeyId);
            RemoveTrayIcon();
            DeleteFonts();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

int RunSelfTest() {
    const std::wstring source = L"The quick brown fox jumps over the lazy dog.";
    const TranslationResult result = TranslateEnglishToChinese(source);
    const std::wstring output = result.ok ? L"OK\r\n" + result.text : L"ERROR\r\n" + result.error;
    const std::string utf8 = WideToUtf8(output);
    const std::wstring path = ExecutableDirectory() + L"\\self-test-result.txt";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 2;
    }
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
    return result.ok ? 0 : 1;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    SetProcessDPIAware();
    if (commandLine && std::wstring(commandLine).find(L"--self-test") != std::wstring::npos) {
        return RunSelfTest();
    }

    g_mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!g_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr)) {
            PostMessageW(existing, kShowExistingMessage, 0, 0);
        }
        if (g_mutex) {
            CloseHandle(g_mutex);
        }
        return 0;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) {
        CloseHandle(g_mutex);
        return 1;
    }

    CreateFonts();
    g_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kWindowClass, kProductName, WS_POPUP,
        0, 0, kCardWidth, kCardHeight, nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        DeleteFonts();
        CloseHandle(g_mutex);
        return 1;
    }
    SetWindowRgn(g_window, CreateRoundRectRgn(0, 0, kCardWidth + 1, kCardHeight + 1, 22, 22), TRUE);

    if (!RegisterHotKey(g_window, kHotkeyId, MOD_CONTROL | MOD_NOREPEAT, 'E')) {
        MessageBoxW(nullptr, L"无法注册 Ctrl + E，全局热键可能已被其他程序占用。",
                    kProductName, MB_ICONERROR);
        DestroyWindow(g_window);
        CloseHandle(g_mutex);
        return 1;
    }

    AddTrayIcon();
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    ShowWelcome();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    ReleaseMutex(g_mutex);
    CloseHandle(g_mutex);
    return static_cast<int>(message.wParam);
}
