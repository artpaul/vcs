#include <cmd/local/workspace.h>
#include <cmd/ui/color.h>
#include <vcs/object/commit.h>

#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>

#include <ctime>
#include <limits>

namespace Vcs {
namespace {

struct Options {
    HashId head{};
    /// Limit output by specific path.
    std::string path;
    /// Maximum number of commits to output.
    uint64_t count = std::numeric_limits<uint64_t>::max();
    /// Coloring mode.
    ColorMode coloring = ColorMode::Auto;
    /// Use only one line for each log entry.
    bool oneline = false;
};

int Execute(const Options& options, const Workspace& repo) {
    uint64_t count = 0u;

    const auto head_style = [&] {
        if (IsColored(options.coloring, stdout)) {
            return fmt::fg(fmt::terminal_color::yellow);
        } else {
            return fmt::text_style();
        }
    };

    const auto date_string = [](const std::time_t ts) {
        char buf[64];

        return std::string(buf, std::strftime(buf, sizeof(buf), "%c %z", std::localtime(&ts)));
    };

    const auto print_commit = [&](const HashId& id, const Commit& c) {
        // Header.
        fmt::print("{}\n", fmt::styled(fmt::format("commit {}", id), head_style()));
        // Author.
        if (const auto author = c.Author()) {
            fmt::print(
                "Author: {}{}\n",
                // Name.
                author.Name(),
                // Login or email.
                author.Id().empty() ? "" : fmt::format(" <{}>", author.Id())
            );
        }
        // Date.
        fmt::print("Date:   {}\n", date_string(c.Timestamp()));
        // Message.
        if (const auto& lines = MessageLines(c.Message()); !lines.empty()) {
            fmt::print("\n");
            for (const auto& line : lines) {
                fmt::print("    {}\n", line);
            }
        }
    };

    const auto cb = [&](const HashId& id, const Commit& c) {
        ++count;

        if (options.oneline) {
            fmt::print("{} {}\n", fmt::styled(id.ToHex(), head_style()), MessageTitle(c.Message()));
        } else {
            if (count > 1) {
                fmt::print("\n");
            }

            print_commit(id, c);
        }

        return options.count > count;
    };

    if (options.path.empty()) {
        repo.Log(LogOptions().Push(options.head), cb);
    } else {
        repo.PathLog(
            LogOptions().Push(options.head), options.path,
            [&](const HashId& id, const std::string_view, const Commit& c) { return cb(id, c); }
        );
    }

    return 0;
}

} // namespace

int ExecuteLog(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"n", "number of commits to output", cxxopts::value<uint64_t>(options.count)},
                {"oneline", "one commit per line", cxxopts::value<bool>(options.oneline)},
                {"color", "coloring mode [always|auto|none]", cxxopts::value<std::string>(), "<mode>"},
                {"args", "free args", cxxopts::value<std::vector<std::string>>()},
            }
        );

        spec.parse_positional("args");
        spec.custom_help("[<options>]");
        spec.positional_help("[<revision>] [[--] <path>]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.count("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
        if (opts.has("args")) {
            const auto& args = opts["args"].as<std::vector<std::string>>();
            const auto& repo = cb();
            size_t i = 0;
            // Try to parse the first argument as a reference.
            if (const auto id = repo.ResolveReference(args[i])) {
                ++i;
                options.head = *id;
            }
            // Try to parse the current argument as a path.
            if (i < args.size() && repo.HasPath(repo.GetCurrentHead(), args[i])) {
                options.path = args[i];
            }

            if (!bool(options.head) && options.path.empty()) {
                fmt::print(
                    stderr,
                    "error: ambiguous argument '{}': unknown revision or path not in the working tree.\n",
                    args[0]
                );
                return 1;
            }
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

        if (!bool(options.head)) {
            options.head = cb().GetCurrentHead();
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
