#include "graph.h"

#include <vcs/changes/validate.h>
#include <vcs/object/commit.h>
#include <vcs/object/serialize.h>

namespace Vcs::UT {

Graph::Graph(Datastore odb)
    : odb_(odb) {
    tree_id_ = odb_.Put(DataType::Tree, TreeBuilder().Serialize()).first;
}

HashId Graph::GetHashId(const KeyId id) const noexcept {
    if (const auto ci = commit_ids_.find(id); ci != commit_ids_.end()) {
        return ci->second;
    }
    return HashId();
}

HashId Graph::MakeCommit(
    const KeyId key, const std::vector<KeyId>& parents, const std::optional<HashId>& tree_id
) {
    // Check existence of the parent commits.
    for (const auto p : parents) {
        if (commit_ids_.find(p) == commit_ids_.end()) {
            throw std::runtime_error(fmt::format("no commit with id={}", p));
        }
    }
    // Lookup commit with same key.
    if (auto ci = commit_ids_.find(key); ci != commit_ids_.end()) {
        return ci->second;
    }

    CommitBuilder commit;

    for (const auto p : parents) {
        commit.parents.push_back(commit_ids_.find(p)->second);
    }
    commit.message = std::to_string(key);
    commit.tree = tree_id.value_or(tree_id_);
    commit.generation = 1 + GetLargestGeneration(commit, odb_);
    commit.author.name = "John";
    commit.author.when = commit_ids_.size();

    const auto& data = commit.Serialize();
    if (!CheckConsistency(Object::Load(DataType::Commit, data), odb_)) {
        throw std::runtime_error("inconsistent commit object");
    }
    HashId id = odb_.Put(DataType::Commit, data).first;
    commit_ids_.emplace(key, id);
    return id;
}

} // namespace Vcs::UT
