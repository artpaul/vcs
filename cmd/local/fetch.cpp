#include "fetch.h"
#include "bare.h"

#include <vcs/git/converter.h>
#include <vcs/git/types.h>
#include <vcs/store/collect.h>

#include <contrib/fmt/fmt/format.h>

#include <unordered_map>
#include <unordered_set>

namespace Vcs {
namespace {

class GitFetcher : public Fetcher {
public:
    GitFetcher(const std::string& name, const std::string& path, const Repository* repo)
        : Fetcher(name, repo)
        , path_(path) {
        // Cut scheme prefix.
        if (path_.starts_with("file://")) {
            path_ = path_.substr(7);
        }
    }

    bool DoFetch(const std::function<void(const std::string_view)> cb) final {
        auto odb = repo_->Objects();
        // Set of commits to hide during conversion.
        std::unordered_set<HashId> hide;
        // List of remote branches to update.
        std::vector<decltype(remote_branches_)::iterator> to_update;

        // Open git repository.
        Git::Converter converter(path_, Git::Converter::Options());

        // Populate list of remote branches.
        converter.ListBranches([&](const std::string& name, const HashId& head) {
            remote_branches_.emplace(name, std::make_pair(head, std::optional<HashId>()));
        });
        // Nothing to fetch if there are no remote branches.
        if (remote_branches_.empty()) {
            return true;
        }

        // Populate list of the locally known remote branches.
        for (auto ri = remote_branches_.begin(); ri != remote_branches_.end(); ++ri) {
            if (const auto branch = branches_->Get(ri->first)) {
                if (!bool(branch->head)) {
                    to_update.push_back(ri);
                    continue;
                }

                const auto c = odb.LoadCommit(branch->head);
                // Locate git hash.
                for (const auto& attr : c.Attributes()) {
                    if (attr.Name() == "git-hash") {
                        const auto id = HashId::FromHex(attr.Value());
                        // Remember the commit as already converted.
                        hide.emplace(id);
                        // Asssign local hash to the remote branch.
                        if (id == ri->second.first) {
                            ri->second.second = branch->head;
                        } else {
                            to_update.push_back(ri);
                        }
                        break;
                    }
                }
            } else {
                // New branch on remote.
                to_update.push_back(ri);
            }
        }

        // Nothing to update. Done.
        if (to_update.empty()) {
            return true;
        }

        // Open remap database.
        auto remap = std::make_unique<Database<Git::Remap>>(
            repo_->GetLayout().Database("git"), Lmdb::Options{.create_if_missing = true}
        );
        // Setup remap callback.
        converter.SetRemap([&](const HashId& id) -> HashId {
            if (const auto result = remap->Get(id.ToBytes())) {
                return result.value().vcs;
            }
            return HashId();
        });

        for (auto ri : to_update) {
            std::vector<HashId> ids;
            // Populate list of git commits to convert.
            converter.ListCommits(ri->first, hide, [&](const HashId& id) {
                ids.push_back(id);
                return WalkAction::Continue;
            });

            if (ids.empty()) {
                // If there are no commits to convert then the head commit should be
                // already converter.
                if (const auto& result = remap->Get(ri->second.first.ToBytes())) {
                    ri->second.second = result->vcs;
                } else {
                    // TODO: error
                }
                continue;
            }

            HashId last;
            // Converting commits.
            for (const auto& id : ids) {
                // fmt::print("converting git {}...\n", id);
                auto collect = Store::Collect::Make();
                last = converter.ConvertCommit(id, odb.Chain(collect));

                if (!last) {
                    // fmt::print(stderr, "error: cannot convert {}\n", id);
                    return false;
                } else if (cb) {
                    cb(fmt::format(
                        "converted {} as {}; objects in commit: {}", id, last, collect->GetIds().size()
                    ));
                }

                // Save remap to database.
                if (const auto status = remap->Put(id.ToBytes(), Git::Remap{.git = id, .vcs = last})) {
                    ;
                } else {
                    // fmt::print(stderr, "error: cannot write remap '{}'\n", status.Message());
                    return false;
                }
            }

            // Set local commit for the branch.
            ri->second.second = last;
        }

        // Update local representation of the remote branches.
        for (auto ri : to_update) {
            assert(ri->second.second);

            branches_->Put(ri->first, BranchInfo{.name = ri->first, .head = *ri->second.second});
        }

        return true;
    }

private:
    /// Path to the git repository.
    std::string path_;
    /// Actual set of remote branches (git hashspace).
    std::unordered_map<std::string, std::pair<HashId, std::optional<HashId>>> remote_branches_;
};

} // namespace

Fetcher::Fetcher(std::string name, const Repository* repo)
    : name_(std::move(name))
    , repo_(repo) {
}

bool Fetcher::Fetch(std::function<void(const std::string_view)> cb) {
    if (branches_) {
        return DoFetch(std::move(cb));
    }
    // Open list of the currently known remote branches.
    if (!(branches_ = repo_->GetRemoteBranches(name_))) {
        return false;
    }

    return DoFetch(std::move(cb));
}

void Fetcher::Prune() {
}

std::unique_ptr<Fetcher> CreateGitFetcher(
    const std::string& name, const std::string& path, const Repository* repo
) {
    return std::make_unique<GitFetcher>(name, path, repo);
}

} // namespace Vcs
