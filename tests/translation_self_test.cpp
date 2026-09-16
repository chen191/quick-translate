#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>

#include <iostream>

#include "../src/translation.h"
#include "../src/util.h"

int wmain() {
    SetConsoleOutputCP(CP_UTF8);
    const ULONGLONG started = GetTickCount64();
    const TranslationResult first =
        TranslateEnglishToChinese(L"The quick brown fox jumps over the lazy dog.");
    if (!first.ok) {
        std::cerr << "ERROR 1: " << WideToUtf8(first.error) << "\n";
        return 1;
    }
    const ULONGLONG afterFirst = GetTickCount64();
    const TranslationResult second =
        TranslateEnglishToChinese(L"A lightweight tool should stay out of the way.");
    if (!second.ok) {
        std::cerr << "ERROR 2: " << WideToUtf8(second.error) << "\n";
        return 1;
    }
    const ULONGLONG finished = GetTickCount64();
    std::cout << "OK 1: " << WideToUtf8(first.text) << "\n";
    std::cout << "OK 2: " << WideToUtf8(second.text) << "\n";
    std::cout << "TimingMs: first=" << (afterFirst - started)
              << " second=" << (finished - afterFirst) << "\n";
    return first.text.empty() || second.text.empty() ? 1 : 0;
}
