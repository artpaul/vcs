#include <cmd/local/workspace.h>
#include <cmd/ui/pager.h>
#include <cmd/ui/printer.h>

#include <util/file.h>
#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>
#include <contrib/libgit2/include/git2.h>

#include <functional>
#include <vector>

namespace Vcs {
namespace {

struct Options {
    /// Paths to show.
    std::vector<std::string> paths;
    /// Coloring mode.
    ColorMode coloring = ColorMode::Auto;
    /// Number of context lines in output.
    size_t context_lines = 3;
};

std::string BlobFromFile(const std::filesystem::path& path) {
    std::string blob;
    char buf[16 << 10];
    File file = File::ForRead(path);
    while (size_t len = file.Read(buf, sizeof(buf))) {
        blob.append(buf, len);
    }
    return blob;
}

void PrintBlob(const Options& options, const Workspace& repo, const PathStatus& status) {
    std::string a;
    std::string b;

    if (status.status == PathStatus::Deleted) {
        a = repo.Objects().LoadBlob(status.entry->id);
    } else if (status.status == PathStatus::Modified) {
        a = repo.Objects().LoadBlob(status.entry->id);
        b = BlobFromFile(repo.ToAbsolutePath(status.path));
    } else {
        return;
    }

    Printer()
        .SetA(a)
        .SetB(b)
        .SetColorMode(options.coloring)
        .SetContexLines(options.context_lines)
        .Print(stdout);
}

void PrintCurrentChanges(const Options& options, const Workspace& repo) {
    repo.Status(StatusOptions().SetInclude(PathFilter(options.paths)), [&](const PathStatus& status) {
        if (status.type != PathType::File) {
            return;
        }
        if (status.status == PathStatus::Deleted || status.status == PathStatus::Modified) {
            PrintHeader(status, options.coloring);
            PrintBlob(options, repo, status);
        }
    });
}

int Execute(const Options& options, const Workspace& repo) {
    git_libgit2_init();
    // Pager.
    SetupPager(repo.GetConfig());
    // Changes between HEAD and state of the working tree.
    PrintCurrentChanges(options, repo);
    // Shutdown libgit2.
    git_libgit2_shutdown();
    return 0;
}

} // namespace

int ExecuteDiff(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"color", "coloring mode [always|auto|none]", cxxopts::value<std::string>(), "<mode>"},
                {"U,unified", "generate diffs with <n> lines", cxxopts::value(options.context_lines)},
                {"paths", "path to show", cxxopts::value<std::vector<std::string>>()},
            }
        );
        spec.parse_positional({"paths"});
        spec.custom_help("[<options>]");
        spec.positional_help("[<commit> [<commit>]] [[--] <path>...]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.count("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
        if (opts.has("color")) {
            const auto arg = opts["color"].as<std::string>();

            if (auto coloring = ParseColorMode(arg)) {
                options.coloring = *coloring;
            } else {
                fmt::print(stderr, "error: unknown coloring mode '{}'\n", arg);
                return 1;
            }
        }
        if (opts.has("paths")) {
            const auto& paths = opts["paths"].as<std::vector<std::string>>();
            const auto& repo = cb();

            for (const auto& path : paths) {
                options.paths.push_back(repo.ToTreePath(path));
            }
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
