#pragma once

#include <util/split.h>

#include <string>
#include <string_view>
#include <vector>

namespace Vcs {

class PathFilter {
public:
    PathFilter() = default;

    explicit PathFilter(const std::vector<std::string>& paths) {
        for (const auto& p : paths) {
            Append(p);
        }
    }

public:
    void Append(const std::string_view path) {
        const auto& parts = SplitPath(path);
        // Nothing to append
        if (parts.empty()) {
            return;
        }
        // Resize vector.
        patterns_.emplace_back();
        // Append path's parts.
        for (const auto& part : parts) {
            patterns_.back().emplace_back(part);
        }
    }

    bool Empty() const noexcept {
        return patterns_.empty();
    }

    bool Match(const std::string_view path) const {
        // Nothing to match.
        if (patterns_.empty() || path.empty()) {
            return true;
        }

        const auto& parts = SplitPath(path);

        for (const auto& p : patterns_) {
            const auto match_path_prefix = [&]() {
                if (p.size() > parts.size()) {
                    return false;
                }
                for (size_t i = 0, end = p.size(); i < end; ++i) {
                    if (p[i] != parts[i]) {
                        return false;
                    }
                }
                return true;
            };

            if (match_path_prefix()) {
                return true;
            }
        }

        return false;
    }

    bool IsParent(const std::string_view path) const {
        // Nothing to match.
        if (patterns_.empty() || path.empty()) {
           return true;
        }

        const auto& parts = SplitPath(path);

        for (const auto& p : patterns_) {
            const auto match_path_prefix = [&]() {
                for (size_t i = 0, end = std::min(p.size(), parts.size()); i < end; ++i) {
                    if (p[i] != parts[i]) {
                        return false;
                    }
                }
                return true;
            };

            if (match_path_prefix()) {
                return true;
            }
        }

        return false;
    }

private:
    std::vector<std::vector<std::string>> patterns_;
};

} // namespace Vcs
