#pragma once

#include "db.h"

#include <functional>
#include <memory>
#include <string>

namespace Vcs {

class Repository;

struct BranchInfo;

/**
 * Fetch data from a remote repository to the local database.
 */
class Fetcher {
public:
    Fetcher(const std::string name, const Repository* repo);

    virtual ~Fetcher() = default;

    /**
     * Updates references in the local snapshot.
     *
     * @param cb status callback.
     */
    bool Fetch(const std::function<void(const std::string_view)> cb);

    /**
     * Deletes stale references from the local snapshot.
     */
    void Prune();

protected:
    virtual bool DoFetch(const std::function<void(const std::string_view)> cb) = 0;

protected:
    /// Name of the remote.
    std::string name_;
    /// Local repository.
    const Repository* repo_;
    /// List of localy stored remote branches.
    std::unique_ptr<Database<BranchInfo>> branches_;
};

std::unique_ptr<Fetcher> CreateGitFetcher(
    const std::string& name, const std::string& path, const Repository* repo
);

} // namespace Vcs
