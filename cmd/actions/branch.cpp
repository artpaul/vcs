#include <cmd/local/workspace.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>

#include <functional>
#include <vector>

namespace Vcs {
namespace {

struct Options {
    std::vector<std::string> names;
    // show-current
    // contains <commit>
};

int ListBranches(const Options&, const Workspace& repo) {
    const auto& current = repo.GetCurrentBranch();
    std::vector<Repository::Branch> branches;

    repo.ListBranches([&](const Repository::Branch& branch) { branches.push_back(branch); });

    std::sort(branches.begin(), branches.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    for (const auto& b : branches) {
        if (b.name == current.name) {
            fmt::print("* {}\n", b.name);
        } else {
            fmt::print("  {}\n", b.name);
        }
    }

    return 0;
}

int Execute(const Options& options, const Workspace& repo) {
    return ListBranches(options, repo);
};

} // namespace

int ExecuteBranch(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"names", "name of branches", cxxopts::value(options.names)},
            }
        );
        spec.parse_positional({"names"});
        spec.custom_help("[<options>]");
        spec.positional_help("[<branch-name>...]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.count("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
