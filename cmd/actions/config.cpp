#include <cmd/local/workspace.h>
#include <vcs/common/config.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>

namespace Vcs {
namespace {

struct Options {
    std::string key;
    std::string value;
    /// Location.
    std::optional<ConfigLocation> location;
    /// Add or update value.
    bool add = false;
    /// Get value by name.
    bool get = false;
    /// Remove a variable.
    bool unset = false;
};

int ExecuteGet(const Options& options, const Workspace& repo) {
    const auto value = options.location ? repo.GetConfig().Get(options.key, *options.location)
                                        : repo.GetConfig().Get(options.key);

    if (value) {
        fmt::print("{}\n", value->dump());
        return 0;
    } else {
        fmt::print(stderr, "error: no key '{}'\n", options.key);
        return 1;
    }
}

} // namespace

int ExecuteConfig(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
            }
        );
        spec.add_options(
            "Config file location",
            {
                {"local", "use repository config file", cxxopts::value<bool>()},
                {"user", "use user config file", cxxopts::value<bool>()},
                {"workspace", "use per-workspace config file", cxxopts::value<bool>()},
            }
        );
        spec.add_options(
            "Actions",
            {
                {"add", "add a new value", cxxopts::value(options.add)},
                {"get", "get value", cxxopts::value(options.get)},
                {"unset", "remove a value", cxxopts::value(options.unset)},
                {"args", "args", cxxopts::value<std::vector<std::string>>()},
            }
        );

        spec.parse_positional("args");
        spec.custom_help("[<options>]");
        spec.positional_help("[<key> [<value>]]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.count("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
        if (opts.has("args")) {
            const auto args = opts["args"].as<std::vector<std::string>>();
            // Key.
            options.key = args[0];
            // Value.
            if (args.size() > 1) {
                options.value = args[1];
            }
        } else {
            fmt::print("{}\n", spec.help());
            return 1;
        }
        // Parse locations.
        if (opts.has("local")) {
            options.location = ConfigLocation::Repository;
        }
        if (opts.has("user")) {
            options.location = ConfigLocation::User;
        }
        if (opts.has("workspace")) {
            options.location = ConfigLocation::Workspace;
        }
    }

    return ExecuteGet(options, cb());
}

} // namespace Vcs
