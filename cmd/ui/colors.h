#pragma once

namespace Vcs {

enum class ColorMode {
    /// No colored output.
    None,
    /// Print colored only if target device is terminal.
    Auto,
    /// Always emit colored output.
    Always,
};

} // namespace Vcs
