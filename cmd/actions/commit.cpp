#include <cmd/local/workspace.h>
#include <vcs/changes/path.h>
#include <vcs/object/commit.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>

#include <functional>
#include <vector>

namespace Vcs {
namespace {

struct Options {
    std::string message;
    std::vector<std::string> paths;
};

int Execute(const Options& options, Workspace& repo) {
    std::vector<PathStatus> changes;
    bool has_create_delete = false;

    const auto cb = [&](const PathStatus& status) {
        if (status.type != PathType::Directory) {
            changes.push_back(status);

            has_create_delete |=
                (status.status == PathStatus::Deleted || status.status == PathStatus::Untracked);
        }
    };

    repo.Status(
        StatusOptions()
            .SetTracked(true)
            .SetUntracked(options.paths.empty() ? Expansion::None : Expansion::All)
            .SetInclude(PathFilter(options.paths)),
        cb
    );

    if (changes.empty()) {
        fmt::print("nothing to commit\n");
        return 1;
    }

    // Make a commit.
    const HashId id = repo.Commit(options.message, changes);

    fmt::print("[{} {}] {}\n", repo.GetCurrentBranch().name, id, MessageTitle(options.message));
    // Number of changes entries.
    fmt::print(" {} file{} changed\n", changes.size(), (changes.size() == 1 ? "" : "s"));
    // List of added and deleted files.
    if (has_create_delete) {
        std::sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) {
            return a.path < b.path;
        });

        for (const auto& change : changes) {
            if (change.status == PathStatus::Deleted || change.status == PathStatus::Untracked) {
                fmt::print(
                    " {} {}\n", (change.status == PathStatus::Deleted ? "delete" : "create"), change.path
                );
            }
        }
    }

    return 0;
}

} // namespace

int ExecuteCommit(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"m,message", "commit message", cxxopts::value(options.message)},
                {"paths", "paths to commit", cxxopts::value<std::vector<std::string>>()},
            }
        );

        spec.parse_positional("paths");
        spec.custom_help("[<options>]");
        spec.positional_help("[[--] <path>...]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.count("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }

        if (options.message.empty()) {
            fmt::print(stderr, "error: message is empty\n");
            return 1;
        }
        if (opts.has("paths")) {
            const auto paths = opts["paths"].as<std::vector<std::string>>();
            const auto& repo = cb();

            for (const auto& path : paths) {
                options.paths.push_back(repo.ToTreePath(path));
            }
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
