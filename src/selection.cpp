#include "selection.h"

#include "util.h"

#include <algorithm>
#include <cwctype>
#include <utility>

namespace {
bool OpenClipboardWithRetry(HWND owner) {
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (OpenClipboard(owner)) {
            return true;
        }
        Sleep(20);
    }
    return false;
}

std::wstring ReadClipboardText() {
    if (!OpenClipboardWithRetry(nullptr)) {
        return {};
    }

    std::wstring result;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
                result = text;
                GlobalUnlock(handle);
            }
        }
    } else if (IsClipboardFormatAvailable(CF_TEXT)) {
        if (HANDLE handle = GetClipboardData(CF_TEXT)) {
            if (const char* text = static_cast<const char*>(GlobalLock(handle))) {
                const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
                if (length > 1) {
                    result.resize(static_cast<size_t>(length));
                    MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), length);
                    result.pop_back();
                }
                GlobalUnlock(handle);
            }
        }
    }
    CloseClipboard();
    return TrimText(std::move(result));
}

bool SendCopyShortcut() {
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'C';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'C';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(4, inputs, sizeof(INPUT)) == 4;
}
}  // namespace

SelectionResult CaptureSelectedText(HWND expectedForeground) {
    SelectionResult result;

    for (int attempt = 0; attempt < 30; ++attempt) {
        const bool controlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool eDown = (GetAsyncKeyState('E') & 0x8000) != 0;
        if (!controlDown && !eDown) {
            break;
        }
        Sleep(20);
    }
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetAsyncKeyState('E') & 0x8000) != 0) {
        result.error = L"请松开 Ctrl 和 E 后再试。";
        return result;
    }

    if (expectedForeground && GetForegroundWindow() != expectedForeground) {
        result.error = L"触发后焦点窗口发生了变化，请重新选中文字再按 Ctrl + E。";
        return result;
    }

    const DWORD sequenceBefore = GetClipboardSequenceNumber();
    if (!SendCopyShortcut()) {
        result.error = L"无法向当前程序发送复制命令。若目标程序以管理员身份运行，请同样以管理员身份运行轻译。";
        return result;
    }

    bool clipboardChanged = false;
    for (int attempt = 0; attempt < 75; ++attempt) {
        if (GetClipboardSequenceNumber() != sequenceBefore) {
            clipboardChanged = true;
            break;
        }
        Sleep(20);
    }
    if (!clipboardChanged) {
        result.error = L"没有读取到新的选区。请先用鼠标选中英文，再按 Ctrl + E。";
        return result;
    }

    result.text = ReadClipboardText();
    if (result.text.empty()) {
        result.error = L"选中的内容不是可读取的文本。";
        return result;
    }
    result.ok = true;
    return result;
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty() || !OpenClipboardWithRetry(owner)) {
        return false;
    }
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CopyMemory(destination, text.c_str(), bytes);
    GlobalUnlock(memory);
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

bool LooksLikeEnglish(const std::wstring& text) {
    size_t latinLetters = 0;
    size_t cjkCharacters = 0;
    for (const wchar_t ch : text) {
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z')) {
            ++latinLetters;
        } else if ((ch >= 0x3400 && ch <= 0x9FFF) || (ch >= 0xF900 && ch <= 0xFAFF)) {
            ++cjkCharacters;
        }
    }
    return latinLetters > 0 && latinLetters >= cjkCharacters;
}
