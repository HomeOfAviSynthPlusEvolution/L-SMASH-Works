#include "au2/Path.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace au2 {

std::string wide_to_utf8(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required_size <= 0) {
        throw std::runtime_error("Failed to convert UTF-16 path to UTF-8.");
    }

    std::string result(static_cast<std::size_t>(required_size), '\0');
    const int converted_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        required_size,
        nullptr,
        nullptr);
    if (converted_size != required_size) {
        throw std::runtime_error("Failed to convert complete UTF-16 path to UTF-8.");
    }

    return result;
}

bool has_script_extension(std::wstring_view path)
{
    const auto slash = path.find_last_of(L"/\\");
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring_view::npos || (slash != std::wstring_view::npos && dot < slash)) {
        return false;
    }

    std::wstring extension(path.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });

    return extension == L".avs" || extension == L".avsi" || extension == L".vpy";
}

} // namespace au2
