#pragma once

#include <vcs/object/hashid.h>
#include <vcs/object/path.h>

#include <optional>
#include <vector>

namespace Vcs {

class Datastore;

class StageArea {
    class Directory;

public:
    explicit StageArea(const Datastore* odb, const HashId& tree_id = HashId()) noexcept;

    ~StageArea();

    /**
     * Sets tree entry.
     *
     * If there is no such path in the base tree the entry will be added,
     * otherwise it will be updated.
     */
    bool Add(const std::string_view path, const PathEntry& entry);

    /**
     * Returns value of the entry.
     *
     * @param removed return entry even if it mareked as removed.
     */
    std::optional<PathEntry> GetEntry(const std::string_view path, bool removed = false) const;

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
     *
     * @return root hash of created tree.
     */
    HashId SaveTree(Datastore* odb) const;

private:
    bool AddImpl(const std::string_view path, const PathEntry& entry, Directory* root);

    /**
     * @param id id of a tree object.
     */
    std::optional<PathEntry> GetPathEntry(const HashId& id, const std::vector<std::string_view>& parts)
        const;

    Directory* MutableRoot();

    HashId SaveTreeImpl(const Directory* root, Datastore* odb) const;

private:
    const Datastore* odb_;
    /// Root of a base tree.
    HashId tree_id_;
    /// Root of the stage tree.
    std::unique_ptr<Directory> stage_root_;
};

/**
 * Ensures the id points to Tree objects or returns the root tree of a commit.
 *
 * @param id  id of a commit or a tree object.
 */
HashId GetTreeId(const HashId& id, const Datastore* odb);

} // namespace Vcs
