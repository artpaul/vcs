#include "commit.h"

#include "serialize.h"
#include "store.h"

namespace Vcs {

uint64_t GetLargestGeneration(const CommitBuilder& builder, const Datastore* odb) {
    uint64_t generation = 0;
    // Parents.
    for (const auto& id : builder.parents) {
        generation = std::max(generation, odb->LoadCommit(id).Generation());
    }
    // Copies.
    if (builder.renames) {
        for (const auto& id : odb->LoadRenames(builder.renames).Commits()) {
            generation = std::max(generation, odb->LoadCommit(id).Generation());
        }
    }
    return generation;
}

} // namespace Vcs
