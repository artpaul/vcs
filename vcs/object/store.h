#pragma once

#include "hashid.h"
#include "object.h"

#include <util/stream.h>

namespace Vcs {

/**
 * Provides high-level interface to object database.
 */
class Datastore {
public:
    /**
     * Low-level object storage.
     */
    class Backend {
    public:
        virtual ~Backend() = default;

        /** Metadata for an object with the given id. */
        virtual DataHeader GetMeta(const HashId& id) const = 0;

        /** Check whether an object with the id exists in the store. */
        virtual bool Exists(const HashId& id) const = 0;

        /** Load object. */
        virtual Object Load(const HashId& id, const DataType expected) const = 0;

        /** Put an object into the datastore. */
        virtual void Put(const HashId& id, const DataType type, const std::string_view content) = 0;

        /** Put an object into the datastore. */
        virtual void Put(const HashId& id, const Object& obj) {
            Put(id, obj.Type(), std::string_view(reinterpret_cast<const char*>(obj.Data()), obj.Size()));
        }
    };

public:
    explicit Datastore(const size_t chunk_size = (4 << 20));

    ~Datastore();

    /**
     * Chains a backend with the datastore in caching mode.
     */
    Datastore Cache(std::shared_ptr<Backend> backend) const {
        return Datastore(*this, std::move(backend), true);
    }

    /**
     * Chains a newly created backend with the datastore.
     */
    template <typename B, typename... Args>
    Datastore Chain(Args&&... args) const {
        return Datastore(*this, std::make_shared<B>(std::forward<Args>(args)...), false);
    }

    /**
     * Chains a backend with the datastore.
     */
    Datastore Chain(std::shared_ptr<Backend> backend) const {
        return Datastore(*this, std::move(backend), false);
    }

    /**
     * Makes a datastore with a default configuration and the give backend.
     */
    template <typename B, typename... Args>
    static Datastore Make(Args&&... args) {
        return Datastore(Datastore(), std::make_shared<B>(std::forward<Args>(args)...), false);
    }

public:
    /**
     * @name Metadata routines.
     * @{
     */

    /**
     * Returns metadata for an object with the given id.
     */
    DataHeader GetMeta(const HashId& id, bool resolve = false) const;

    /**
     * Returns type of an object with the given id.
     */
    DataType GetType(const HashId& id, bool resolve = false) const;

    /**
     * Checks whether an object with the id exists in the store.
     */
    bool IsExists(const HashId& id) const;

    /**@}*/

public:
    /**
     * @name Loading routines.
     * @{
     */

    /**
     * Loads object.
     */
    Object Load(const HashId& id) const;

    /**
     * Loads object.
     */
    Object Load(const HashId& id, DataType expected) const;

    /**
     * Loads blob object.
     */
    Blob LoadBlob(const HashId& id) const;

    /**
     * Loads commit object.
     */
    Commit LoadCommit(const HashId& id) const;

    /**
     * Loads index object.
     */
    Index LoadIndex(const HashId& id) const;

    /**
     * Loads renames object.
     */
    Renames LoadRenames(const HashId& id) const;

    /**
     * Loads tree object.
     */
    Tree LoadTree(const HashId& id) const;

    /**@}*/

public:
    /**
     * @name Saving routines.
     * @{
     */

    /**
     * Puts an object into the datastore.
     *
     * @param type type of data object.
     * @param content raw content of data object.
     */
    HashId Put(const DataType type, const std::string_view content);

    /**
     * Puts an object into the datastore.
     *
     * @param meta type and size of data object.
     * @param input input stream which provides content of data object.
     */
    HashId Put(const DataHeader meta, InputStream input);

    /**@}*/

private:
    Datastore(const Datastore& other, std::shared_ptr<Backend> backend, bool cache);

private:
    class Impl;

    std::shared_ptr<Impl> impl_;
};

/// Ensure Datastore is move constructible.
static_assert(std::is_move_constructible_v<Datastore>);

} // namespace Vcs
