#include <cmd/local/workspace.h>

#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>

#include <functional>
#include <vector>

namespace Vcs {
namespace {

struct Options {
    /// Paths to include in status.
    std::vector<std::string> paths;
};

void BranchInfo(const Options&, const Workspace& repo) {
    const auto& branch = repo.GetCurrentBranch();

    fmt::print(
        "On branch {}\n",
        fmt::styled(
            branch.name,
            util::is_atty(stdout) ? fmt::fg(fmt::terminal_color::bright_magenta) : fmt::text_style()
        )
    );
}

void ChangesInfo(const Options& options, const Workspace& repo) {
    std::vector<PathStatus> tracked;
    std::vector<PathStatus> untracked;

    repo.Status(StatusOptions().SetInclude(PathFilter(options.paths)), [&](const PathStatus& status) {
        if (status.status == PathStatus::Deleted || status.status == PathStatus::Modified) {
            tracked.push_back(status);
        }
        if (status.status == PathStatus::Untracked) {
            untracked.push_back(status);
        }
    });

    const auto process_paths = [&](std::vector<PathStatus>& paths) {
        // Sort paths.
        std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    };

    process_paths(tracked);
    process_paths(untracked);

    if (tracked.size()) {
        fmt::print("\nChanges to be committed:\n");
        fmt::print("  (use \"vcs restore <file>...\" to discard changes in working directory)\n");

        for (const auto& status : tracked) {
            const auto status_name = [&] {
                if (status.status == PathStatus::Modified) {
                    return "modified:   ";
                } else if (status.status == PathStatus::Deleted) {
                    return "deleted:    ";
                }
                return "";
            };

            const auto status_style = [] {
                if (util::is_atty(stdout)) {
                    return fmt::fg(fmt::terminal_color::red);
                } else {
                    return fmt::text_style();
                }
            };

            fmt::print(
                "\t{}\n", fmt::styled(
                              fmt::format(
                                  "{}{}{}", status_name(), status.path,
                                  (status.type == PathType::Directory ? "/" : "")
                              ),
                              status_style()
                          )
            );
        }
    }
    if (untracked.size()) {
        fmt::print("\nUntracked files:\n");
        fmt::print("  (use \"vcs add <file>...\" if you want to track changes to file)\n");

        for (const auto& status : untracked) {
            fmt::print("\t{}{}\n", status.path, (status.type == PathType::Directory ? "/" : ""));
        }
    }

    if (untracked.size()) {
        fmt::print("\n");
    }
}

int Execute(const Options& options, const Workspace& repo) {
    BranchInfo(options, repo);
    ChangesInfo(options, repo);
    return 0;
};

} // namespace

int ExecuteStatus(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"paths", "paths to status", cxxopts::value<std::vector<std::string>>()},
            }
        );
        spec.parse_positional({"paths"});
        spec.custom_help("[<options>]");
        spec.positional_help("[[--] <path>...]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.count("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
        if (opts.has("paths")) {
            const auto& paths = opts["paths"].as<std::vector<std::string>>();
            const auto& repo = cb();

            for (const auto& path : paths) {
                options.paths.push_back(repo.ToTreePath(path));
            }
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
