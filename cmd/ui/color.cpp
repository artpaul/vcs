#include "color.h"

#include <util/tty.h>

namespace Vcs {

bool IsColored(const ColorMode mode, FILE* output) {
    return (mode == ColorMode::Always) || (mode == ColorMode::Auto && util::is_atty(output));
}

std::optional<ColorMode> ParseColorMode(const std::string_view value) {
    if (value == "always") {
        return ColorMode::Always;
    }
    if (value == "auto") {
        return ColorMode::Auto;
    }
    if (value == "none") {
        return ColorMode::None;
    }
    return {};
}

} // namespace Vcs
