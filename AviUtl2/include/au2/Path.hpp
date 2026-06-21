#pragma once

#include <string>
#include <string_view>

namespace au2 {

std::string wide_to_utf8(std::wstring_view text);
bool has_script_extension(std::wstring_view path);

} // namespace au2
