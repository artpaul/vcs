#pragma once

#include "bare.h"
#include "status.h"

#include <filesystem>
#include <functional>

namespace Vcs {

class StageArea;
class WorkingTree;

/**
 * Local workspace.
 */
class Workspace : public Repository {
public:
    Workspace(const std::filesystem::path& bare_path, const std::filesystem::path& work_path);

    ~Workspace();

public:
    /**
     * @name References
     * @{
     */

    /**
     * Returns state of the current branch.
     */
    Branch GetCurrentBranch() const;

    /** */
    HashId GetCurrentHead() const;

    /**@}*/

public:
    /**
     * @name Working tree
     * @{
     */

    /** Remove untracked files from the working tree. */
    void Cleanup();

    /** */
    HashId Commit(const std::string& message, const std::vector<PathStatus>& changes);

    /** Emits status of changed items in the working tree. */
    void Status(const StatusOptions& options, const StatusCallback& cb) const;

    /**@}*/

private:
    StageArea GetStage() const;

private:
    std::filesystem::path state_path_;

    mutable std::unique_ptr<StageArea> stage_;

    std::unique_ptr<WorkingTree> working_tree_;
};

} // namespace Vcs
