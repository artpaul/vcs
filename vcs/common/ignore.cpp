#include "ignore.h"

#include <util/split.h>
#include <util/wildmatch.h>

#include <contrib/fmt/fmt/format.h>

namespace Vcs {

static std::string_view StripRight(const std::string_view str) {
    for (size_t i = 0, end = str.size(); i != end; ++i) {
        if (!std::isspace(str[i])) {
            return str.substr(i);
        }
    }
    return {};
}

static void StripTrailingSpace(std::string& str) {
    while (str.size()) {
        if (str.back() == ' ' || str.back() == '\t') {
            str.pop_back();
        } else {
            break;
        }
    }
}

static bool ParseRule(std::string_view line, Rule& rule) {
    int slash_count = 0;
    size_t i = 0;

    if (line.empty() || line[0] == '#') {
        return false;
    }
    if (line.size() == 1) {
        if (line[0] == '*' || line[0] == '.') {
            rule.flags |= Rule::kMatchAll;
            return true;
        }
        if (line[0] == '#') {
            return false;
        }
    }

    if (line[0] == '!') {
        line = line.substr(1);
        rule.flags |= Rule::kNegative;
    }

    for (size_t end = line.size(); i != end; ++i) {
        const char c = line[i];

        if (std::isspace(c)) {
            break;
        }
        if (c == '/') {
            rule.flags |= Rule::kFullPath;
            slash_count++;

            if (slash_count == 1 && rule.pattern.empty()) {
                continue;
            }
        }
        if (c == '*' || c == '?') {
            rule.flags |= Rule::kHasWildcard;
        }

        rule.pattern += c;
    }

    if (rule.pattern.empty()) {
        return false;
    }
    if (rule.pattern.size() == 1 && rule.pattern.back() == '\r') {
        return false;
    }

    StripTrailingSpace(rule.pattern);

    if (rule.pattern.empty()) {
        return false;
    }

    if (rule.pattern.back() == '/') {
        rule.pattern.pop_back();
        rule.flags |= Rule::kDirectory;
        if (--slash_count <= 0) {
            rule.flags &= ~Rule::kFullPath;
        }
    }

    // TODO: context

    return true;
}

IgnoreRules::IgnoreRules(const std::string_view data) {
    Load(data);
}

void IgnoreRules::Load(const std::string_view data) {
    const auto lines = SplitString(data, '\n');

    for (const auto line : lines) {
        Rule rule;

        if (ParseRule(StripRight(line), rule)) {
            rules_.push_back(std::move(rule));
        }
    }
}

bool IgnoreRules::Match(const std::string_view path, const bool is_directory) const {
    if (path.empty()) {
        return false;
    }

    std::string_view filename;
    bool matched = false;

    // Extract filename.
    if (auto pos = path.rfind('/'); pos == std::string_view::npos) {
        filename = path;
    } else {
        filename = path.substr(pos + 1);
    }

    for (const auto& rule : rules_) {
        const auto match_text = [&](const std::string_view text) {
            if (rule.flags & Rule::kHasWildcard) {
                const unsigned int flags = is_directory ? WM_PATHNAME : 0;

                if (Wildcard(rule.pattern.c_str(), std::string(text).c_str(), flags) == WM_MATCH) {
                    matched = !(rule.flags & Rule::kNegative);
                }
            }
            if (rule.pattern == text) {
                matched = !(rule.flags & Rule::kNegative);
            }
        };

        if ((rule.flags & Rule::kMatchAll)) {
            matched = true;
            continue;
        }
        if ((rule.flags & Rule::kDirectory) && !is_directory) {
            continue;
        }
        // Match full path.
        if ((rule.flags & rule.kFullPath)) {
            match_text(path);
        } else {
            match_text(filename);
        }
    }

    return matched;
}

} // namespace Vcs
