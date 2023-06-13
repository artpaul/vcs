#include "actions.h"

#include <cmd/local/workspace.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>

namespace Vcs {

extern int ExecuteBranch(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteCommit(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteConfig(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteDiff(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteDump(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteFetch(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteGit(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteInit(int argc, char* argv[]);
extern int ExecuteLog(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteRemote(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteReset(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteRestore(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteShow(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteStatus(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteSwitch(int argc, char* argv[], const std::function<Workspace&()>& cb);

static int Main(int argc, char* argv[]) {
    {
        cxxopts::options spec("vcs");
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"C", "change working directory", cxxopts::value<std::string>(), "<path>"},
                {"P,no-pager", "do not pipe output into a pager", cxxopts::value<bool>()},
            }
        );

        spec.stop_on_positional();
        spec.custom_help("[<options>]");
        spec.positional_help("<command> [<args>]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.has("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
        if (opts.has("C")) {
            std::filesystem::current_path(opts["C"].as<std::string>());
        }

        argc -= opts.consumed();
        argv += opts.consumed();
    }

    if (argc < 1) {
        return 0;
    }

    std::unique_ptr<Workspace> workspace;

    auto get_workspace = [&] -> Workspace& {
        if (workspace) {
            return *workspace;
        }

        auto bare_path = std::filesystem::path();
        auto path = std::filesystem::current_path();
        auto vcs_path = std::filesystem::path();

        while (true) {
            vcs_path = path / ".vcs";

            switch (std::filesystem::status(vcs_path).type()) {
                case std::filesystem::file_type::directory: {
                    if (std::filesystem::exists(vcs_path / "workspaces")) {
                        bare_path = vcs_path;
                    }
                    break;
                }
                default:
                    break;
            }

            if (!bare_path.empty()) {
                break;
            }

            if (path.has_relative_path()) {
                path = path.parent_path();
            } else {
                fmt::print(
                    stderr, "error: no repository in the current directory or in any parent directory\n"
                );
                std::exit(EXIT_FAILURE);
            }
        }

        workspace = std::make_unique<Workspace>(bare_path, path);

        return *workspace;
    };

    switch (ParseAction(argv[0])) {
        case Action::Dump:
            return ExecuteDump(argc, argv, get_workspace);
        case Action::Git:
            return ExecuteGit(argc, argv, get_workspace);

        case Action::Branch:
            return ExecuteBranch(argc, argv, get_workspace);
        case Action::Clean:
            break;
        case Action::Commit:
            return ExecuteCommit(argc, argv, get_workspace);
        case Action::Config:
            return ExecuteConfig(argc, argv, get_workspace);
        case Action::Diff:
            return ExecuteDiff(argc, argv, get_workspace);
        case Action::Fetch:
            return ExecuteFetch(argc, argv, get_workspace);
        case Action::Init:
            return ExecuteInit(argc, argv);
        case Action::Log:
            return ExecuteLog(argc, argv, get_workspace);
        case Action::Remote:
            return ExecuteRemote(argc, argv, get_workspace);
        case Action::Remove:
            break;
        case Action::Reset:
            return ExecuteReset(argc, argv, get_workspace);
        case Action::Restore:
            return ExecuteRestore(argc, argv, get_workspace);
        case Action::Show:
            return ExecuteShow(argc, argv, get_workspace);
        case Action::Status:
            return ExecuteStatus(argc, argv, get_workspace);
        case Action::Switch:
            return ExecuteSwitch(argc, argv, get_workspace);
        case Action::Workspace:
            break;
        case Action::Unknown:
            fmt::print(stderr, "error: unknown command '{}'\n", argv[0]);
            return 1;
    }

    return 0;
}

} // namespace Vcs

int main(int argc, char* argv[]) {
    try {
        return Vcs::Main(argc, argv);
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
    }
    return 1;
}
