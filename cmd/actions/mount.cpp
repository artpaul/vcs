#include <cmd/local/bare.h>
#include <src/fs/linux/mount.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>
#include <contrib/fmt/fmt/std.h>

#include <optional>

namespace Vcs {
namespace {

struct Options {
    /// Mounting path.
    std::filesystem::path path;
    /// Repository path.
    std::filesystem::path bare_path;
    /// Debug output.
    bool debug = false;
    /// Run in foreground mode.
    bool foreground = false;
};

std::string GuessWorkspaceName(const Options& options, const Repository& repo) {
    std::string name = options.path.filename();

    if (name.empty()) {
        name = options.path.parent_path().filename();
    }
    if (name.empty()) {
        name = "main";
    }

    while (true) {
        if (repo.GetWorkspace(name)) {
            name.append("1");
        } else {
            break;
        }
    }

    return name;
}

int Execute(const Options& options) {
    Repository repo(options.bare_path, Repository::Options());

    std::optional<WorkspaceInfo> base;
    std::optional<WorkspaceInfo> info;

    repo.ListWorkspaces([&](const WorkspaceInfo& ws) {
        if (ws.path == options.path) {
            base = ws;
            info = ws;
        } else if (!base) {
            base = ws;
        }
    });

    if (info) {
        if (!info->fuse) {
            fmt::print(stderr, "error: working tree exist but not virtual\n");
            return 1;
        }
    } else {
        info = WorkspaceInfo{
            .name = GuessWorkspaceName(options, repo),
            .path = options.path,
            .branch = base->branch,
            .tree = base->tree,
            .fuse = true,
        };

        if (!repo.CreateWorkspace(*info, false)) {
            fmt::print(stderr, "error: cannot create workspace at {}\n", options.path);
            return 1;
        }
    }

    Fs::MountOptions mount;

    mount.debug = options.debug;
    mount.foreground = options.foreground;
    mount.mount_path = options.path;
    mount.repository = &repo;
    mount.state_path = repo.GetLayout().Workspace(info->name);
    mount.tree = info->tree;

    return Fs::MountWorktree(mount);
}

} // namespace

int ExecuteMount(int argc, char* argv[]) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"r,repo", "path to repository", cxxopts::value<std::string>()},
                {"F", "run in foreground", cxxopts::value<bool>(options.foreground)},
                {"D", "show debug output", cxxopts::value<bool>(options.debug)},
                {"path", "path for a repository", cxxopts::value<std::string>()},
            }
        );
        spec.custom_help("[<options>]");
        spec.parse_positional({"path"});
        spec.positional_help("[<directory>]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.count("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }

        if (opts.has("path")) {
            options.path = std::filesystem::absolute(opts["path"].as<std::string>());
        } else {
            fmt::print(stderr, "error: mount path should be defined\n");
            return 1;
        }
        if (opts.has("repo")) {
            options.bare_path = std::filesystem::absolute(opts["repo"].as<std::string>());
        } else {
            fmt::print(stderr, "error: repository path should be defined\n");
            return 1;
        }
    }

    return Execute(options);
}

} // namespace Vcs
