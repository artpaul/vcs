#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace Vcs {

class CommitBuilder;
class Datastore;

uint64_t GetLargestGeneration(const CommitBuilder& commit, const Datastore& odb);

std::vector<std::string_view> MessageLines(const std::string_view msg);

std::string_view MessageTitle(const std::string_view msg) noexcept;

} // namespace Vcs
