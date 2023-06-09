#include <cmd/local/workspace.h>
#include <cmd/ui/printer.h>
#include <vcs/changes/changelist.h>
#include <vcs/changes/path.h>
#include <vcs/changes/stage.h>
#include <vcs/object/commit.h>
#include <vcs/store/memory.h>

#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>
#include <contrib/libgit2/include/git2.h>

namespace Vcs {
namespace {

struct Options {
    HashId id{};
    std::vector<std::string> paths;
    /// Number of context lines in output.
    size_t context_lines = 3;
    bool name_only = false;
    bool name_status = false;
};

int ShowBlob(const Blob& blob) {
    // Disable output bufferization since the data is already in memory.
    std::setbuf(stdout, nullptr);
    // Write the blob.
    std::fwrite(blob.Data(), 1, blob.Size(), stdout);

    return 0;
}

int ShowCommit(const Options& options, const Commit& commit, const Datastore& odb) {
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

    // Header.
    fmt::print("{}\n", fmt::styled(fmt::format("commit {}", options.id), head_style()));
    // Author.
    if (const auto author = commit.Author()) {
        fmt::print(
            "Author: {}{}\n",
            // Name.
            author.Name(),
            // Login or email.
            author.Id().empty() ? "" : fmt::format(" <{}>", author.Id())
        );
    }
    // Date.
    fmt::print("Date:   {}\n", date_string(commit.Timestamp()));
    // Message.
    if (const auto& lines = MessageLines(commit.Message()); !lines.empty()) {
        fmt::print("\n");
        for (const auto& line : lines) {
            fmt::print("    {}\n", line);
        }
    }

    if (options.name_only || options.name_status) {
        const auto cb = [&, first = true](const Change& change) mutable {
            const auto status_char = [&]() {
                if (change.action == PathAction::Add) {
                    return 'A';
                } else if (change.action == PathAction::Change) {
                    return 'M';
                } else if (change.action == PathAction::Delete) {
                    return 'D';
                }
                return '?';
            };

            const auto status_style = [&]() {
                if (!util::is_atty(stdout)) {
                    return fmt::text_style();
                }
                if (change.action == PathAction::Add) {
                    return fmt::fg(fmt::terminal_color::green);
                } else if (change.action == PathAction::Change) {
                    return fmt::fg(fmt::terminal_color::yellow);
                } else if (change.action == PathAction::Delete) {
                    return fmt::fg(fmt::terminal_color::red);
                }
                return fmt::text_style();
            };

            if (first) {
                fmt::print("\n");
                first = false;
            }

            if (options.name_only) {
                fmt::print("{}{}\n", change.path, (change.type == PathType::Directory ? "/" : ""));
            } else {
                const auto line = fmt::format(
                    "{}   {}{}", status_char(), change.path, (change.type == PathType::Directory ? "/" : "")
                );

                fmt::print("{}\n", fmt::styled(line, status_style()));
            }
        };

        ChangelistBuilder(odb, cb)
            .SetExpandAdded(true)
            .SetExpandDeleted(true)
            .SetInclude(PathFilter(options.paths))
            .Changes(commit.Parents() ? commit.Parents()[0] : HashId(), options.id);
    } else {
        const auto from = commit.Parents() ? odb.Load(commit.Parents()[0]).AsCommit().Tree() : HashId();
        const auto to = options.id ? odb.Load(options.id).AsCommit().Tree() : HashId();
        auto stage_odb = odb.Cache(Store::MemoryCache::Make());

        const auto cb = [&, first = true](const Change& change) mutable {
            if (change.type != PathType::File) {
                return;
            }
            if (first) {
                fmt::print("\n");
                first = false;
            }

            PrintHeader(change);

            {
                std::string a;
                std::string b;

                if (change.action == PathAction::Add) {
                    b = odb.LoadBlob(StageArea(stage_odb, to).GetEntry(change.path)->id);
                } else if (change.action == PathAction::Change) {
                    a = odb.LoadBlob(StageArea(stage_odb, from).GetEntry(change.path)->id);
                    b = odb.LoadBlob(StageArea(stage_odb, to).GetEntry(change.path)->id);
                } else if (change.action == PathAction::Delete) {
                    a = odb.LoadBlob(StageArea(stage_odb, from).GetEntry(change.path)->id);
                } else {
                    return;
                }

                Printer().SetA(a).SetB(b).SetContexLines(options.context_lines).Print(stdout);
            }
        };

        git_libgit2_init();

        ChangelistBuilder(stage_odb, cb)
            .SetExpandAdded(true)
            .SetExpandDeleted(true)
            .SetInclude(PathFilter(options.paths))
            .Changes(commit.Parents() ? commit.Parents()[0] : HashId(), options.id);
        // Shutdown libgit2.
        git_libgit2_shutdown();
    }

    return 0;
}

int Execute(const Options& options, const Workspace& repo) {
    const auto obj = repo.Objects().Load(options.id);

    switch (obj.Type()) {
        case DataType::Blob:
            return ShowBlob(obj.AsBlob());
        case DataType::Commit:
            return ShowCommit(options, obj.AsCommit(), repo.Objects());
        case DataType::Index:
            return 1;
        case DataType::Tree:
        case DataType::Tag:
            return 1;
        case DataType::None:
        case DataType::Renames:
            return 1;
    }

    return 0;
}

} // namespace

int ExecuteShow(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"U,unified", "generate diffs with <n> lines", cxxopts::value(options.context_lines)},
                {"name-only", "show only names of changed files", cxxopts::value(options.name_only)},
                {"name-status", "show only names and status of changed files",
                 cxxopts::value(options.name_status)},
                {"args", "paths to show", cxxopts::value<std::vector<std::string>>()},
            }
        );

        spec.parse_positional("args");
        spec.custom_help("[<options>]");
        spec.positional_help("[<object>] [<path>...]");

        const auto& opts = spec.parse(argc, argv);
        if (opts.has("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
        if (opts.has("args")) {
            const auto& args = opts["args"].as<std::vector<std::string>>();
            const auto& repo = cb();

            if (const auto id = repo.ResolveReference(args[0])) {
                options.id = *id;
            }

            for (size_t i = (bool(options.id) ? 1 : 0), end = args.size(); i < end; ++i) {
                options.paths.push_back(repo.ToTreePath(args[i]));
            }
        }

        if (!bool(options.id)) {
            options.id = cb().GetCurrentHead();
        }
    }

    return Execute(options, cb());
}

} // namespace Vcs
