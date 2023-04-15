#pragma once

#include "data.h"
#include "hashid.h"

#include <cstdint>

namespace Vcs {

enum class PathType : uint8_t {
    /// A regular file.
    File = 0,
    /// A directory.
    Directory = 1,
    /// A file with the execute permission.
    Executible = 2,
    /// A symbolic link.
    Symlink = 3,
};

struct PathEntry {
    /// Identifier of the object.
    HashId id{};
    /// Type of the entry.
    PathType type{PathType::File};
    /// Size of the object.
    uint64_t size{0};
};

static_assert(sizeof(PathEntry) == 32);

/// Ensure the value of PathEntry is memcpy copyable.
static_assert(std::is_trivially_copyable<PathEntry>::value);

} // namespace Vcs
