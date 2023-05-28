#pragma once

#include "change.h"
#include "path.h"

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

    ChangelistBuilder& SetExpandDirectories(bool value) noexcept;

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
    /// Expand content created or deleted directories.
    bool expand_directories_ = true;
};

} // namespace Vcs
