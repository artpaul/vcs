#pragma once

#include <vcs/object/path.h>

#include <functional>
#include <optional>

namespace Vcs {

enum class Expansion : uint8_t {
    None = 0,
    Normal = 1,
    All = 2,
};

struct PathStatus {
    enum Status {
        /// Path is untracked.
        Untracked = 0,
        /// Path has been added.
        Added,
        /// Path is deleted.
        Deleted,
        /// Path is ignored.
        Ignored,
        /// Path has been modified.
        Modified,
    };

    /// Status of the item.
    Status status = Untracked;
    /// Type of the path item.
    PathType type = PathType::Unknown;
    /// Paht of the item.
    std::string path;
    /// Entry from base tree.
    std::optional<PathEntry> entry;

    PathStatus& SetPath(std::string value) {
        path = std::move(value);
        return *this;
    }

    PathStatus& SetStatus(const Status value) {
        status = value;
        return *this;
    }

    PathStatus& SetType(const PathType value) {
        type = value;
        return *this;
    }
};

struct StatusOptions {
    /// Emit ignored items.
    Expansion ignored = Expansion::None;
    /// Emit modifications of tracked items.
    bool tracked = true;
    /// Emit untracked items.
    Expansion untracked = Expansion::Normal;

    StatusOptions& SetIgnored(Expansion value) {
        ignored = value;
        return *this;
    }

    StatusOptions& SetTracked(bool value) {
        tracked = value;
        return *this;
    }

    StatusOptions& SetUntracked(Expansion value) {
        untracked = value;
        return *this;
    }
};

/**
 * @returns true to continue processing or false to stop.
 */
using StatusCallback = std::function<void(const PathStatus& status)>;

} // namespace Vcs
