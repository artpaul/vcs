#include <cmd/local/workspace.h>

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
    /// Number of context lines in output.
    size_t context_lines = 3;
};

constexpr std::string_view PathTypeToMode(const PathType type) noexcept {
    switch (type) {
        case PathType::Unknown:
            return "0000000";
        case PathType::File:
            return "0100644";
        case PathType::Directory:
            return "0040000";
        case PathType::Executible:
            return "0100755";
        case PathType::Symlink:
            return "0120000";
    }
    return "0000000";
}

class Printer {
public:
    explicit Printer(FILE* out) noexcept
        : output_(out) {
    }

    Printer& SetA(const std::string_view value) noexcept {
        a_ = value;
        return *this;
    }

    Printer& SetB(const std::string_view value) noexcept {
        b_ = value;
        return *this;
    }

    Printer& SetContexLines(const size_t value) noexcept {
        context_lines_ = value;
        return *this;
    }

    void Print() {
        git_diff_options options{};
        git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);

        options.context_lines = context_lines_;
        options.flags = GIT_DIFF_NORMAL;

        git_diff_buffers(
            a_.data(), a_.size(), "a", b_.data(), b_.size(), "b", &options, nullptr, nullptr, DoHunk,
            DoLine, this
        );
    }

private:
    static int DoHunk(const git_diff_delta*, const git_diff_hunk* hunk, void* payload) {
        FILE* const output = static_cast<Printer*>(payload)->output_;

        const auto style = [&]() {
            if (util::is_atty(output)) {
                return fmt::fg(fmt::terminal_color::cyan);
            }
            return fmt::text_style();
        };

        fmt::print(output, style(), "{}", std::string_view(hunk->header, hunk->header_len));

        return 0;
    }

    static int DoLine(
        const git_diff_delta*, const git_diff_hunk*, const git_diff_line* line, void* payload
    ) {
        FILE* const output = static_cast<Printer*>(payload)->output_;

        const auto style = [&]() {
            if (util::is_atty(output)) {
                if (line->origin == GIT_DIFF_LINE_ADDITION) {
                    return fmt::fg(fmt::terminal_color::green);
                }
                if (line->origin == GIT_DIFF_LINE_DELETION) {
                    return fmt::fg(fmt::terminal_color::red);
                }
            }
            return fmt::text_style();
        };

        switch (line->origin) {
            case GIT_DIFF_LINE_CONTEXT:
                fmt::print(output, " {}", std::string_view(line->content, line->content_len));
                break;
            case GIT_DIFF_LINE_ADDITION:
                fmt::print(output, style(), "+{}", std::string_view(line->content, line->content_len));
                break;
            case GIT_DIFF_LINE_DELETION:
                fmt::print(output, style(), "-{}", std::string_view(line->content, line->content_len));
                break;
            case GIT_DIFF_LINE_ADD_EOFNL:
            case GIT_DIFF_LINE_CONTEXT_EOFNL:
            case GIT_DIFF_LINE_DEL_EOFNL:
                fmt::print(output, "{}", std::string_view(line->content, line->content_len));
                break;
            default:
                break;
        }

        return 0;
    }

private:
    FILE* output_;
    std::string_view a_;
    std::string_view b_;
    size_t context_lines_{3};
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

void PrintBlob(const PathStatus& status, const Workspace& repo, size_t lines) {
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

    Printer(stdout).SetA(a).SetB(b).SetContexLines(lines).Print();
}

void PrintHeader(const PathStatus& status) {
    const auto style = [&]() {
        if (util::is_atty(stdout)) {
            return fmt::text_style(fmt::emphasis::bold);
        }
        return fmt::text_style();
    };

    if (status.status == PathStatus::Deleted) {
        fmt::print(
            "{}\n", fmt::styled(fmt::format("diff --git a/{} b/{}", status.path, status.path), style())
        );
        fmt::print(
            "{}\n", fmt::styled(fmt::format("deleted file mode {}", PathTypeToMode(status.type)), style())
        );
        fmt::print("{}\n", fmt::styled(fmt::format("index {}..{}", status.entry->id, HashId()), style()));
        fmt::print("{}\n", fmt::styled(fmt::format("--- a/{}", status.path), style()));
        fmt::print("{}\n", fmt::styled("+++ /dev/null", style()));
        return;
    }

    if (status.status == PathStatus::Modified) {
        fmt::print(
            "{}\n", fmt::styled(fmt::format("diff --git a/{} b/{}", status.path, status.path), style())
        );
        fmt::print(
            "{}\n",
            fmt::styled(
                fmt::format("index {}..{} {}", status.entry->id, HashId(), PathTypeToMode(status.type)),
                style()
            )
        );
        fmt::print("{}\n", fmt::styled(fmt::format("--- a/{}", status.path), style()));
        fmt::print("{}\n", fmt::styled(fmt::format("+++ b/{}", status.path), style()));
        return;
    }
}

void PrintCurrentChanges(const Options& options, const Workspace& repo) {
    repo.Status(StatusOptions().SetInclude(PathFilter(options.paths)), [&](const PathStatus& status) {
        if (status.type != PathType::File) {
            return;
        }
        if (status.status == PathStatus::Deleted || status.status == PathStatus::Modified) {
            PrintHeader(status);
            PrintBlob(status, repo, options.context_lines);
        }
    });
}

int Execute(const Options& options, const Workspace& repo) {
    git_libgit2_init();
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
