#include <cmd/local/workspace.h>
#include <vcs/git/converter.h>
#include <vcs/store/collect.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>
#include <contrib/fmt/fmt/std.h>

#include <unordered_map>

namespace Vcs {
namespace {

int ExecuteConvert(int argc, char* argv[]) {
    struct {
        std::string branch;
        std::filesystem::path path;
        std::filesystem::path target_path;
        bool bare = false;
    } options;

    {
        cxxopts::options spec(fmt::format("vcs git {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "show help"},
                {"git", "path to git repository", cxxopts::value<std::string>()},
                {"b,branch", "branch to convert", cxxopts::value(options.branch)->default_value("master")},
                {"bare", "create a bare repository", cxxopts::value(options.bare)},
                {"path", "path to target repository", cxxopts::value<std::string>()},
            }
        );
        spec.custom_help("[<options>]");
        spec.parse_positional({"path"});
        spec.positional_help("[<directory>]");

        auto result = spec.parse(argc, argv);
        if (result.has("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }

        if (result.has("path")) {
            options.target_path = result["path"].as<std::string>();
            options.target_path = std::filesystem::absolute(options.target_path);
        } else {
            fmt::print(stderr, "error: path should be defined\n");
            return 1;
        }
        if (result.has("git")) {
            options.path = result["git"].as<std::string>();
            options.path = std::filesystem::absolute(options.path);
        } else {
            fmt::print(stderr, "error: git path should be defined\n");
            return 1;
        }
    }

    Git::Converter converter(options.path, Git::Converter::Options());

    // Ordered list of git commits to convert.
    std::vector<HashId> ids;
    std::unordered_map<HashId, HashId> remap;

    // Setup remap callback.
    converter.SetRemap([&](const HashId& id) -> HashId {
        if (auto ri = remap.find(id); ri != remap.end()) {
            return ri->second;
        }
        return HashId();
    });
    // Populate list of commits.
    converter.ListCommits(options.branch, [&](const HashId& id) {
        ids.push_back(id);
        return WalkAction::Continue;
    });

    const auto bare_path = options.bare ? options.target_path : options.target_path / ".vcs";

    // Initialize target repository.
    if (options.bare) {
        Repository::Initialize(bare_path);
    } else {
        // Create working area.
        std::filesystem::create_directories(options.path);
        // Create bare repository.
        Repository::Initialize(bare_path);
    }

    Repository repo(bare_path);

    HashId last;
    // Converting commits.
    for (const auto& id : ids) {
        fmt::print("converting git {}...\n", id);
        auto collect = Store::Collect::Make();
        last = converter.ConvertCommit(id, repo.Objects().Chain(collect));

        if (!last) {
            fmt::print(stderr, "error: cannot convert {}\n", id);
            return 1;
        } else {
            fmt::print("converted {} as {}; objects in commit: {}\n", id, last, collect->GetIds().size());
        }

        remap.emplace(id, last);
    }

    repo.CreateBranch(options.branch, last);

    // Create and set a branch.
    if (const auto& b = repo.GetBranch(options.branch)) {
        fmt::print("branch '{}' set to {}\n", options.branch, b->head);
    } else {
        return 1;
    }

    // Create a workspace if requested.
    if (!options.bare) {
        const auto ws = Repository::Workspace{
            .name = "main",
            .path = options.target_path,
            .branch = options.branch,
        };

        if (!repo.CreateWorkspace(ws, true)) {
            fmt::print(stderr, "error: cannot create workspace at '{}'\n", options.target_path);
            return 1;
        }
    }

    return 0;
}

void PrintHelp() {
    fmt::print("vcs git convert <options> <output>\n");
}

} // namespace

int ExecuteGit(int argc, char* argv[]) {
    const std::unordered_map<std::string_view, std::function<int(int argc, char* argv[])>> actions = {
        {"convert", ExecuteConvert},
    };

    if (argc > 1) {
        if (auto ai = actions.find(argv[1]); ai != actions.end()) {
            return ai->second(argc - 1, argv + 1);
        }
    }

    PrintHelp();
    return 1;
}

} // namespace Vcs
