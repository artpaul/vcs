#include "workspace.h"
#include "config.h"
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
    // Lookup workspace settings.
    if (const auto& ws = workspaces_->Get(work_path.string())) {
        state_path_ = bare_path_ / "workspaces" / ws->name;
    } else {
        throw std::runtime_error(fmt::format("working tree {} is not registered", work_path));
    }

    // Setup workspace-level configuration.
    config_->Reset(ConfigLocation::Workspace, Config::MakeBackend(state_path_ / "config.json"));

    // Working tree.
    working_tree_ = std::make_unique<WorkingTree>(work_path, odb_, [this]() {
        if (const auto id = GetCurrentHead()) {
            return odb_.LoadCommit(id).Tree();
        } else {
            return HashId();
        }
    });
}

Workspace::~Workspace() = default;

auto Workspace::GetCurrentBranch() const -> Branch {
    if (auto branch = branches_->Get(StringFromFile(state_path_ / "HEAD", true))) {
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
            stage->Remove(change.path);
        } else {
            if (auto blob = working_tree_->MakeBlob(change.path, odb_)) {
                stage->Add(change.path, *blob);
            } else {
                throw std::runtime_error(fmt::format("cannot make blob from '{}'", change.path));
            }
        }
    }

    const auto now = std::chrono::system_clock::now();
    CommitBuilder builder;
    builder.message = message;
    builder.tree = stage->SaveTree(odb_);
    // Author.
    builder.author.name = config_->Get("user.name").value_or(nlohmann::json()).get<std::string>();
    builder.author.id = config_->Get("user.email").value_or(nlohmann::json()).get<std::string>();
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
    const auto [id, _] = odb_.Put(DataType::Commit, builder.Serialize());

    // Reset stage.
    stage_.reset();
    // Update head of the branch.
    branch.head = id;
    branches_->Put(branch.name, branch);

    return id;
}

bool Workspace::Reset(const ResetMode mode, const HashId& commit_id) {
    const auto tree_id = commit_id ? odb_.LoadCommit(commit_id).Tree() : HashId();

    // Resetting working tree.
    if (mode == ResetMode::Hard) {
        if (!working_tree_->SwitchTo(tree_id)) {
            return false;
        }
    }

    // Resetting current branch.
    if (mode == ResetMode::Soft || mode == ResetMode::Hard) {
        auto branch = GetCurrentBranch();
        // Reset stage.
        stage_.reset();
        // Update head of the branch.
        branch.head = commit_id;
        branches_->Put(branch.name, branch);
    }

    return true;
}

bool Workspace::Restore(const std::string& path) {
    if (const auto& entry = GetStage()->GetEntry(path)) {
        if (entry->id) {
            working_tree_->Checkout(path, *entry);
        } else {
            assert(IsDirectory(entry->type));

            working_tree_->CreateDirectory(path);
        }
        return true;
    }
    return false;
}

void Workspace::Status(const StatusOptions& options, const StatusCallback& cb) const {
    working_tree_->Status(options, *GetStage(), cb);
}

bool Workspace::SwitchTo(const std::string& branch) {
    const auto current = GetCurrentBranch();
    const auto target = branches_->Get(branch);

    // Target branch should exist.
    if (!bool(target)) {
        return false;
    }
    // Same branch. Nothing to do.
    if (current.name == target->name) {
        return true;
    }

    const auto tree_id = target->head ? odb_.LoadCommit(target->head).Tree() : HashId();

    if (!working_tree_->SwitchTo(tree_id)) {
        return false;
    }

    // Reset stage.
    stage_.reset();
    // Update HEAD.
    SetCurrentBranch(branch);

    return true;
}

std::filesystem::path Workspace::ToAbsolutePath(const std::string& path) const {
    return working_tree_->GetPath() / path;
}

std::string Workspace::ToTreePath(const std::filesystem::path& path) const {
    auto result =
        path.is_relative()
            ? std::filesystem::relative(std::filesystem::current_path() / path, working_tree_->GetPath())
            : std::filesystem::relative(path, working_tree_->GetPath());

    if (result == ".") {
        return std::string();
    } else {
        return result;
    }
}

StageArea* Workspace::GetStage() const {
    if (!bool(stage_)) {
        const auto head = GetCurrentHead();

        stage_ = std::make_unique<StageArea>(odb_, head ? odb_.LoadCommit(head).Tree() : HashId());
    }

    return stage_.get();
}

void Workspace::SetCurrentBranch(const std::string_view name) {
    StringToFile(state_path_ / "HEAD", name);
}

} // namespace Vcs
