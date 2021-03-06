#pragma once

#include <string>

namespace app::utility {

std::string wide_string_to_string(const std::wstring& wide_string);
void CopyToClipboard(const char *buffer, size_t length);

}
