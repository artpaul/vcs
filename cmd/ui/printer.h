#pragma once

#include "colors.h"

#include <cmd/local/status.h>
#include <vcs/object/change.h>

namespace Vcs {

class Printer {
public:
    Printer& SetA(const std::string_view value) noexcept {
        a_ = value;
        return *this;
    }

    Printer& SetB(const std::string_view value) noexcept {
        b_ = value;
        return *this;
    }

    Printer& SetColorMode(const ColorMode value) noexcept {
        color_mode_ = value;
        return *this;
    }

    Printer& SetContexLines(const size_t value) noexcept {
        context_lines_ = value;
        return *this;
    }

    void Print(FILE* output);

private:
    std::string_view a_;
    std::string_view b_;
    size_t context_lines_{3};
    ColorMode color_mode_{ColorMode::Auto};
};

std::string_view PathTypeToMode(const PathType type) noexcept;

void PrintHeader(const Change& change);

void PrintHeader(const PathStatus& status);

} // namespace Vcs
