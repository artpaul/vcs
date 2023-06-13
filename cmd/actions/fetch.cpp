
#include <cmd/local/fetch.h>
#include <cmd/local/workspace.h>

#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>

#include <functional>
#include <vector>

namespace Vcs {
namespace {

struct Options {
    /// Name of remote to fetch from.
    std::string remote;
    /// Fetch from all remotes.
    bool all = false;
};

int Execute(const Options& options, Workspace& repo) {
    const auto fetch_from_remote = [&](const auto& name, auto fetcher) {
        const auto cb = [](const std::string_view msg) {
            fmt::print("{}\n", msg);
        };

        fmt::print("Fetching '{}'\n", name);

        fetcher->Fetch(cb);
    };

    if (options.all) {
        std::vector<std::string> remotes;

        repo.ListRemotes([&](const RemoteInfo& remote) { remotes.emplace_back(remote.name); });

        for (const auto& name : remotes) {
            fetch_from_remote(name, repo.GetRemoteFetcher(name));
        }
    } else if (options.remote.size()) {
        if (auto fetcher = repo.GetRemoteFetcher(options.remote)) {
            fetch_from_remote(options.remote, std::move(fetcher));
        } else {
            fmt::print(stderr, "error: unknown remote '{}'\n", options.remote);
            return 1;
        }
    }

    return 0;
}

} // namespace

int ExecuteFetch(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"all", "fetch from all remotes", cxxopts::value(options.all)},
                {"args", "paths to show", cxxopts::value<std::vector<std::string>>()},
            }
        );

        spec.parse_positional("args");
        spec.custom_help("[<options>]");
        spec.positional_help("[<remote> [<branch>...]]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.has("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
        if (opts.has("args")) {
            const auto& args = opts["args"].as<std::vector<std::string>>();

            options.remote = args[0];
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
