#include <cmd/local/workspace.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>

#include <functional>
#include <vector>

namespace Vcs {
namespace {

struct Options {
    /// Paths to restore.
    std::vector<std::string> paths;
    bool dry_run = false;
};

int Execute(const Options& options, Workspace& repo) {
    std::vector<PathStatus> tracked;

    const auto cb = [&](const PathStatus& status) {
        if (status.status == PathStatus::Deleted || status.status == PathStatus::Modified) {
            tracked.push_back(status);
        }
    };

    repo.Status(StatusOptions().SetInclude(PathFilter(options.paths)).SetUntracked(Expansion::None), cb);

    if (options.dry_run) {
        for (const auto& status : tracked) {
            fmt::print(
                "would restore {}{}\n", status.path, (status.type == PathType::Directory ? "/" : "")
            );
        }
    } else {
        for (const auto& status : tracked) {
            if (repo.Restore(status.path)) {
                fmt::print("restored '{}'\n", status.path);
            } else {
                fmt::print(stderr, "error: path '{}' did not match any known file(s)\n", status.path);
                return 1;
            }
        }
    }

    return 0;
}

} // namespace

int ExecuteRestore(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"n,dry-run", "dry run", cxxopts::value(options.dry_run)},
                {"paths", "paths to restore", cxxopts::value<std::vector<std::string>>()},
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
        } else {
            fmt::print(stderr, "error: path(s) for restoring should be specified\n");
            return 1;
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
