#include "util.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <iomanip>
#include <sstream>

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                        length, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring TrimText(std::wstring value) {
    auto isWhitespace = [](wchar_t ch) {
        return std::iswspace(ch) || ch == 0x00A0 || ch == 0x200B;
    };
    const auto first = std::find_if_not(value.begin(), value.end(), isWhitespace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isWhitespace).base();
    value = std::wstring(first, last);
    value.erase(std::remove(value.begin(), value.end(), L'\0'), value.end());
    return value;
}

std::string UrlEncodeUtf8(const std::wstring& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    const std::string utf8 = WideToUtf8(value);
    std::string encoded;
    encoded.reserve(utf8.size() * 3);
    for (const unsigned char ch : utf8) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[(ch >> 4) & 0x0F]);
            encoded.push_back(hex[ch & 0x0F]);
        }
    }
    return encoded;
}

std::wstring Win32ErrorMessage(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = length && buffer ? std::wstring(buffer, length) : L"未知错误";
    if (buffer) {
        LocalFree(buffer);
    }
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) {
        result.pop_back();
    }
    return result;
}

namespace {
void AppendCodePointUtf8(std::string& output, unsigned int codePoint) {
    if (codePoint <= 0x7F) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

bool ParseHex4(const std::string& value, size_t position, unsigned int& output) {
    if (position + 4 > value.size()) {
        return false;
    }
    output = 0;
    for (size_t index = 0; index < 4; ++index) {
        const char ch = value[position + index];
        output <<= 4;
        if (ch >= '0' && ch <= '9') {
            output |= static_cast<unsigned int>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            output |= static_cast<unsigned int>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            output |= static_cast<unsigned int>(ch - 'A' + 10);
        } else {
            return false;
        }
    }
    return true;
}
}  // namespace

std::wstring DecodeJsonString(const std::string& json, size_t quotePosition, size_t* endPosition) {
    if (quotePosition >= json.size() || json[quotePosition] != '"') {
        return {};
    }
    std::string decoded;
    for (size_t index = quotePosition + 1; index < json.size(); ++index) {
        const char ch = json[index];
        if (ch == '"') {
            if (endPosition) {
                *endPosition = index + 1;
            }
            return Utf8ToWide(decoded);
        }
        if (ch != '\\') {
            decoded.push_back(ch);
            continue;
        }
        if (++index >= json.size()) {
            return {};
        }
        switch (json[index]) {
            case '"': decoded.push_back('"'); break;
            case '\\': decoded.push_back('\\'); break;
            case '/': decoded.push_back('/'); break;
            case 'b': decoded.push_back('\b'); break;
            case 'f': decoded.push_back('\f'); break;
            case 'n': decoded.push_back('\n'); break;
            case 'r': decoded.push_back('\r'); break;
            case 't': decoded.push_back('\t'); break;
            case 'u': {
                unsigned int codePoint = 0;
                if (!ParseHex4(json, index + 1, codePoint)) {
                    return {};
                }
                index += 4;
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF && index + 6 < json.size() &&
                    json[index + 1] == '\\' && json[index + 2] == 'u') {
                    unsigned int low = 0;
                    if (ParseHex4(json, index + 3, low) && low >= 0xDC00 && low <= 0xDFFF) {
                        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                        index += 6;
                    }
                }
                AppendCodePointUtf8(decoded, codePoint);
                break;
            }
            default:
                return {};
        }
    }
    return {};
}
