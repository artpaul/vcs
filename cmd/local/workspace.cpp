#include "workspace.h"
#include "worktree.h"

#include <vcs/changes/stage.h>
#include <vcs/object/commit.h>
#include <vcs/object/serialize.h>

#include <util/file.h>

#include <contrib/fmt/fmt/std.h>

#include <chrono>

namespace Vcs {

Workspace::Workspace(const std::filesystem::path& bare_path, const std::filesystem::path& work_path)
    : Repository(bare_path) {
    working_tree_ = std::make_unique<WorkingTree>(work_path, odb_);
    // Lookup workspace.
    if (const auto& ws = workspaces_->Get(work_path.string())) {
        state_path_ = bare_path_ / "workspaces" / ws->name;
    } else {
        throw std::runtime_error(fmt::format("working tree {} is not registered", work_path));
    }
}

Workspace::~Workspace() = default;

auto Workspace::GetCurrentBranch() const -> Branch {
    if (auto branch = branches_->Get(StringFromFile(state_path_ / "HEAD"))) {
        return *branch;
    }
    return Branch();
}

HashId Workspace::GetCurrentHead() const {
    return GetCurrentBranch().head;
}

std::optional<HashId> Workspace::ResolveReference(const std::string_view ref) const {
    if (HashId::IsHex(ref)) {
        return HashId::FromHex(ref);
    }
    if (const auto& branch = branches_->Get(ref)) {
        return branch->head;
    }
    if (ref == "HEAD") {
        return GetCurrentHead();
    }
    return std::nullopt;
}

HashId Workspace::Commit(const std::string& message, const std::vector<PathStatus>& changes) {
    auto branch = GetCurrentBranch();
    auto stage = GetStage();

    for (const auto& change : changes) {
        if (change.status == PathStatus::Deleted) {
            stage.Remove(change.path);
        } else {
            if (auto blob = working_tree_->MakeBlob(change.path, odb_)) {
                stage.Add(change.path, *blob);
            } else {
                throw std::runtime_error(fmt::format("cannot make blob from '{}'", change.path));
            }
        }
    }

    const auto now = std::chrono::system_clock::now();
    CommitBuilder builder;
    builder.message = message;
    builder.tree = stage.SaveTree(odb_);
    // Author.
    builder.author.when = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    // Committer.
    builder.committer = builder.author;
    // Parents.
    if (branch.head) {
        builder.parents.push_back(branch.head);
    }
    // Generation number.
    builder.generation = 1 + GetLargestGeneration(builder, odb_);

    // Store commit object.
    const HashId id = odb_.Put(DataType::Commit, builder.Serialize());

    // Update head of the branch.
    branch.head = id;
    branches_->Put(branch.name, branch);

    return id;
}

void Workspace::Status(const StatusOptions& options, const StatusCallback& cb) const {
    working_tree_->Status(options, GetStage(), cb);
}

std::string Workspace::ToTreePath(const std::filesystem::path& path) const {
    if (path.is_relative()) {
        return (std::filesystem::current_path() / path).lexically_relative(working_tree_->GetPath());
    }
    return path.lexically_relative(working_tree_->GetPath());
}

StageArea Workspace::GetStage() const {
    const auto head = GetCurrentHead();

    return StageArea(odb_, head ? odb_.LoadCommit(head).Tree() : HashId());
}

} // namespace Vcs
