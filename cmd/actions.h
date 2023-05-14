#pragma once

#include <string_view>

namespace Vcs {

enum class Action {
    Unknown = 0,

    /// Remove untracked files from the working tree.
    Clean,
    /// Record changes to the repository.
    Commit,
    /// Create an empty repository.
    Init,
    /// Show commit log.
    Log,
    /// Remove files from the working tree.
    Remove,
    /// Restore working tree files.
    Restore,
    /// Show various type of objects.
    Show,
    /// Show working tree status.
    Status,
    /// Switch branches.
    Switch,
    /// Mangage multiple working spaces.
    Workspace,

    Dump,
    Git,
};

Action ParseAction(const std::string_view name) noexcept;

} // namespace Vcs
