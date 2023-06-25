#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Vcs {

struct Rule {
    /// Match directories only.
    static constexpr int kDirectory = 0x01;
    /// Match full path instead of filename.
    static constexpr int kFullPath = 0x02;
    /// Rule has wildcard symbols.
    static constexpr int kHasWildcard = 0x04;
    /// Match everything.
    static constexpr int kMatchAll = 0x08;
    /// Negate matching result.
    static constexpr int kNegative = 0x10;

    std::string pattern;
    int flags{0};
};

/**
 * @brief
 *
 * @note https://mirrors.edge.kernel.org/pub/software/scm/git/docs/gitignore.html#_pattern_format
 */
class IgnoreRules {
public:
    IgnoreRules() = default;

    explicit IgnoreRules(const std::string_view data);

    size_t Count() const {
        return rules_.size();
    }

    const Rule& operator[](const size_t n) const noexcept {
        return rules_[n];
    }

public:
    /**
     * Loads ignore rules from the string.
     */
    void Load(const std::string_view data);

    /**
     * @brief
     *
     * @param path patch to match against rules
     * @param is_directory path is a directory
     * @return true if path should be ignored
     * @return false otherwise
     */
    bool Match(const std::string_view path, const bool is_directory) const;

private:
    std::vector<Rule> rules_;
};

} // namespace Vcs
