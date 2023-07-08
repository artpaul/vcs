#include <cmd/local/workspace.h>
#include <cmd/ui/pager.h>
#include <vcs/object/commit.h>
#include <vcs/store/memory.h>

#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>

#include <functional>
#include <map>
#include <vector>

namespace Vcs {
namespace {

// contains <commit>

struct Options {
    std::vector<std::string> names;
    /// Force creation / rename / deletion.
    bool force = false;
    /// List branches.
    bool list = false;
    /// Act on remotes.
    bool remotes = false;
    /// Delete branch.
    bool remove = false;
    /// Show commit message.
    bool show_commit = false;
    /// Just show name of the current branch.
    bool show_current_branch = false;
};

int CreateBranch(const Options& options, Workspace& repo) {
    HashId head;

    if (options.names.size() == 1) {
        head = repo.GetCurrentHead();
    } else if (options.names.size() == 2) {
        if (const auto& id = repo.ResolveReference(options.names[1])) {
            head = *id;
        } else {
            fmt::print(stderr, "error: cannot resolve reference '{}'\n", options.names[1]);
            return 1;
        }
    } else {
        fmt::print(stderr, "error: at most two names should be provided\n");
        return 1;
    }

    if (!options.force) {
        if (repo.GetBranch(options.names[0])) {
            fmt::print(stderr, "branch named '{}' already exists\n", options.names[0]);
            return 1;
        }
    }

    repo.CreateBranch(options.names[0], head);

    fmt::print("branch '{}' created ({})\n", options.names[0], head);

    return 0;
}

int DeleteBranches(const Options& options, Workspace& repo) {
    if (options.names.empty()) {
        fmt::print(stderr, "error: branch name required\n");
        return 1;
    }
    // Remove branches.
    for (const auto& name : options.names) {
        if (const auto& b = repo.GetBranch(name)) {
            repo.DeleteBranch(name);
            fmt::print(stderr, "deleted branch '{}' (was {})\n", name, b->head);
        } else {
            fmt::print(stderr, "error: branch '{}' not found\n", name);
        }
    }
    return 0;
}

int ListRemoteBranches(const Options& options, const Workspace& repo) {
    const auto name_style = [] {
        if (IsAtty(stdout)) {
            return fmt::fg(fmt::terminal_color::red);
        } else {
            return fmt::text_style();
        }
    };

    std::map<std::string, std::vector<BranchInfo>> remotes;

    repo.ListRemotes([&](const RemoteInfo& remote) {
        remotes.emplace(remote.name, std::vector<BranchInfo>());
        return true;
    });

    for (auto& [name, branches] : remotes) {
        if (const auto& db = repo.GetRemoteBranches(name)) {
            db->Enumerate([&](const std::string_view, const BranchInfo& branch) {
                branches.push_back(branch);
                return true;
            });
        }
        // Ensure branches are ordered properly.
        std::sort(branches.begin(), branches.end(), [](const auto& a, const auto& b) {
            return a.name < b.name;
        });
    }

    // Print branch names with extra information.
    if (options.show_commit) {
        auto odb = repo.Objects().Cache(Store::MemoryCache<Store::NoLock>::Make());
        auto longest_name = size_t(0);
        // Calculate size of longest name.
        for (const auto& [remote, branches] : remotes) {
            for (const auto& b : branches) {
                longest_name = std::max(longest_name, b.name.size() + remote.size() + 1);
            }
        }
        // Print branches.
        for (const auto& [remote, branches] : remotes) {
            for (const auto& b : branches) {
                fmt::print(
                    "  {:<{}} {} {}\n",
                    // Styled name of the branch.
                    fmt::styled(fmt::format("{}/{}", remote, b.name), name_style()),
                    // Padding for branch name.
                    longest_name,
                    // Head commit.
                    b.head,
                    // Title of the head commit.
                    MessageTitle(odb.LoadCommit(b.head).Message())
                );
            }
        }
        return 0;
    }

    // Print branch names concisely.
    for (const auto& [remote, branches] : remotes) {
        for (const auto& b : branches) {
            fmt::print(
                "  {}\n",
                // Styled name of the branch.
                fmt::styled(fmt::format("{}/{}", remote, b.name), name_style())
            );
        }
    }

    return 0;
}

int ListBranches(const Options& options, const Workspace& repo) {
    const auto name_style = [] {
        if (IsAtty(stdout)) {
            return fmt::fg(fmt::terminal_color::green);
        } else {
            return fmt::text_style();
        }
    };

    const auto& current = repo.GetCurrentBranch();
    std::vector<BranchInfo> branches;

    repo.ListBranches([&](const BranchInfo& branch) { branches.push_back(branch); });
    // Ensure branches are ordered properly.
    std::sort(branches.begin(), branches.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    // Print branch names with extra information.
    if (options.show_commit) {
        auto odb = repo.Objects().Cache(Store::MemoryCache<Store::NoLock>::Make());
        auto longest_name = size_t(0);
        // Calculate size of longest name.
        for (const auto& b : branches) {
            longest_name = std::max(longest_name, b.name.size());
        }
        // Print branches.
        for (const auto& b : branches) {
            const bool is_active = b.name == current.name;

            fmt::print(
                "{} {:<{}} {} {}\n",
                // Mark of the active branch.
                is_active ? '*' : ' ',
                // Styled name of the branch.
                fmt::styled(b.name, is_active ? name_style() : fmt::text_style()),
                // Padding for branch name.
                longest_name,
                // Head commit.
                b.head,
                // Title of the head commit.
                MessageTitle(odb.LoadCommit(b.head).Message())
            );
        }
        return 0;
    }

    // Print branch names concisely.
    for (const auto& b : branches) {
        const bool is_active = b.name == current.name;

        fmt::print(
            "{} {}\n",
            // Mark of the active branch.
            is_active ? '*' : ' ',
            // Styled name of the branch.
            fmt::styled(b.name, is_active ? name_style() : fmt::text_style())
        );
    }

    return 0;
}

int ShowCurrentBranch(const Options&, const Workspace& repo) {
    fmt::print("{}\n", repo.GetCurrentBranch().name);
    return 0;
}

int Execute(const Options& options, Workspace& repo) {
    if (options.show_current_branch) {
        return ShowCurrentBranch(options, repo);
    }
    if (options.remove) {
        return DeleteBranches(options, repo);
    }
    if (options.list || options.names.empty()) {
        SetupPager(repo.GetConfig());

        if (options.remotes) {
            return ListRemoteBranches(options, repo);
        } else {
            return ListBranches(options, repo);
        }
    }
    return CreateBranch(options, repo);
};

} // namespace

int ExecuteBranch(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "Generic options",
            {
                {"h,help", "print help"},
                {"v,verbose", "show hash and subject", cxxopts::value(options.show_commit)},
                {"r,remotes", "act on remote-tracking branches", cxxopts::value(options.remotes)},
            }
        );
        spec.add_options(
            "Specific actions",
            {
                {"d,delete", "delete branch", cxxopts::value(options.remove)},
                {"f,force", "force creation, move/rename, deletion", cxxopts::value(options.force)},
                {"l,list", "list branch names", cxxopts::value(options.list)},
                {"show-current", "show current branch name", cxxopts::value(options.show_current_branch)},
                {"names", "name of branches", cxxopts::value(options.names)},
            }
        );
        spec.parse_positional({"names"});
        spec.custom_help("[<options>]");
        spec.positional_help("[<branch-name>...]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.count("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
