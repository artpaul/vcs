#pragma once

#include <string_view>

namespace Vcs {

enum class Action {
    Unknown = 0,

    /// List, create, or delete branches.
    Branch,
    /// Remove untracked files from the working tree.
    Clean,
    /// Record changes to the repository.
    Commit,
    /// Get or set repository or global options.
    Config,
    /// Show changes between commits, commit and working tree, etc.
    Diff,
    /// Download objects and refs from another repository.
    Fetch,
    /// Create an empty repository.
    Init,
    /// Show commit log.
    Log,
    /// Manage set of tracked repositories.
    Remote,
    /// Remove files from the working tree.
    Remove,
    /// Reset current HEAD to the specified state.
    Reset,
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

    /// Dump various internal info.
    Dump,
    /// Set of tools to interact with git repositories.
    Git,
};

Action ParseAction(const std::string_view name) noexcept;

} // namespace Vcs
