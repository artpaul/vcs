#pragma once

#include "db.h"

#include <vcs/object/store.h>

#include <filesystem>
#include <unordered_set>

namespace Vcs {

class Config;

struct LogOptions {
    std::unordered_set<HashId> roots;
    std::unordered_set<HashId> hidden;
    bool first_parent = false;

    LogOptions& Hide(const HashId& commit_id) {
        if (commit_id) {
            hidden.insert(commit_id);
        }
        return *this;
    }

    LogOptions& Push(const HashId& commit_id) {
        if (commit_id) {
            roots.insert(commit_id);
        }
        return *this;
    }

    LogOptions& SetFirstParent(const bool value) {
        first_parent = value;
        return *this;
    }
};

class Repository {
public:
    struct Branch {
        /// Name of a branch.
        std::string name;
        /// Current commit.
        HashId head;

        static Branch Load(const std::string_view data);

        static std::string Save(const Branch& rec);
    };

    struct Workspace {
        /// Name of a workspace.
        std::string name;
        /// Location of working tree.
        std::filesystem::path path;
        /// Current branch.
        std::string branch;
        /// Base of working tree.
        HashId tree;

        static Workspace Load(const std::string_view data);

        static std::string Save(const Workspace& rec);
    };

public:
    Repository(const std::filesystem::path& path);

    ~Repository();

    /** Initalize a bare repository. */
    static void Initialize(const std::filesystem::path& path);

public:
    /**
     * @name Branches
     * @{
     */

    /**
     * Creates a branch which will be point to the given commit.
     */
    void CreateBranch(const std::string& name, const HashId head);

    /**
     * Deletes a branch.
     */
    void DeleteBranch(const std::string& name);

    /**
     * Returns state of the branch.
     */
    std::optional<Branch> GetBranch(const std::string& name) const;

    /**
     * Lists local branches.
     */
    void ListBranches(const std::function<void(const Branch& branch)>& cb) const;

    /**@}*/

public:
    /**
     * @name Configuration
     * @{
     */

    const Config& GetConfig() const;

    /**@}*/

public:
    /**
     * @name History
     * @{
     */

    void Log(
        const LogOptions& options, const std::function<bool(const HashId& id, const Commit& commit)>& cb
    ) const;

    /**@}*/

public:
    /**
     * @name Object storage
     * @{
     */

    Datastore Objects();

    const Datastore Objects() const;

    /**@}*/

public:
    /**
     * @name Workspaces
     * @{
     */

    bool CreateWorkspace(const Workspace& params, bool checkout);

    std::optional<Workspace> GetWorkspace(const std::string& name) const;

    /** Lists registered workspaces. */
    void ListWorkspaces(const std::function<const Workspace&>& cb) const;

    /**@}*/

protected:
    std::filesystem::path bare_path_;
    /// Configuration.
    std::unique_ptr<Config> config_;
    /// Object storage.
    Datastore odb_;
    /// Local branches.
    std::unique_ptr<Database<Branch>> branches_;
    /// Local workspaces.
    std::unique_ptr<Database<Workspace>> workspaces_;
};

} // namespace Vcs
