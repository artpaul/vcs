#include <cmd/local/workspace.h>
#include <vcs/object/commit.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>

#include <functional>
#include <vector>

namespace Vcs {
namespace {

struct Options {
    /// Commit id to set.
    HashId id;
    /// Reset mode.
    ResetMode mode = ResetMode::None;
};

int Execute(const Options& options, Workspace& repo) {
    const auto branch = repo.GetCurrentBranch();

    // Nothing to do if HEAD already at the given id.
    if (branch.head == options.id) {
        if (options.mode == ResetMode::Soft) {
            fmt::print("Already at {}\n", options.id);
            return 0;
        }
    }

    if (repo.Reset(options.mode, options.id)) {
        const auto commit = repo.Objects().LoadCommit(options.id);

        fmt::print("HEAD is now at {} {}\n", options.id, MessageTitle(commit.Message()));
    } else {
        fmt::print(stderr, "error: cannot reset '{}' to {}\n", branch.name, options.id);
        return 1;
    }

    return 0;
}

} // namespace

int ExecuteReset(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"soft", "reset only HEAD", cxxopts::value<bool>()},
                {"hard", "reset HEAD and working tree", cxxopts::value<bool>()},
                {"commit", "commit to set", cxxopts::value<std::string>()},
            }
        );
        spec.parse_positional("commit");
        spec.custom_help("[<options>]");
        spec.positional_help("<commit-ish>");

        const auto& opts = spec.parse(argc, argv);
        if (opts.has("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }

        if (opts.has("soft")) {
            options.mode = ResetMode::Soft;
        }
        if (opts.has("hard")) {
            options.mode = ResetMode::Hard;
        }

        if (opts.has("commit")) {
            const auto arg = opts["commit"].as<std::string>();

            if (const auto id = cb().ResolveReference(arg)) {
                options.id = *id;
            } else {
                fmt::print(stderr, "error: cannot resolve reference '{}'\n", arg);
                return 1;
            }
        } else {
            options.id = cb().GetCurrentHead();
        }

        if (options.mode == ResetMode::None) {
            options.mode = ResetMode::Soft;
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
