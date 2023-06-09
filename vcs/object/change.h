#pragma once

#include "hashid.h"
#include "path.h"

#include <string>

namespace Vcs {

struct Modifications {
    /// Attributes of an entry were changed.
    bool attributes = false;
    /// Content of an entry was changed.
    bool content = false;
    /// Type of an entry was changed.
    bool type = false;

    explicit constexpr operator bool() const noexcept {
        return attributes || content || type;
    }
};

struct CommitPath {
    /// Identifier of the commit object.
    HashId id = HashId();
    /// Path of the entry in the commit.
    std::string path;
};

struct Change {
    /// Performed actions.
    PathAction action = PathAction::None;
    /// Modification flags.
    Modifications flags;
    /// Type of resultant entry.
    PathType type = PathType::Unknown;
    /// Path of changed entry.
    std::string path;
    /// Source of copied entry.
    CommitPath source;
};

Modifications CompareEntries(const PathEntry& x, const PathEntry& y) noexcept;

} // namespace Vcs
