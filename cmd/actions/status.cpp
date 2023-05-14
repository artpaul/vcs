#include <cmd/local/workspace.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>

#include <functional>
#include <vector>

namespace Vcs {
namespace {

struct Options { };

void BranchInfo(const Options&, const Workspace&) {
    fmt::print("On branch ...\n");
}

void ChangesInfo(const Options&, const Workspace& repo) {
    std::vector<PathStatus> untracked;

    repo.Status(StatusOptions(), [&](const PathStatus& status) {
        if (status.status == PathStatus::Untracked) {
            untracked.push_back(status);
        }
    });

    const auto process_paths = [&](std::vector<PathStatus>& paths) {
        // Sort paths.
        std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    };

    process_paths(untracked);

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
    }

    return Execute(options, cb());
}

} // namespace Vcs
