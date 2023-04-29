#pragma once

#include <vcs/object/store.h>

#include <filesystem>
#include <functional>

namespace Vcs::Store {

/**
 * Git-like loose disk storage.
 */
class Loose : public Datastore {
public:
    struct Options {
        /// Compression codec to use.
        Compression codec = Compression::Lz4;
        /// Flush in-core data to storage device after write.
        bool data_sync = true;
    };

public:
    explicit Loose(
        const std::filesystem::path& path,
        const Options& options = Options{.codec = Compression::Lz4, .data_sync = true}
    );

    /**
     * Enumerates all objects in the storage.
     *
     * @param with_metadata read and emit object's metadata.
     * @param cb data receiver.
     */
    void Enumerate(bool with_metadata, const std::function<bool(const HashId&, const DataHeader)>& cb)
        const;

private:
    DataHeader DoGetMeta(const HashId& id) const override;

    bool DoIsExists(const HashId& id) const override;

    Object DoLoad(const HashId& id, const DataType expected) const override;

    HashId DoPut(DataType type, std::string_view content) override;

private:
    /// Storage root directory.
    const std::filesystem::path path_;
    const Options options_;
};

} // namespace Vcs::Store
