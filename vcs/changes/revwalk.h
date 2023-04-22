#pragma once

namespace Vcs {

enum class WalkAction {
    /// Move to next commit.
    Continue,
    /// Hide all ancestors of a commit.
    Hide,
    /// Stop iteration.
    Stop,
};

} // namespace Vcs
