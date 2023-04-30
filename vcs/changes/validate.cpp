#include "validate.h"
#include "stage.h"

#include <vcs/object/object.h>

#include <unordered_map>

namespace Vcs {
namespace {

bool CheckCommit(const Commit& c, const Datastore& odb) {
    uint64_t max_generation = 0;
    // Root tree should not be null.
    if (!c.Tree()) {
        return false;
    }
    // Check parents.
    for (const auto& p : c.Parents()) {
        // Collect generation numbers of the parent commits.
        max_generation = std::max(max_generation, odb.LoadCommit(p).Generation());
    }
    // Check renames.
    if (c.Renames()) {
        const auto renames = odb.LoadRenames(c.Renames());
        // Collect generation numbers of the source of copied entries.
        for (const auto& id : renames.Commits()) {
            max_generation = std::max(max_generation, odb.LoadCommit(id).Generation());
        }
    }
    // Generation number mismatch.
    if (c.Generation() != max_generation + 1) {
        return false;
    }
    return true;
}

bool CheckIndex(const Index& index, const Datastore& odb) {
    for (const auto& p : index.Parts()) {
        const auto meta = odb.GetMeta(p.Id());
        // Type is not a blob.
        if (meta.Type() != DataType::Blob) {
            return false;
        }
        // Size mismatch.
        if (meta.Size() != p.Size()) {
            return false;
        }
    }
    return true;
}

bool CheckRenames(const Renames& renames, const Datastore& odb) {
    std::unordered_map<HashId, HashId> roots;
    // Check that the commits list holds pointers to Commit objects.
    for (const auto& id : renames.Commits()) {
        if (odb.GetType(id, true) != DataType::Commit) {
            return false;
        }
        // Load root tree.
        roots.emplace(id, odb.LoadCommit(id).Tree());
    }
    // Check that source paths of copied entries are valid.
    for (const auto& copy : renames.Copies()) {
        const auto ri = roots.find(copy.CommitId());
        // Identifier of the source commit should be present in the commits list.
        if (ri == roots.end()) {
            return false;
        }
        // Source path should be in the commit.
        if (!StageArea(odb, ri->second).GetEntry(copy.Source())) {
            return false;
        }
    }
    return true;
}

bool CheckTree(const Tree& t, const Datastore& odb) {
    for (const auto& e : t.Entries()) {
        // Object id is null.
        if (!e.Id()) {
            return false;
        }
        // Name is empty.
        if (e.Name().empty()) {
            return false;
        }

        const auto meta = odb.GetMeta(e.Id(), true);
        // Check type and size.
        if (meta.Type() == DataType::Blob) {
            // Size mismath.
            if (meta.Size() != e.Size()) {
                return false;
            }
            // Type mismatch.
            if (e.Type() == PathType::Directory) {
                return false;
            }
        } else if (meta.Type() == DataType::Tree) {
            // Type mismatch.
            if (e.Type() != PathType::Directory) {
                return false;
            }
        } else {
            return false;
        }
    }
    return false;
}

} // namespace

bool CheckConsistency(const HashId& id, const Datastore& odb) {
    switch (odb.GetType(id)) {
        case DataType::None:
            return false;
        case DataType::Blob:
            return true;
        case DataType::Tree:
            return CheckTree(odb.LoadTree(id), odb);
        case DataType::Commit:
            return CheckCommit(odb.LoadCommit(id), odb);
        case DataType::Renames:
            return CheckRenames(odb.LoadRenames(id), odb);
        case DataType::Tag:
            break;
        case DataType::Index:
            return CheckIndex(odb.LoadIndex(id), odb);
    }
    return false;
}

bool CheckConsistency(const Object& obj, const Datastore& odb) {
    switch (obj.Type()) {
        case DataType::None:
            return false;
        case DataType::Blob:
            return true;
        case DataType::Tree:
            return CheckTree(obj.AsTree(), odb);
        case DataType::Commit:
            return CheckCommit(obj.AsCommit(), odb);
        case DataType::Renames:
            return CheckRenames(obj.AsRenames(), odb);
        case DataType::Tag:
            break;
        case DataType::Index:
            return CheckIndex(obj.AsIndex(), odb);
    }
    return false;
}

} // namespace Vcs
