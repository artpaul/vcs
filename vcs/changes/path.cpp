#include "path.h"

#include <util/split.h>

#include <contrib/fmt/fmt/format.h>

#include <algorithm>
#include <cassert>

namespace Vcs {

PathFilter::PathFilter(const std::vector<std::string>& paths) {
    patterns_.reserve(paths.size());

    for (const auto& p : paths) {
        Append(p);
    }

    std::sort(patterns_.begin(), patterns_.end());
}

bool PathFilter::Empty() const noexcept {
    return patterns_.empty();
}

bool PathFilter::Match(const std::string_view path) const {
    assert(path.empty() || path[0] != '/');

    // Nothing to match.
    if (patterns_.empty() || path.empty()) {
        return true;
    }

    for (const auto& p : patterns_) {
        if (p[0] > path[0]) {
            break;
        }
        if (p[0] < path[0] || p.size() > path.size()) {
            continue;
        }
        if (p.size() == path.size() || path[p.size()] == '/') {
            if (path.starts_with(p)) {
                return true;
            }
        }
    }

    return false;
}

bool PathFilter::IsParent(const std::string_view path) const {
    assert(path.empty() || path[0] != '/');

    // Nothing to match.
    if (patterns_.empty() || path.empty()) {
        return true;
    }

    for (const auto& p : patterns_) {
        if (p[0] > path[0]) {
            break;
        }
        if (p[0] < path[0]) {
            continue;
        }
        if (p.size() > path.size()) {
            if (p[path.size()] == '/' && p.starts_with(path)) {
                return true;
            }
        } else {
            if (p.size() == path.size() || path[p.size()] == '/') {
                if (path.starts_with(p)) {
                    return true;
                }
            }
        }
    }

    return false;
}

void PathFilter::Append(const std::string_view path) {
    // Reconstruct path to ensure it has canonized form.
    const auto parts = SplitPath(path);
    // Nothing to append.
    if (parts.empty()) {
        return;
    }
    // Append path.
    patterns_.push_back(fmt::to_string(fmt::join(parts, "/")));
}

} // namespace Vcs
