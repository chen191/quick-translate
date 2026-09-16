#pragma once

#include <windows.h>

#include <string>

struct SelectionResult {
    bool ok = false;
    std::wstring text;
    std::wstring error;
};

SelectionResult CaptureSelectedText(HWND expectedForeground);
bool CopyTextToClipboard(HWND owner, const std::wstring& text);
bool LooksLikeEnglish(const std::wstring& text);
