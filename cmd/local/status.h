#pragma once

#include <vcs/changes/path.h>
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

    PathStatus& SetEntry(const PathEntry& value) noexcept {
        entry = value;
        return *this;
    }

    PathStatus& SetEntry(const std::optional<PathEntry>& value) noexcept {
        entry = value;
        return *this;
    }

    PathStatus& SetPath(std::string value) noexcept {
        path = std::move(value);
        return *this;
    }

    PathStatus& SetStatus(const Status value) noexcept {
        status = value;
        return *this;
    }

    PathStatus& SetType(const PathType value) noexcept {
        type = value;
        return *this;
    }
};

struct StatusOptions {
    /// Emit ignored items.
    bool ignored = false;
    /// Emit modifications of tracked items.
    bool tracked = true;
    /// Emit untracked items.
    Expansion untracked = Expansion::Normal;
    /// Paths to include.
    PathFilter include;

    StatusOptions& SetIgnored(bool value) noexcept {
        ignored = value;
        return *this;
    }

    StatusOptions& SetInclude(PathFilter value) noexcept {
        include = std::move(value);
        return *this;
    }

    StatusOptions& SetTracked(bool value) noexcept {
        tracked = value;
        return *this;
    }

    StatusOptions& SetUntracked(Expansion value) noexcept {
        untracked = value;
        return *this;
    }
};

/**
 * @returns true to continue processing or false to stop.
 */
using StatusCallback = std::function<void(const PathStatus& status)>;

} // namespace Vcs
