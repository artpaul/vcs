#pragma once

#include "bare.h"
#include "status.h"

#include <filesystem>
#include <functional>

namespace Vcs {

class StageArea;
class WorkingTree;

enum class ResetMode {
    None,
    /// Reset only HEAD.
    Soft,
    /// Reset HEAD and working tree.
    Hard,
};

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
    BranchInfo GetCurrentBranch() const;

    /** */
    HashId GetCurrentHead() const;

    std::optional<HashId> ResolveReference(const std::string_view ref) const;

    void SetCurrentBranch(const std::string_view name);

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

    /** Resets HEAD and / or working tree. */
    bool Reset(const ResetMode mode, const HashId& commit_id);

    /** Restores path from HEAD. */
    bool Restore(const std::string& path);

    /** Emits status of changed items in the working tree. */
    void Status(const StatusOptions& options, const StatusCallback& cb) const;

    /** Switches working are to the given branch. */
    bool SwitchTo(const std::string& branch);

    /** Converts working tree path to filesystem path. */
    std::filesystem::path ToAbsolutePath(const std::string& path) const;

    /** Converts filesystem path to a path relative the working tree. */
    std::string ToTreePath(const std::filesystem::path& path) const;

    /**@}*/

private:
    StageArea* GetStage() const;

private:
    class Resolver;

    std::filesystem::path state_path_;
    /// Cached instance of stage area.
    mutable std::unique_ptr<StageArea> stage_;

    std::unique_ptr<WorkingTree> working_tree_;
};

} // namespace Vcs
