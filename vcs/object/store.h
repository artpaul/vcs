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
    DataHeader GetMeta(const HashId& id) const {
        return DoGetMeta(id);
    }

    /// Returns type of an object with the given id.
    DataType GetType(const HashId& id) const {
        return DoGetMeta(id).Type();
    }

    /// Checks whether an object with the id exists in the store.
    bool IsExists(const HashId& id) const {
        return DoIsExists(id);
    }

    /// Loads object.
    Object Load(const HashId& id) const {
        return DoLoad(id, DataType::None);
    }

    /// Loads object.
    Object Load(const HashId& id, DataType expected) const {
        return DoLoad(id, expected);
    }

    /// Loads blob object.
    Blob LoadBlob(const HashId& id) const;

    /// Loads commit object.
    Commit LoadCommit(const HashId& id) const;

    /// Loads index object.
    Index LoadIndex(const HashId& id) const;

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
