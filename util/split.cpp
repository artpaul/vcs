#include "split.h"

std::vector<std::string_view> SplitPath(const std::string_view s, const char ch) {
    std::vector<std::string_view> parts;
    size_t p = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == ch) {
            if (p < i) {
                parts.push_back(s.substr(p, i - p));
            }
            p = i + 1;
        }
    }
    if (p != s.size()) {
        parts.push_back(s.substr(p));
    }
    return parts;
}
