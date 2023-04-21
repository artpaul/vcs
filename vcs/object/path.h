#pragma once

#include "data.h"
#include "hashid.h"

#include <cstdint>

namespace Vcs {

enum class PathAction : uint8_t {
    /// Nothing to do.
    None = 0,
    /// Add entry to a tree.
    Add = 1,
    /// Update entry.
    Change = 2,
    /// Remove entry from a tree.
    Delete = 3,
};

enum class PathType : uint8_t {
    /// Path type is unknown or unspecified.
    Unknown = 0,
    /// A regular file.
    File = 1,
    /// A directory.
    Directory = 2,
    /// A file with the execute permission.
    Executible = 3,
    /// A symbolic link.
    Symlink = 4,
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

inline constexpr bool IsFile(const PathType type) noexcept {
    return type == PathType::File || type == PathType::Executible;
}

inline constexpr bool IsDirectory(const PathType type) noexcept {
    return type == PathType::Directory;
}

inline constexpr bool IsSymlink(const PathType type) noexcept {
    return type == PathType::Symlink;
}

} // namespace Vcs
