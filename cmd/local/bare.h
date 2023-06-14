#pragma once

#include "db.h"
#include "layout.h"

#include <vcs/object/store.h>

#include <filesystem>
#include <unordered_set>

namespace Vcs {

class Config;
class Fetcher;

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

struct BranchInfo {
    /// Name of a branch.
    std::string name;
    /// Current commit.
    HashId head;

    static BranchInfo Load(const std::string_view data);

    static std::string Save(const BranchInfo& rec);
};

struct RemoteInfo {
    /// Name of a remote.
    std::string name;
    /// Location of a remote repository.
    std::string fetch_uri;
    /// Source repository is a git repository.
    bool is_git = false;

    static RemoteInfo Load(const std::string_view data);

    static std::string Save(const RemoteInfo& rec);
};

struct WorkspaceInfo {
    /// Name of a workspace.
    std::string name;
    /// Location of working tree.
    std::filesystem::path path;
    /// Current branch.
    std::string branch;
    /// Base of working tree.
    HashId tree;

    static WorkspaceInfo Load(const std::string_view data);

    static std::string Save(const WorkspaceInfo& rec);
};

class Repository {
public:
    struct Options {
        /// If true, all writes of objects will be forwarded directly into a pack storage.
        bool bulk_upload = false;

        /// If true, the repository will be opened in read-only mode.
        bool read_only = false;
    };

public:
    Repository(const std::filesystem::path& path, const Options& options);

    ~Repository();

    /** Initalize a bare repository. */
    static void Initialize(const std::filesystem::path& path);

    /** Returns layout of internal directories. */
    Layout GetLayout() const;

public:
    /**
     * @name Branches
     * @{
     */

    /**
     * Creates a branch which will be point to the given commit.
     */
    BranchInfo CreateBranch(const std::string& name, const HashId head);

    /**
     * Deletes a branch.
     */
    void DeleteBranch(const std::string_view name);

    /**
     * Returns state of the branch.
     */
    std::optional<BranchInfo> GetBranch(const std::string_view name) const;

    /**
     * Lists local branches.
     */
    void ListBranches(const std::function<void(const BranchInfo& branch)>& cb) const;

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

    bool HasPath(const HashId& rev, const std::string_view path) const;

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
     * @name Remotes
     * @{
     */

    bool CreateRemote(const RemoteInfo& params);

    /** Lists registered remotes. */
    void ListRemotes(const std::function<void(const RemoteInfo&)>& cb) const;

    /** Branches of the remote. */
    std::unique_ptr<Database<BranchInfo>> GetRemoteBranches(const std::string_view name) const;

    /** Fetcher for the remote. */
    std::unique_ptr<Fetcher> GetRemoteFetcher(const std::string_view name) const;

    /**@}*/

public:
    /**
     * @name Workspaces
     * @{
     */

    bool CreateWorkspace(const WorkspaceInfo& params, bool checkout);

    std::optional<WorkspaceInfo> GetWorkspace(const std::string& name) const;

    /** Lists registered workspaces. */
    void ListWorkspaces(const std::function<const WorkspaceInfo&>& cb) const;

    /**@}*/

protected:
    Datastore OpenObjects(const Options& options);

protected:
    std::filesystem::path bare_path_;
    ///
    Layout layout_;
    /// Configuration.
    std::unique_ptr<Config> config_;
    /// Object storage.
    Datastore odb_;
    /// Local branches.
    std::unique_ptr<Database<BranchInfo>> branches_;
    /// Remotes.
    std::unique_ptr<Database<RemoteInfo>> remotes_;
    /// Local workspaces.
    std::unique_ptr<Database<WorkspaceInfo>> workspaces_;

    /// Finalization routines.
    std::vector<std::function<void()>> finalizers_;
};

} // namespace Vcs
