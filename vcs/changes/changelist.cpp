#include "changelist.h"
#include "path.h"
#include "stage.h"

#include <vcs/object/serialize.h>
#include <vcs/object/store.h>

namespace Vcs {
namespace {

inline Modifications CompareEntries(const Tree::Entry x, const Tree::Entry y) {
    return CompareEntries(static_cast<PathEntry>(x), static_cast<PathEntry>(y));
}

Tree GetRoot(const HashId& id, const Datastore& odb) {
    if (id) {
        return odb.LoadTree(GetTreeId(id, odb));
    } else {
        return Tree::Load(TreeBuilder().Serialize());
    }
}

std::string JoinPath(std::string path, const std::string_view name) {
    if (path.empty()) {
        return std::string(name);
    } else {
        path.append("/");
        path.append(name);
        return path;
    }
}

} // namespace

ChangelistBuilder::ChangelistBuilder(const Datastore& odb, std::function<void(Change)> cb)
    : odb_(odb)
    , cb_(std::move(cb)) {
    assert(cb_);
}

ChangelistBuilder::ChangelistBuilder(const Datastore& odb, std::vector<Change>& changes)
    : odb_(odb)
    , cb_([&changes](Change change) { changes.push_back(std::move(change)); }) {
}

ChangelistBuilder& ChangelistBuilder::SetEmitDirectoryChanged(bool value) noexcept {
    emit_directory_changed_ = value;
    return *this;
}

ChangelistBuilder& ChangelistBuilder::SetExpandAdded(bool value) noexcept {
    expand_added_ = value;
    return *this;
}

ChangelistBuilder& ChangelistBuilder::SetExpandDeleted(bool value) noexcept {
    expand_deleted_ = value;
    return *this;
}

ChangelistBuilder& ChangelistBuilder::SetInclude(PathFilter value) noexcept {
    filter_ = std::move(value);
    return *this;
}

void ChangelistBuilder::Changes(const HashId& from, const HashId& to) {
    if (from == to) {
        return;
    }

    if (emit_directory_changed_) {
        const auto t1 = from ? GetTreeId(from, odb_) : HashId();
        const auto t2 = to ? GetTreeId(to, odb_) : HashId();

        if (t1 != t2) {
            EmitChange(std::string(), PathType::Directory, Modifications{.content = true});
        }
    }

    TreeChanges(std::string(), GetRoot(from, odb_), GetRoot(to, odb_));
}

void ChangelistBuilder::EmitAdd(const std::string& path, const PathType type) {
    if (filter_.Match(path)) {
        Change change;

        change.action = PathAction::Add;
        change.path = path;
        change.type = type;

        cb_(std::move(change));
    }
}

void ChangelistBuilder::EmitChange(
    const std::string& path, const PathType type, const Modifications flags
) {
    if (filter_.Match(path)) {
        Change change;

        change.action = PathAction::Change;
        change.flags = flags;
        change.path = path;
        change.type = type;

        cb_(std::move(change));
    }
}

void ChangelistBuilder::EmitDelete(const std::string& path, const PathType type) {
    if (filter_.Match(path)) {
        Change change;

        change.action = PathAction::Delete;
        change.path = path;
        change.type = type;

        cb_(std::move(change));
    }
}

void ChangelistBuilder::ProcessAdded(const std::string& path, const Tree::Entry to) {
    EmitAdd(path, to.Type());

    if (IsDirectory(to.Type()) && expand_added_ && filter_.IsParent(path)) {
        const auto& tree = odb_.LoadTree(to.Id());

        for (const auto entry : tree.Entries()) {
            ProcessAdded(JoinPath(path, entry.Name()), entry);
        }
    }
}

void ChangelistBuilder::ProcessChanged(
    const std::string& path, const Tree::Entry from, const Tree::Entry to
) {
    if (const auto flags = CompareEntries(from, to)) {
        if (flags.type) {
            ProcessDeleted(path, from);
            ProcessAdded(path, to);
        } else if (IsFile(from.Type())) {
            EmitChange(path, from.Type(), flags);
        } else if (IsDirectory(to.Type())) {
            if (emit_directory_changed_) {
                EmitChange(path, PathType::Directory, flags);
            }
            if (filter_.IsParent(path)) {
                TreeChanges(path, odb_.LoadTree(from.Id()), odb_.LoadTree(to.Id()));
            }
        } else {
            assert(false);
        }
    }
}

void ChangelistBuilder::ProcessDeleted(const std::string& path, const Tree::Entry from) {
    if (IsDirectory(from.Type()) && expand_deleted_ && filter_.IsParent(path)) {
        const auto& tree = odb_.LoadTree(from.Id());

        for (const auto entry : tree.Entries()) {
            ProcessDeleted(JoinPath(path, entry.Name()), entry);
        }
    }

    EmitDelete(path, from.Type());
}

void ChangelistBuilder::TreeChanges(const std::string& path, const Tree& from, const Tree& to) {
    const auto fe = from.Entries().end();
    const auto te = to.Entries().end();

    auto fi = from.Entries().begin();
    auto ti = to.Entries().begin();

    while (fi != fe && ti != te) {
        const int cmp = (*fi).Name().compare((*ti).Name());

        if (cmp == 0) {
            ProcessChanged(JoinPath(path, (*fi).Name()), *fi, *ti);
            ++fi;
            ++ti;
        } else if (cmp < 0) {
            ProcessDeleted(JoinPath(path, (*fi).Name()), *fi);
            ++fi;
        } else {
            ProcessAdded(JoinPath(path, (*ti).Name()), *ti);
            ++ti;
        }
    }

    while (fi != fe) {
        ProcessDeleted(JoinPath(path, (*fi).Name()), *fi);
        ++fi;
    }

    while (ti != te) {
        ProcessAdded(JoinPath(path, (*ti).Name()), *ti);
        ++ti;
    }
}
} // namespace Vcs
