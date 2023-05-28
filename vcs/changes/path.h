#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Vcs {

class PathFilter {
public:
    PathFilter() noexcept = default;

    explicit PathFilter(const std::vector<std::string>& paths);

public:
    bool Empty() const noexcept;

    bool Match(const std::string_view path) const;

    bool IsParent(const std::string_view path) const;

private:
    void Append(const std::string_view path);

private:
    std::vector<std::string> patterns_;
};

/// Ensure PathFilter can be effectively assigned by move.
static_assert(std::is_nothrow_move_assignable_v<PathFilter>);
/// Ensure PathFilter can be effectively constructed by move.
static_assert(std::is_nothrow_move_constructible_v<PathFilter>);

} // namespace Vcs
