#include <cmd/local/bare.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/format.h>

namespace Vcs {
namespace {

struct Options {
    std::filesystem::path path;
    /// Create a bare repository.
    bool bare = false;
};

int Execute(const Options& options) {
    if (options.bare) {
        // Just create a bare repository.
        Repository::Initialize(options.path);
    } else {
        const auto bare_path = options.path / ".vcs";

        // Create working area.
        std::filesystem::create_directories(options.path);
        // Create bare repository.
        Repository::Initialize(bare_path);
    }

    return 0;
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
        } else {
            options.path = std::filesystem::current_path();
        }
    }

    return Execute(options);
}

} // namespace Vcs
