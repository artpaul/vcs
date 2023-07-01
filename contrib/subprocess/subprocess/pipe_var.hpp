#pragma once

#include "basic_types.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <variant>

namespace subprocess {

enum class PipeVarIndex {
  option,
  string,
  handle,
  istream,
  ostream,
  file
};

using PipeVar = std::variant<PipeOption,
                             std::string,
                             PipeHandle,
                             std::istream*,
                             std::ostream*,
                             FILE*>;

inline PipeOption get_pipe_option(const PipeVar& option) {
  PipeVarIndex index = static_cast<PipeVarIndex>(option.index());

  switch (index) {
    case PipeVarIndex::option:
      return std::get<PipeOption>(option);
    case PipeVarIndex::handle:
      return PipeOption::specific;
    default:
      return PipeOption::pipe;
  }
}

} // namespace subprocess
