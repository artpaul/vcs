
#include <cmd/local/workspace.h>
#include <vcs/changes/changelist.h>
#include <vcs/changes/path.h>

#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>

#include <functional>
#include <vector>

namespace Vcs {
namespace {

struct Options {
    bool verbose = false;
};

int Execute(const Options& options, const Workspace& repo) {
    repo.ListRemotes([&](const Workspace::Remote& remote) {
        if (options.verbose) {
            fmt::print("{}  {}{}\n", remote.name, remote.fetch_uri, remote.is_git ? " (git)" : "");
        } else {
            fmt::print("{}\n", remote.name);
        }
    });

    return 0;
}

} // namespace

int ExecuteRemote(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"v,verbose", "show remote uri", cxxopts::value(options.verbose)},
            }
        );
        spec.custom_help("[<options>]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.has("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
