#pragma once

#include <vcs/object/hashid.h>

namespace Vcs::Fs {

/// Mutable file.
#define S_IFMUT (1u << 31)

/// Check if an entry is mutable.
#define S_ISMUT(mode) (((mode) & (S_IFMUT)) != 0)

struct Meta {
    HashId id;
    /// Type and protection.
    mode_t mode;
    /// Total size in bytes.
    off_t size;
    /// Time of last status change.
    timespec ctime;
    /// Time of last modification.
    timespec mtime;
};

struct Timestamps {
    /// Time of last status change.
    timespec ctime;
    /// Time of last modification.
    timespec mtime;
};

static_assert(sizeof(Meta) == 64);

static_assert(sizeof(Timestamps) == 32);

} // namespace Vcs::Fs
