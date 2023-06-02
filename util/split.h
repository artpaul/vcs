#pragma once

#include <vector>
#include <string_view>

std::vector<std::string_view> SplitPath(const std::string_view path, const char ch = '/');
