#pragma once

#include <string>

struct TranslationResult {
    bool ok = false;
    std::wstring text;
    std::wstring error;
};

TranslationResult TranslateEnglishToChinese(const std::wstring& source);
