#pragma once

#include "status.h"

#include <vcs/changes/stage.h>
#include <vcs/object/store.h>

#include <filesystem>

namespace Vcs {

/**
 * Actions with working directory.
 */
class WorkingTree {
public:
    WorkingTree(const std::filesystem::path& path, const Datastore odb, const std::function<HashId()> cb);

    /** Root path of the tree. */
    const std::filesystem::path& GetPath() const;

    /** Save file to a blob. */
    std::optional<PathEntry> MakeBlob(const std::string& path, Datastore odb) const;

    /** */
    void SetBaseTree(const HashId& tree_id);

public:
    /** Create directory. */
    void CreateDirectory(const std::string& path);

    /** Writes a tree into the working directory. */
    void Checkout(const HashId& tree_id);

    /** Writes an entry into the working directory. */
    void Checkout(const std::string& path, const PathEntry& entry);

    /** Remove a file or a directory from the working directory. */
    void Remove(const std::string& path);

    /** Emit status of changed items in the working tree. */
    void Status(const StatusOptions& options, const StageArea& stage, const StatusCallback& cb) const;

    /** Switches the working tree to the given snapshot. */
    bool SwitchTo(const HashId& tree_id);

private:
    void MakeTree(const std::filesystem::path& path, const Tree tree) const;

    void WriteBlob(const std::filesystem::path& path, const PathEntry& entry) const;

private:
    std::filesystem::path path_;
    /// Read-only object storage.
    const Datastore odb_;
    ///
    std::function<HashId()> get_tree_;
};

} // namespace Vcs
