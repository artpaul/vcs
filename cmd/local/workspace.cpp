#include "workspace.h"
#include "worktree.h"

#include <vcs/changes/stage.h>

#include <util/file.h>

#include <contrib/fmt/fmt/std.h>

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

void Workspace::Status(const StatusOptions& options, const StatusCallback& cb) const {
    working_tree_->Status(options, *GetStage(), cb);
}

StageArea* Workspace::GetStage() const {
    if (stage_) {
        return stage_.get();
    }

    // if (std::filesystem::exists(state_path_ / "stage"))
    //{
    //  TODO: load from file
    //} else
    {
        const auto& branch = GetCurrentBranch();
        HashId tree_id;

        if (branch.head) {
            tree_id = odb_.LoadCommit(branch.head).Tree();
        }

        stage_ = std::make_unique<StageArea>(odb_, tree_id);
    }

    return stage_.get();
}

} // namespace Vcs
