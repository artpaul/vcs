#include "printer.h"

#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>
#include <contrib/libgit2/include/git2.h>

namespace Vcs {
namespace {

struct Params {
    FILE* output;
    ColorMode mode;
};

std::pair<std::string_view, std::string_view> SplitHunk(const std::string_view hunk) {
    if (hunk.size() > 2 && (hunk[0] == '@' && hunk[1] == '@')) {
        const auto pos = hunk.find("@@", 2);

        if (pos != std::string_view::npos) {
            return std::make_pair(hunk.substr(0, pos + 2), hunk.substr(pos + 2));
        }
    }

    return std::make_pair(hunk, std::string_view());
}

std::pair<std::string_view, std::string_view> SplitEol(const std::string_view line) {
    const auto pos = line.find_last_of('\n');

    if (pos != std::string_view::npos) {
        return std::make_pair(line.substr(0, pos), line.substr(pos));
    }

    return std::make_pair(line, std::string_view());
}

int DoHunk(const git_diff_delta*, const git_diff_hunk* hunk, void* payload) {
    Params* const params = static_cast<Params*>(payload);

    const auto style = [&]() {
        if (IsColored(params->mode, params->output)) {
            return fmt::fg(fmt::terminal_color::cyan);
        }
        return fmt::text_style();
    };

    const auto [first, second] = SplitHunk(std::string_view(hunk->header, hunk->header_len));

    fmt::print(params->output, "{}{}", fmt::styled(first, style()), second);

    return 0;
}

int DoLine(const git_diff_delta*, const git_diff_hunk*, const git_diff_line* line, void* payload) {
    Params* const params = static_cast<Params*>(payload);

    const auto line_style = [&]() {
        if (IsColored(params->mode, params->output)) {
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
            fmt::print(params->output, " {}", std::string_view(line->content, line->content_len));
            break;
        case GIT_DIFF_LINE_ADDITION:
        case GIT_DIFF_LINE_DELETION: {
            const auto sign = line->origin == GIT_DIFF_LINE_ADDITION ? '+' : '-';
            const auto style = line_style();

            if (style.has_foreground()) {
                const auto [first, second] = SplitEol(std::string_view(line->content, line->content_len));

                fmt::print(
                    params->output, "{}{}", fmt::styled(fmt::format("{}{}", sign, first), style), second
                );
            } else {
                fmt::print(
                    params->output, "{}{}", sign, std::string_view(line->content, line->content_len)
                );
            }
            break;
        }
        case GIT_DIFF_LINE_ADD_EOFNL:
        case GIT_DIFF_LINE_CONTEXT_EOFNL:
        case GIT_DIFF_LINE_DEL_EOFNL:
            fmt::print(params->output, "{}", std::string_view(line->content, line->content_len));
            break;
        default:
            break;
    }

    return 0;
} // namespace

} // namespace

void Printer::Print(FILE* output) {
    git_diff_options options{};
    git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);

    options.context_lines = context_lines_;
    options.flags = GIT_DIFF_NORMAL;

    Params params{.output = output, .mode = color_mode_};
    git_diff_buffers(
        a_.data(), a_.size(), "a", b_.data(), b_.size(), "b", &options, nullptr, nullptr, DoHunk, DoLine,
        &params
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

void PrintHeader(const Change& change, const ColorMode coloring) {
    const auto style = [&]() {
        if (IsColored(coloring, stdout)) {
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

void PrintHeader(const PathStatus& status, const ColorMode coloring) {
    const auto style = [&]() {
        if (IsColored(coloring, stdout)) {
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
