#include <cmd/local/fetch.h>
#include <cmd/local/workspace.h>
#include <vcs/git/types.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>
#include <contrib/fmt/fmt/std.h>
#include <contrib/json/nlohmann.hpp>

#include <unordered_map>

static const std::string kDefaultRemote("origin");

namespace Vcs {
namespace {

int ExecuteConvert(int argc, char* argv[], const std::function<Workspace&()>&) {
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
                {"b,branch", "branch to convert", cxxopts::value(options.branch)},
                {"bare", "create a bare repository", cxxopts::value(options.bare)},
                {"path", "path to target repository", cxxopts::value<std::string>()},
            }
        );
        spec.custom_help("[<options>]");
        spec.parse_positional("path");
        spec.positional_help("[<directory>]");

        const auto opts = spec.parse(argc, argv);
        if (opts.has("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }

        if (opts.has("path")) {
            options.target_path = opts["path"].as<std::string>();
            options.target_path = std::filesystem::absolute(options.target_path);
        } else {
            fmt::print(stderr, "error: path should be defined\n");
            return 1;
        }
        if (opts.has("git")) {
            options.path = opts["git"].as<std::string>();
            options.path = std::filesystem::absolute(options.path);
        } else {
            fmt::print(stderr, "error: git path should be defined\n");
            return 1;
        }
    }

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

    Repository repo(bare_path, Repository::Options());
    std::optional<std::string> branch_name;

    // Create remote.
    {
        RemoteInfo remote;
        remote.name = kDefaultRemote;
        remote.fetch_uri = fmt::format("file://{}", options.path.string());
        remote.is_git = true;

        if (!repo.CreateRemote(remote)) {
            fmt::print(stderr, "error: cannot create remote '{}'\n", remote.name);
            return 1;
        }
    }

    // Convert commits from the git repository to the system format.
    if (auto fetcher = repo.GetRemoteFetcher(kDefaultRemote)) {
        const auto cb = [](const std::string_view msg) {
            fmt::print("{}\n", msg);
        };

        if (!fetcher->Fetch(cb)) {
            fmt::print(stderr, "error: cannot fetch from remote '{}'\n", kDefaultRemote);
            return 1;
        }
    } else {
        fmt::print(stderr, "error: cannot get fetcher for '{}'\n", kDefaultRemote);
        return 1;
    }

    // Create local branch.
    if (auto branches = repo.GetRemoteBranches(kDefaultRemote)) {
        const auto make_branch_names = [&]() -> std::vector<std::string> {
            if (options.branch.size()) {
                return {options.branch};
            } else {
                return {"main", "master", "trunk"};
            }
        };

        for (const auto& name : make_branch_names()) {
            if (const auto ret = branches->Get(name)) {
                // Create local branch with the choosen name.
                repo.CreateBranch(name, ret->head);
                // Use the name further.
                branch_name = name;
                break;
            }
        }

        if (!bool(branch_name)) {
            if (options.branch.empty()) {
                fmt::print(
                    stderr,
                    "error: none of branch named 'main', 'master' or 'trunk' cannot be located in "
                    "remote '{}'\n",
                    kDefaultRemote
                );
            } else {
                fmt::print(
                    stderr, "error: cannot locate remote branch '{}/{}'\n", kDefaultRemote, options.branch
                );
            }
            return 1;
        }
    } else {
        fmt::print(stderr, "error: cannot get remote branches\n");
        return 1;
    }

    // Check the branch was created.
    if (const auto& b = repo.GetBranch(*branch_name)) {
        fmt::print("branch '{}' set to {}\n", *branch_name, b->head);
    } else {
        fmt::print(stderr, "error: cannot get branch '{}'\n", *branch_name);
        return 1;
    }

    // Create a workspace if requested.
    if (!options.bare) {
        const auto ws = WorkspaceInfo{
            .name = "main",
            .path = options.target_path,
            .branch = *branch_name,
        };

        if (!repo.CreateWorkspace(ws, true)) {
            fmt::print(stderr, "error: cannot create workspace at '{}'\n", options.target_path);
            return 1;
        }
    }

    return 0;
}

int ExecuteOid(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    struct {
        HashId oid;
    } options;

    {
        cxxopts::options spec(fmt::format("vcs git {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "show help"},
                {"oid", "git oid", cxxopts::value<std::string>()},
            }
        );
        spec.custom_help("[<options>]");
        spec.parse_positional("oid");
        spec.positional_help("<oid>");

        const auto opts = spec.parse(argc, argv);
        if (opts.has("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
        if (opts.has("oid")) {
            options.oid = HashId::FromHex(opts["oid"].as<std::string>());
        } else {
            fmt::print(stderr, "error: oid should be provided\n");
            return 1;
        }
    }

    Database<Git::Remap> db(cb().GetLayout().Database("git"), Lmdb::Options());

    if (const auto rec = db.Get(options.oid.ToBytes())) {
        fmt::print("{}\n", rec.value().vcs);
        return 0;
    } else if (rec.error().IsNotFound()) {
        fmt::print(stderr, "error: unknown oid '{}'\n", options.oid);
        return 1;
    } else {
        fmt::print(stderr, "error: {}\n", rec.error().Message());
        return 1;
    }
}

void PrintHelp() {
    fmt::print("usage: vcs git convert <options> <output>\n"
               "   or: vcs git oid <options> <oid>\n");
}

} // namespace

int ExecuteGit(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    const std::unordered_map<
        std::string_view, std::function<int(int argc, char* argv[], const std::function<Workspace&()>& cb)>>
        actions = {
            {"convert", ExecuteConvert},
            {"oid", ExecuteOid},
        };

    if (argc > 1) {
        if (auto ai = actions.find(argv[1]); ai != actions.end()) {
            return ai->second(argc - 1, argv + 1, cb);
        }
    }

    PrintHelp();
    return 1;
}

} // namespace Vcs
