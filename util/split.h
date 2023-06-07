#pragma once

#include <string_view>
#include <vector>

template <typename S, typename R = S>
std::vector<R> SplitString(const S& s, const typename S::value_type ch) {
    std::vector<R> parts;
    auto sp = s.begin();
    for (auto si = s.begin(); si != s.end(); ++si) {
        if (*si == ch) {
            if (sp < si) {
                parts.emplace_back(sp, si);
            }
            sp = si + 1;
        }
    }
    if (sp != s.end()) {
        parts.emplace_back(sp, s.end());
    }
    return parts;
}

inline std::vector<std::string_view> SplitPath(const std::string_view s, const char ch = '/') {
    return SplitString<std::string_view>(s, ch);
}
