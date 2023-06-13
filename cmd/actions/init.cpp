#include <cmd/local/bare.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>
#include <contrib/fmt/fmt/std.h>

namespace Vcs {
namespace {

struct Options {
    std::filesystem::path path;
    /// Name of the default branch.
    std::string branch;
    /// Create a bare repository.
    bool bare = false;
};

int InitializeBare(const Options& options) {
    // Just create a bare repository.
    Repository::Initialize(options.path);

    Repository repo(options.path);
    // Create starting point of the history.
    repo.CreateBranch(options.branch, HashId());

    fmt::print("Repository has been initialized at {}\n", options.path);

    return 0;
}

int InitializeWorkspace(const Options& options) {
    const auto bare_path = options.path / ".vcs";

    // Create working area.
    std::filesystem::create_directories(options.path);
    // Create bare repository.
    Repository::Initialize(bare_path);

    Repository repo(bare_path);
    // Create starting point of the history.
    repo.CreateBranch(options.branch, HashId());

    const auto ws = WorkspaceInfo{
        .name = "main",
        .path = options.path,
        .branch = options.branch,
    };

    if (!repo.CreateWorkspace(ws, true)) {
        fmt::print(stderr, "error: cannot create workspace at {}\n", options.path);
        return 1;
    }

    fmt::print("Workspace has been initialized at {}\n", options.path);

    return 0;
}

int Execute(const Options& options) {
    if (options.bare) {
        return InitializeBare(options);
    } else {
        return InitializeWorkspace(options);
    }
}

} // namespace

int ExecuteInit(int argc, char* argv[]) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"b,branch", "branch name", cxxopts::value(options.branch)->default_value("trunk")},
                {"bare", "create a bare repository", cxxopts::value(options.bare)},
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
            options.path = opts["path"].as<std::string>();
            options.path = std::filesystem::absolute(options.path);
        } else {
            options.path = std::filesystem::current_path();
        }

        if (options.branch.empty()) {
            fmt::print(stderr, "error: branch should be defined\n");
            return 1;
        }
    }

    return Execute(options);
}

} // namespace Vcs
