#include "revparse.h"

#include <cassert>

namespace Vcs {

static auto ParseNumber(std::string_view::const_iterator& ci, const std::string_view::const_iterator end) {
    assert(ci != end && std::isdigit(*ci));

    uint64_t value = 0;

    do {
        value = value * 10 + (*ci - '0');
        ++ci;
        // TODO: interger overflow
    } while (ci != end && std::isdigit(*ci));

    return value;
}

std::optional<HashId> ReferenceResolver::Resolve(const std::string_view ref) const {
    std::optional<HashId> result;
    // Length of an identifier.
    size_t length = 0;

    const auto ensure_revision_loaded = [&]() {
        if (result) {
            return true;
        }
        if (length == 0) {
            return false;
        }
        if (auto id = DoLookup(ref.substr(0, length))) {
            result = id;
            return true;
        }
        return false;
    };

    const auto extract_how_many_carets = [](auto& ci, const auto end) {
        ++ci;

        if (ci != end && std::isdigit(*ci)) {
            return ParseNumber(ci, end);
        } else {
            return uint64_t(1u);
        }
    };

    const auto extract_how_many_tildes = [](auto& ci, const auto end) {
        uint64_t count = 0;

        while (ci != end) {
            if (*ci == '~') {
                ++ci;
                ++count;
            } else if (std::isdigit(*ci)) {
                count = (count + ParseNumber(ci, end)) - 1;
            } else {
                break;
            }
        }

        return count;
    };

    for (auto ci = ref.begin(), end = ref.end(); ci != end;) {
        switch (*ci) {
            case '^': {
                const uint64_t count = extract_how_many_carets(ci, end);

                if (!ensure_revision_loaded()) {
                    return {};
                }
                if (auto id = DoGetNthParent(*result, count)) {
                    result = id;
                } else {
                    return {};
                }

                break;
            }
            case '~': {
                const uint64_t count = extract_how_many_tildes(ci, end);

                if (!ensure_revision_loaded()) {
                    return {};
                }
                if (auto id = DoGetNthAncestor(*result, count)) {
                    result = id;
                } else {
                    return {};
                }

                break;
            }
            case ':': {
                // Not implemented.
                return {};
            }
            case '@': {
                if (length == 0) {
                    assert(!result);
                    // @ alone is a shortcut for HEAD.
                    if (auto id = DoLookup("HEAD")) {
                        ++ci;
                        ++length;
                        result = id;
                    } else {
                        return {};
                    }
                    break;
                }
                // Not implemented.
                return {};
            }
            default:
                if (result) {
                    // Unknown character in the pathspec.
                    return {};
                } else {
                    ++ci;
                    ++length;
                }
                break;
        }
    }

    if (result) {
        return result;
    } else if (auto id = DoLookup(ref.substr(0, length))) {
        return id;
    }

    return {};
}

} // namespace Vcs
