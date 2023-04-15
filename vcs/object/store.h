#pragma once

#include "hashid.h"
#include "object.h"

namespace Vcs {

class Datastore {
public:
    explicit Datastore(const size_t chunk_size = (4 << 20)) noexcept;

    virtual ~Datastore() noexcept = default;

public:
    /// Returns metadata for an object with the given id.
    DataHeader GetMeta(const HashId& id, bool resolve = false) const;

    /// Returns type of an object with the given id.
    DataType GetType(const HashId& id, bool resolve = false) const;

    /// Checks whether an object with the id exists in the store.
    bool IsExists(const HashId& id) const;

    /// Loads object.
    Object Load(const HashId& id) const;

    /// Loads object.
    Object Load(const HashId& id, DataType expected) const;

    /// Loads blob object.
    Blob LoadBlob(const HashId& id) const;

    /// Loads commit object.
    Commit LoadCommit(const HashId& id) const;

    /// Loads index object.
    Index LoadIndex(const HashId& id) const;

    /// Loads renames object.
    Renames LoadRenames(const HashId& id) const;

    /// Loads tree object.
    Tree LoadTree(const HashId& id) const;

    /// Puts an object into the datastore.
    HashId Put(const DataType type, const std::string_view content);

protected:
    virtual DataHeader DoGetMeta(const HashId& id) const = 0;

    virtual bool DoIsExists(const HashId& id) const = 0;

    virtual Object DoLoad(const HashId& id, const DataType expected) const = 0;

    virtual HashId DoPut(DataType type, std::string_view content) = 0;

private:
    size_t chunk_size_;
};

} // namespace Vcs
