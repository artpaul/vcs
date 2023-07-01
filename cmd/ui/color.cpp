#include "color.h"
#include "pager.h"

#include <util/tty.h>

namespace Vcs {

bool IsColored(const ColorMode mode, FILE* output) noexcept {
    if (mode == ColorMode::Always) {
        return true;
    }
    if (mode == ColorMode::Auto && (IsAtty(output) || PagerInUse())) {
        return true;
    }
    return false;
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
