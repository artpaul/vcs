#pragma once

#include <vcs/object/change.h>
#include <vcs/object/hashid.h>
#include <vcs/object/path.h>
#include <vcs/object/store.h>

#include <map>
#include <optional>
#include <vector>

namespace Vcs {

class StageArea {
    class Directory;

public:
    explicit StageArea(Datastore odb, const HashId& tree_id = HashId()) noexcept;

    ~StageArea();

    /**
     * Sets tree entry.
     *
     * If there is no such path in the base tree the entry will be added,
     * otherwise it will be updated.
     */
    bool Add(const std::string_view path, const PathEntry& entry);

    /**
     * Copies a path within the current tree.
     */
    bool Copy(const std::string& src, const std::string& dst);

    /**
     * Returns value of the entry.
     *
     * @param removed return entry even if it mareked as removed.
     */
    std::optional<PathEntry> GetEntry(const std::string_view path, bool removed = false) const;

    /**
     * Returns value of the entry.
     *
     * @param removed return entry even if it mareked as removed.
     */
    std::optional<PathEntry> GetEntry(const std::vector<std::string_view>& parts, bool removed = false)
        const;

    /**
     * @param removed return entry info even if it mareked as removed.
     */
    std::vector<std::pair<std::string, PathEntry>> ListTree(
        const std::string_view path, bool removed = false
    ) const;

    /**
     * Sets tree entry as removed.
     */
    bool Remove(const std::string_view path);

    /**
     * Builds tree.
     *
     * @param odb data storage where all new tree objects will be put.
     * @param save_empty_directory save empty directories.
     *
     * @return root hash of created tree.
     */
    HashId SaveTree(Datastore odb, bool save_empty_directories = true) const;

public:
    /**
     * @name Renames
     * @{
     */

    const std::map<std::string, CommitPath>& CopyInfo() const;

    /**@}*/

private:
    bool AddImpl(const std::string_view path, const PathEntry& entry, Directory* root);

    /**
     * @param id id of a tree object.
     */
    std::optional<PathEntry> GetPathEntry(const HashId& id, const std::vector<std::string_view>& parts)
        const;

    Directory* MutableRoot();

    std::pair<HashId, DataType> SaveTreeImpl(
        const Directory* root, Datastore odb, bool save_empty_directory
    ) const;

private:
    Datastore odb_;
    /// Root of a base tree.
    HashId tree_id_;
    /// Root of the stage tree.
    std::unique_ptr<Directory> stage_root_;
    /// List of destinations with the source info.
    std::map<std::string, CommitPath> copies_;
};

/**
 * Ensures the id points to Tree objects or returns the root tree of a commit.
 *
 * @param id  id of a commit or a tree object.
 */
HashId GetTreeId(const HashId& id, const Datastore& odb);

} // namespace Vcs
