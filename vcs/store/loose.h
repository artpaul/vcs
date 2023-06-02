#pragma once

#include <vcs/object/store.h>

#include <filesystem>
#include <functional>

namespace Vcs::Store {

/**
 * Git-like loose disk storage.
 */
class Loose : public Datastore::Backend {
public:
    struct Options {
        /// Compression codec to use.
        Compression codec = Compression::Lz4;
        /// Flush in-core data to storage device after write.
        bool data_sync = true;
    };

public:
    explicit Loose(
        const std::filesystem::path path,
        const Options& options = Options{.codec = Compression::Lz4, .data_sync = true}
    );

    template <typename... Args>
    static auto Make(Args&&... args) {
        return std::make_shared<Loose>(std::forward<Args>(args)...);
    }

    /**
     * Enumerates all objects in the storage.
     *
     * @param with_metadata read and emit object's metadata.
     * @param cb data receiver.
     */
    void Enumerate(bool with_metadata, const std::function<bool(const HashId&, const DataHeader)>& cb)
        const;

private:
    DataHeader GetMeta(const HashId& id) const override;

    bool Exists(const HashId& id) const override;

    Object Load(const HashId& id, const DataType expected) const override;

    void Put(const HashId& id, DataType type, std::string_view content) override;

private:
    /// Storage root directory.
    const std::filesystem::path path_;
    const Options options_;
};

} // namespace Vcs::Store
