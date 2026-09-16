#include <iostream>

#include "../src/selection.h"
#include "../src/util.h"

int main() {
    bool ok = true;
    ok = ok && LooksLikeEnglish(L"Select this English sentence.");
    ok = ok && LooksLikeEnglish(L"API 返回 error code 404");
    ok = ok && !LooksLikeEnglish(L"这是一段中文文本");
    ok = ok && TrimText(L" \r\n hello \t") == L"hello";
    ok = ok && UrlEncodeUtf8(L"Hello world!") == "Hello%20world%21";
    ok = ok && DecodeJsonString("\"\\u4F60\\u597D\\nworld\"", 0) == L"你好\nworld";
    std::cout << (ok ? "core_unit_test=OK\n" : "core_unit_test=FAILED\n");
    return ok ? 0 : 1;
}
