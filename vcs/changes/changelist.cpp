#include "changelist.h"
#include "path.h"
#include "stage.h"

#include <vcs/object/serialize.h>
#include <vcs/object/store.h>

namespace Vcs {
namespace {

Modifications CompareEntries(const Tree::Entry x, const Tree::Entry y) {
    Modifications flags;

    flags.content = x.Id() != y.Id();

    if (IsFile(x.Type()) && IsFile(y.Type())) {
        flags.attributes = (x.Type() == PathType::Executible) != (y.Type() == PathType::Executible);
        flags.type = (x.Type() == PathType::Symlink) != (y.Type() == PathType::Symlink);
    } else {
        flags.type = x.Type() != y.Type();
    }

    return flags;
}

Tree GetRoot(const HashId& id, const Datastore& odb) {
    if (id) {
        return odb.LoadTree(GetTreeId(id, odb));
    } else {
        return Tree::Load(TreeBuilder().Serialize());
    }
}

bool IsIncluded(const PathFilter* filter, const std::string_view path) {
    return !filter || filter->Match(path);
}

bool IsParent(const PathFilter* filter, const std::string_view path) {
    return !filter || filter->IsParent(path);
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

ChangelistBuilder& ChangelistBuilder::SetExpandDirectories(bool value) noexcept {
    expand_directories_ = value;
    return *this;
}

ChangelistBuilder& ChangelistBuilder::SetInclude(const PathFilter* value) noexcept {
    filter_ = value;
    return *this;
}

void ChangelistBuilder::Changes(const HashId& from, const HashId& to) {
    if (from == to) {
        return;
    }

    TreeChanges(std::string(), GetRoot(from, odb_), GetRoot(to, odb_));
}

void ChangelistBuilder::EmitAdd(const std::string& path, const PathType type) {
    if (IsIncluded(filter_, path)) {
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
    if (IsIncluded(filter_, path)) {
        Change change;

        change.action = PathAction::Change;
        change.flags = flags;
        change.path = path;
        change.type = type;

        cb_(std::move(change));
    }
}

void ChangelistBuilder::EmitDelete(const std::string& path, const PathType type) {
    if (IsIncluded(filter_, path)) {
        Change change;

        change.action = PathAction::Delete;
        change.path = path;
        change.type = type;

        cb_(std::move(change));
    }
}

void ChangelistBuilder::ProcessAdded(const std::string& path, const Tree::Entry to) {
    EmitAdd(path, to.Type());

    if (IsDirectory(to.Type()) && expand_directories_ && IsParent(filter_, path)) {
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
            if (IsParent(filter_, path)) {
                TreeChanges(path, odb_.LoadTree(from.Id()), odb_.LoadTree(to.Id()));
            }
        } else {
            assert(false);
        }
    }
}

void ChangelistBuilder::ProcessDeleted(const std::string& path, const Tree::Entry from) {
    EmitDelete(path, from.Type());

    if (IsDirectory(from.Type()) && expand_directories_ && IsParent(filter_, path)) {
        const auto& tree = odb_.LoadTree(from.Id());

        for (const auto entry : tree.Entries()) {
            ProcessDeleted(JoinPath(path, entry.Name()), entry);
        }
    }
}

void ChangelistBuilder::TreeChanges(const std::string& path, const Tree& from, const Tree& to) {
    const auto fe = from.Entries().end();
    const auto te = to.Entries().end();

    auto fi = from.Entries().begin();
    auto ti = to.Entries().begin();

    while (fi != fe && ti != te) {
        const int cmp = (*fi).Name().compare((*ti).Name());

        if (cmp == 0) {
            const auto& name = JoinPath(path, (*fi).Name());

            ProcessChanged(name, *fi, *ti);

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
