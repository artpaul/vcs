#include "commit.h"
#include "serialize.h"
#include "store.h"

namespace Vcs {

uint64_t GetLargestGeneration(const CommitBuilder& builder, const Datastore& odb) {
    uint64_t generation = 0;
    // Parents.
    for (const auto& id : builder.parents) {
        generation = std::max(generation, odb.LoadCommit(id).Generation());
    }
    // Copies.
    if (builder.renames) {
        for (const auto& id : odb.LoadRenames(builder.renames).Commits()) {
            generation = std::max(generation, odb.LoadCommit(id).Generation());
        }
    }
    return generation;
}

std::vector<std::string_view> MessageLines(const std::string_view msg) {
    std::vector<std::string_view> lines;

    for (size_t i = 0, end = msg.size(); i < end;) {
        while (i < end && std::isspace(msg[i])) {
            ++i;
        }

        size_t l = i;
        while (i < msg.size() && msg[i] != '\n') {
            ++i;
        }
        size_t r = i - 1;
        while (l < r) {
            if (std::isspace(msg[r])) {
                --r;
            } else {
                lines.push_back(msg.substr(l, r - l + 1));
                break;
            }
        }
    }

    return lines;
}

std::string_view MessageTitle(const std::string_view msg) noexcept {
    auto pos = msg.find('\n');
    if (pos == std::string_view::npos) {
        return msg;
    } else {
        return msg.substr(0, pos);
    }
}

} // namespace Vcs
