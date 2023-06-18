#pragma once

#include "path.h"

#include <vcs/object/change.h>
#include <vcs/object/object.h>
#include <vcs/object/path.h>

#include <functional>
#include <vector>

namespace Vcs {

class Datastore;

class ChangelistBuilder {
public:
    ChangelistBuilder(const Datastore& odb, std::function<void(Change)> cb);
    ChangelistBuilder(const Datastore& odb, std::vector<Change>& changes);

    void Changes(const HashId& from, const HashId& to);

    ChangelistBuilder& SetEmitDirectoryChanged(bool value) noexcept;

    ChangelistBuilder& SetExpandAdded(bool value) noexcept;

    ChangelistBuilder& SetExpandDeleted(bool value) noexcept;

    ChangelistBuilder& SetInclude(PathFilter value) noexcept;

private:
    void EmitAdd(const std::string& path, const PathType type);

    void EmitChange(const std::string& path, const PathType type, const Modifications flags);

    void EmitDelete(const std::string& path, const PathType type);

    void ProcessAdded(const std::string& path, const Tree::Entry to);

    void ProcessChanged(const std::string& path, const Tree::Entry from, const Tree::Entry to);

    void ProcessDeleted(const std::string& path, const Tree::Entry from);

    /**
     * Emits changes between two give subtrees.
     */
    void TreeChanges(const std::string& path, const Tree& from, const Tree& to);

private:
    const Datastore& odb_;
    std::function<void(Change)> cb_;
    PathFilter filter_;
    /// Emit change event for directories.
    bool emit_directory_changed_ = false;
    /// Expand content of created directories.
    bool expand_added_ = true;
    /// Expand content of deleted directories.
    bool expand_deleted_ = true;
};

} // namespace Vcs
