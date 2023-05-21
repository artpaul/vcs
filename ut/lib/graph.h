#pragma once

#include <vcs/object/store.h>

#include <unordered_map>

namespace Vcs::UT {

using KeyId = uint64_t;

class Graph {
public:
    explicit Graph(Datastore odb);

    HashId GetHashId(const KeyId id) const noexcept;

    HashId MakeCommit(
        const KeyId key,
        const std::vector<KeyId>& parents = std::vector<KeyId>(),
        const std::optional<HashId>& tree_id = std::nullopt
    );

private:
    Datastore odb_;
    /// Hash of empty tree.
    HashId tree_id_;
    std::unordered_map<KeyId, HashId> commit_ids_;
};

} // namespace Vcs::UT
