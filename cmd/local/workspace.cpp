#include "workspace.h"
#include "worktree.h"

namespace Vcs {

Workspace::Workspace(const std::filesystem::path& bare_path, const std::filesystem::path& work_path)
    : Repository(bare_path) {
    working_tree_ = std::make_unique<WorkingTree>(work_path, odb_);
}

Workspace::~Workspace() = default;

void Workspace::Status(const StatusOptions& options, const StatusCallback& cb) const {
    working_tree_->Status(options, StageArea(odb_), cb);
}

} // namespace Vcs
