#pragma once

#include <optional>
#include <string_view>

namespace Vcs {

enum class ColorMode {
    /// No colored output.
    None,
    /// Print colored only if target device is terminal.
    Auto,
    /// Always emit colored output.
    Always,
};

bool IsColored(const ColorMode mode, FILE* output) noexcept;

std::optional<ColorMode> ParseColorMode(const std::string_view value);

} // namespace Vcs
