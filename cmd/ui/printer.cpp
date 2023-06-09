#include "printer.h"

#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>
#include <contrib/libgit2/include/git2.h>

namespace Vcs {
namespace {

int DoHunk(const git_diff_delta*, const git_diff_hunk* hunk, void* payload) {
    FILE* const output = static_cast<FILE*>(payload);

    const auto style = [&]() {
        if (util::is_atty(output)) {
            return fmt::fg(fmt::terminal_color::cyan);
        }
        return fmt::text_style();
    };

    fmt::print(output, style(), "{}", std::string_view(hunk->header, hunk->header_len));

    return 0;
}

int DoLine(const git_diff_delta*, const git_diff_hunk*, const git_diff_line* line, void* payload) {
    FILE* const output = static_cast<FILE*>(payload);

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

} // namespace

void Printer::Print(FILE* output) {
    git_diff_options options{};
    git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);

    options.context_lines = context_lines_;
    options.flags = GIT_DIFF_NORMAL;

    git_diff_buffers(
        a_.data(), a_.size(), "a", b_.data(), b_.size(), "b", &options, nullptr, nullptr, DoHunk, DoLine,
        output
    );
}

std::string_view PathTypeToMode(const PathType type) noexcept {
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

void PrintHeader(const Change& change) {
    const auto style = []() {
        if (util::is_atty(stdout)) {
            return fmt::text_style(fmt::emphasis::bold);
        }
        return fmt::text_style();
    };

    if (change.action == PathAction::Add) {
        fmt::print(
            "{}\n", fmt::styled(fmt::format("diff --git a/{} b/{}", change.path, change.path), style())
        );
        fmt::print(
            "{}\n", fmt::styled(fmt::format("new file mode {}", PathTypeToMode(change.type)), style())
        );
        // fmt::print("{}\n", fmt::styled(fmt::format("index {}..{}", change.entry->id, HashId()),
        // style()));
        fmt::print("{}\n", fmt::styled(fmt::format("--- /dev/null"), style()));
        fmt::print("{}\n", fmt::styled(fmt::format("+++ b/{}", change.path), style()));
        return;
    }

    if (change.action == PathAction::Change) {
        fmt::print(
            "{}\n", fmt::styled(fmt::format("diff --git a/{} b/{}", change.path, change.path), style())
        );
        // fmt::print(
        //     "{}\n",
        //     fmt::styled(
        //         fmt::format("index {}..{} {}", status.entry->id, HashId(), PathTypeToMode(change.type)),
        //         style()
        //     )
        // );
        fmt::print("{}\n", fmt::styled(fmt::format("--- a/{}", change.path), style()));
        fmt::print("{}\n", fmt::styled(fmt::format("+++ b/{}", change.path), style()));
        return;
    }

    if (change.action == PathAction::Delete) {
        fmt::print(
            "{}\n", fmt::styled(fmt::format("diff --git a/{} b/{}", change.path, change.path), style())
        );
        fmt::print(
            "{}\n", fmt::styled(fmt::format("deleted file mode {}", PathTypeToMode(change.type)), style())
        );
        // fmt::print("{}\n", fmt::styled(fmt::format("index {}..{}", change.entry->id, HashId()),
        // style()));
        fmt::print("{}\n", fmt::styled(fmt::format("--- a/{}", change.path), style()));
        fmt::print("{}\n", fmt::styled("+++ /dev/null", style()));
        return;
    }
}

void PrintHeader(const PathStatus& status) {
    const auto style = []() {
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

} // namespace Vcs
