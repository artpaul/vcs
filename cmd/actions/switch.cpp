
#include <cmd/local/workspace.h>
#include <vcs/changes/changelist.h>
#include <vcs/changes/path.h>

#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>

#include <functional>
#include <vector>

// TODO:
//  --overwrite-ignore       update ignored files (default)
//  --ignore-other-worktrees  do not check if another worktree is holding the given ref

namespace Vcs {
namespace {

struct Options {
    /// Branch name to switch to.
    std::string branch_name;
    /// Head commit to set.
    std::optional<HashId> id;
    /// Create new branch before switching.
    bool create = false;
    /// Check if switch can be done without conflicts.
    bool dry_run = false;
    /// Force execution (throw away local modifications).
    bool force = false;
};

int Execute(const Options& options, Workspace& repo) {
    const auto target = repo.GetBranch(options.branch_name);

    // Target branch should exist or be requested for creation.
    if (!bool(target)) {
        if (options.create) {
            // Create a reference.
            repo.CreateBranch(options.branch_name, options.id ? *options.id : repo.GetCurrentHead());
            // Update HEAD.
            repo.SetCurrentBranch(options.branch_name);
            return 0;
        } else {
            return 1;
        }
    }
    // Check conflicts with local modifications.
    if (options.force) {
        if (options.dry_run) {
            fmt::print("dry run: force switch will be used\n");
            return 0;
        }
    } else {
        std::vector<std::string> paths;

        {
            auto cb = [&](const PathStatus& status) {
                // Cannot lose already deleted file. Skip it.
                if (status.status == PathStatus::Deleted) {
                    return;
                }

                paths.push_back(status.path);
            };

            // Collect modifications.
            repo.Status(StatusOptions().SetTracked(true).SetUntracked(Expansion::None), cb);
        }

        if (paths.size()) {
            bool has_changes = false;

            const auto status_style = [] {
                if (util::is_atty(stdout)) {
                    return fmt::fg(fmt::terminal_color::red);
                } else {
                    return fmt::text_style();
                }
            };

            auto cb = [&, first = true](const Change& change) mutable {
                if (first) {
                    fmt::print(
                        stderr, "The local changes to the following files would be overwritten by switch:\n"
                                "  (use \"vcs commit <file>...\" to commit changes in working directory)\n"
                    );
                    first = false;
                }
                fmt::print(stderr, "\t{}\n", fmt::styled(change.path, status_style()));
                has_changes = true;
            };

            ChangelistBuilder(repo.Objects(), cb)
                .SetInclude(PathFilter(paths))
                .Changes(repo.GetCurrentHead(), target->head);

            if (has_changes) {
                fmt::print(stderr, "\n");
                fmt::print(stderr, "Please commit the changes before switching the branches.\n");
                return 1;
            }
        }

        if (options.dry_run) {
            fmt::print("dry run: no conflicts detected\n");
            return 0;
        }
    }

    if (repo.SwitchTo(options.branch_name)) {
        return 0;
    } else {
        fmt::print(stderr, "error: cannot switch to '{}'\n", options.branch_name);
        return 1;
    }
}

} // namespace

int ExecuteSwitch(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"n,dry-run", "dry run", cxxopts::value(options.dry_run)},
                {"branch", "branch name", cxxopts::value(options.branch_name)},
                {"commit", "commit id", cxxopts::value<std::string>()},
            }
        );
        spec.parse_positional("branch", "commit");
        spec.custom_help("[<options>]");
        spec.positional_help("<branch> [<commit>]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.count("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
        if (options.branch_name.empty()) {
            fmt::print(stderr, "error: branch name should be specified\n");
            return 1;
        }
        if (opts.has("commit")) {
            const auto arg = opts["commit"].as<std::string>();

            if (const auto& id = cb().ResolveReference(arg)) {
                options.id = *id;
            } else {
                fmt::print(stderr, "error: cannot resolve reference '{}'\n", arg);
                return 1;
            }
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
