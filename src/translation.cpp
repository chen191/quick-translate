#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "translation.h"

#include "util.h"

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <string>
#include <utility>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace {
constexpr size_t kMaximumResponseBytes = 2 * 1024 * 1024;

class InternetHandle {
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET value) : value_(value) {}
    ~InternetHandle() {
        if (value_) {
            WinHttpCloseHandle(value_);
        }
    }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    InternetHandle(InternetHandle&& other) noexcept : value_(other.value_) {
        other.value_ = nullptr;
    }
    InternetHandle& operator=(InternetHandle&& other) noexcept {
        if (this != &other) {
            if (value_) {
                WinHttpCloseHandle(value_);
            }
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    void reset(HINTERNET value = nullptr) {
        if (value_) {
            WinHttpCloseHandle(value_);
        }
        value_ = value;
    }
    HINTERNET get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }

private:
    HINTERNET value_ = nullptr;
};

struct HttpResponse {
    DWORD status = 0;
    std::string body;
    std::wstring error;
};

struct BingAuth {
    std::string ig;
    std::string iid;
    std::string key;
    std::string token;
    ULONGLONG expiresAt = 0;
    unsigned int requestCount = 1;

    bool Valid() const {
        return !ig.empty() && !iid.empty() && !key.empty() && !token.empty() &&
               GetTickCount64() < expiresAt;
    }

    void Clear() {
        *this = {};
    }
};

BingAuth g_auth;
InternetHandle g_session;
InternetHandle g_connection;

HttpResponse SendRequest(HINTERNET connection,
                         const wchar_t* method,
                         const std::wstring& path,
                         const std::wstring& headers,
                         const std::string& body) {
    HttpResponse response;
    InternetHandle request(WinHttpOpenRequest(
        connection, method, path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) {
        response.error = L"创建网络请求失败：" + Win32ErrorMessage(GetLastError());
        return response;
    }

    const wchar_t* headerPointer = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
    const DWORD headerLength = headers.empty() ? 0 : static_cast<DWORD>(headers.size());
    void* bodyPointer = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    if (!WinHttpSendRequest(request.get(), headerPointer, headerLength, bodyPointer,
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
        response.error = L"发送网络请求失败：" + Win32ErrorMessage(GetLastError());
        return response;
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        response.error = L"接收翻译响应失败：" + Win32ErrorMessage(GetLastError());
        return response;
    }

    DWORD statusBytes = sizeof(response.status);
    WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &statusBytes,
                        WINHTTP_NO_HEADER_INDEX);
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            response.error = L"读取翻译响应失败：" + Win32ErrorMessage(GetLastError());
            return response;
        }
        if (available == 0) {
            break;
        }
        const size_t oldSize = response.body.size();
        if (oldSize + available > kMaximumResponseBytes) {
            response.error = L"翻译服务响应超过 2 MB，已停止读取。";
            return response;
        }
        response.body.resize(oldSize + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), response.body.data() + oldSize, available, &read)) {
            response.error = L"读取翻译数据失败：" + Win32ErrorMessage(GetLastError());
            return response;
        }
        response.body.resize(oldSize + read);
    }
    return response;
}

std::string ExtractBetween(const std::string& text, const std::string& marker) {
    const size_t start = text.find(marker);
    if (start == std::string::npos) {
        return {};
    }
    const size_t valueStart = start + marker.size();
    const size_t end = text.find('"', valueStart);
    return end == std::string::npos ? std::string{} : text.substr(valueStart, end - valueStart);
}

bool ParseAuthPage(const HttpResponse& page, BingAuth& auth, std::wstring& error) {
    if (page.status != 200) {
        error = L"翻译服务初始化失败，HTTP 状态码：" + std::to_wstring(page.status);
        return false;
    }

    auth.ig = ExtractBetween(page.body, "IG:\"");

    size_t iidPosition = page.body.find("id=\"rich_tta\"");
    if (iidPosition != std::string::npos) {
        iidPosition = page.body.find("data-iid=\"", iidPosition);
    }
    if (iidPosition == std::string::npos) {
        iidPosition = page.body.find("data-iid=\"translator.");
    }
    if (iidPosition != std::string::npos) {
        const size_t valueStart = page.body.find('"', iidPosition) + 1;
        const size_t valueEnd = page.body.find('"', valueStart);
        if (valueStart > 0 && valueEnd != std::string::npos) {
            auth.iid = page.body.substr(valueStart, valueEnd - valueStart);
        }
    }

    const std::string marker = "params_AbusePreventionHelper";
    size_t helper = page.body.find(marker);
    if (helper != std::string::npos) {
        helper = page.body.find('[', helper);
    }
    if (helper != std::string::npos) {
        size_t cursor = helper + 1;
        while (cursor < page.body.size() && std::isspace(static_cast<unsigned char>(page.body[cursor]))) {
            ++cursor;
        }
        const size_t comma = page.body.find(',', cursor);
        if (comma != std::string::npos) {
            auth.key = page.body.substr(cursor, comma - cursor);
            const size_t tokenQuote = page.body.find('"', comma + 1);
            if (tokenQuote != std::string::npos) {
                const size_t tokenEnd = page.body.find('"', tokenQuote + 1);
                if (tokenEnd != std::string::npos) {
                    auth.token = page.body.substr(tokenQuote + 1, tokenEnd - tokenQuote - 1);
                }
            }
        }
    }

    auth.expiresAt = GetTickCount64() + 50ULL * 60ULL * 1000ULL;
    auth.requestCount = 1;

    if (!auth.Valid()) {
        error = L"翻译服务页面结构发生变化，暂时无法取得访问参数。";
        auth.Clear();
        return false;
    }
    return true;
}

bool RefreshAuth(HINTERNET connection, std::wstring& error) {
    const std::wstring headers =
        L"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
        L"Cache-Control: no-cache\r\n";
    const HttpResponse page = SendRequest(connection, L"GET", L"/translator?mkt=zh-CN", headers, {});
    if (!page.error.empty()) {
        error = page.error;
        return false;
    }
    BingAuth fresh;
    if (!ParseAuthPage(page, fresh, error)) {
        return false;
    }
    g_auth = std::move(fresh);
    return true;
}

std::wstring ExtractTranslation(const std::string& json) {
    size_t position = json.find("\"translations\"");
    if (position == std::string::npos) {
        return {};
    }
    position = json.find("\"text\"", position);
    if (position == std::string::npos) {
        return {};
    }
    position = json.find(':', position);
    if (position == std::string::npos) {
        return {};
    }
    position = json.find('"', position);
    if (position == std::string::npos) {
        return {};
    }
    return DecodeJsonString(json, position);
}

TranslationResult PerformTranslation(HINTERNET connection, const std::wstring& source,
                                     bool& shouldRefreshAuth) {
    TranslationResult result;
    shouldRefreshAuth = false;
    const std::string form =
        "fromLang=auto-detect&text=" + UrlEncodeUtf8(source) +
        "&to=zh-Hans&tryFetchingGenderDebiasedTranslations=true&token=" +
        UrlEncodeUtf8(Utf8ToWide(g_auth.token)) + "&key=" + g_auth.key;

    const std::string pathUtf8 =
        "/ttranslatev3?isVertical=1&IG=" + g_auth.ig + "&IID=" + g_auth.iid + "." +
        std::to_string(g_auth.requestCount++);
    std::wstring headers =
        L"Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n"
        L"Referer: https://www.bing.com/translator\r\n"
        L"Origin: https://www.bing.com\r\n"
        L"X-Requested-With: XMLHttpRequest\r\n"
        L"Accept: application/json, text/plain, */*\r\n";

    const HttpResponse response = SendRequest(connection, L"POST", Utf8ToWide(pathUtf8), headers, form);
    if (!response.error.empty()) {
        result.error = response.error;
        return result;
    }
    if (response.status != 200) {
        shouldRefreshAuth = response.status == 401 || response.status == 403;
        result.error = L"翻译服务返回 HTTP " + std::to_wstring(response.status) + L"。";
        return result;
    }
    result.text = TrimText(ExtractTranslation(response.body));
    if (result.text.empty()) {
        shouldRefreshAuth = true;
        result.error = L"翻译服务没有返回可读取的中文结果。";
        return result;
    }
    result.ok = true;
    return result;
}

bool EnsureClient(std::wstring& error) {
    if (!g_session) {
        g_session.reset(WinHttpOpen(
            L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            L"(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!g_session) {
            error = L"无法初始化网络组件：" + Win32ErrorMessage(GetLastError());
            return false;
        }
        WinHttpSetTimeouts(g_session.get(), 5000, 5000, 10000, 15000);
    }
    if (!g_connection) {
        g_connection.reset(WinHttpConnect(g_session.get(), L"www.bing.com",
                                          INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!g_connection) {
            error = L"无法连接翻译服务：" + Win32ErrorMessage(GetLastError());
            return false;
        }
    }
    return true;
}
}  // namespace

TranslationResult TranslateEnglishToChinese(const std::wstring& source) {
    TranslationResult result;
    std::wstring clientError;
    if (!EnsureClient(clientError)) {
        result.error = std::move(clientError);
        return result;
    }

    std::wstring authError;
    if (!g_auth.Valid() && !RefreshAuth(g_connection.get(), authError)) {
        result.error = std::move(authError);
        return result;
    }

    bool shouldRefreshAuth = false;
    result = PerformTranslation(g_connection.get(), source, shouldRefreshAuth);
    if (!result.ok && shouldRefreshAuth) {
        g_auth.Clear();
        if (RefreshAuth(g_connection.get(), authError)) {
            result = PerformTranslation(g_connection.get(), source, shouldRefreshAuth);
        }
    }
    return result;
}
