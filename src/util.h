#pragma once

#include <windows.h>

#include <string>

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::wstring TrimText(std::wstring value);
std::string UrlEncodeUtf8(const std::wstring& value);
std::wstring Win32ErrorMessage(DWORD error);
std::wstring DecodeJsonString(const std::string& json, size_t quotePosition, size_t* endPosition = nullptr);
