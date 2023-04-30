#pragma once

#include <cstdint>

namespace Vcs {

class CommitBuilder;
class Datastore;

uint64_t GetLargestGeneration(const CommitBuilder& commit, const Datastore& odb);

} // namespace Vcs
