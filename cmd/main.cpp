#include "actions.h"

#include <cmd/local/workspace.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>

namespace Vcs {

extern int ExecuteBranch(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteCommit(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteDiff(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteDump(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteGit(int argc, char* argv[]);
extern int ExecuteInit(int argc, char* argv[]);
extern int ExecuteLog(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteRestore(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteShow(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteStatus(int argc, char* argv[], const std::function<Workspace&()>& cb);
extern int ExecuteSwitch(int argc, char* argv[], const std::function<Workspace&()>& cb);

static int Main(int argc, char* argv[]) {
    if (argc < 2) {
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
                    stderr, "error: no repository in the curren directory or in any parent directory\n"
                );
                std::exit(EXIT_FAILURE);
            }
        }

        workspace = std::make_unique<Workspace>(bare_path, path);

        return *workspace;
    };

    switch (ParseAction(argv[1])) {
        case Action::Dump:
            return ExecuteDump(argc - 1, argv + 1, get_workspace);
        case Action::Git:
            return ExecuteGit(argc - 1, argv + 1);

        case Action::Branch:
            return ExecuteBranch(argc - 1, argv + 1, get_workspace);
        case Action::Clean:
            break;
        case Action::Commit:
            return ExecuteCommit(argc - 1, argv + 1, get_workspace);
        case Action::Diff:
            return ExecuteDiff(argc - 1, argv + 1, get_workspace);
        case Action::Init:
            return ExecuteInit(argc - 1, argv + 1);
        case Action::Log:
            return ExecuteLog(argc - 1, argv + 1, get_workspace);
        case Action::Remove:
            break;
        case Action::Restore:
            return ExecuteRestore(argc - 1, argv + 1, get_workspace);
        case Action::Show:
            return ExecuteShow(argc - 1, argv + 1, get_workspace);
        case Action::Status:
            return ExecuteStatus(argc - 1, argv + 1, get_workspace);
        case Action::Switch:
            return ExecuteSwitch(argc - 1, argv + 1, get_workspace);
        case Action::Workspace:
            break;
        case Action::Unknown:
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
