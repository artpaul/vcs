#include <cmd/local/workspace.h>
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
    /// Maximum number of commits to output.
    uint64_t count = std::numeric_limits<uint64_t>::max();
    /// Use only one line for each log entry.
    bool oneline = false;
};

int Execute(const Options& options, const Workspace& repo) {
    uint64_t count = 0u;

    const auto head_style = [] {
        if (util::is_atty(stdout)) {
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
        const auto author = c.Author();
        const auto committer = c.Committer();

        // Header.
        fmt::print("{}\n", fmt::styled(fmt::format("commit {}", id), head_style()));
        // Author.
        if (author) {
            fmt::print(
                "Author: {}{}\n",
                // Name.
                author.Name(),
                // Login or email.
                author.Id().empty() ? "" : fmt::format(" <{}>", author.Id())
            );
        }
        // Committer.
        if (committer && author != committer) {
            fmt::print(
                "Committer: {}{}\n",
                // Name.
                committer.Name(),
                // Login or email.
                committer.Id().empty() ? "" : fmt::format(" <{}>", committer.Id())
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

    repo.Log(LogOptions().Push(options.head), [&](const HashId& id, const Commit& c) {
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
    });

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

            if (const auto id = repo.ResolveReference(args[0])) {
                options.head = *id;
            }
        }

        if (!bool(options.head)) {
            options.head = cb().GetCurrentHead();
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
