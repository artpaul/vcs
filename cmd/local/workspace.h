#pragma once

#include "bare.h"
#include "status.h"

#include <filesystem>
#include <functional>

namespace Vcs {

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
     * @name Working area
     * @{
     */

    /** Remove untracked files from the working tree. */
    void Cleanup();

    /** Emits status of changed items in the working tree. */
    void Status(const StatusOptions& options, const StatusCallback& cb) const;

    /**@}*/

private:
    std::unique_ptr<WorkingTree> working_tree_;
};

} // namespace Vcs
