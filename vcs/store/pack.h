#pragma once

#include <vcs/object/store.h>

#include <util/arena.h>
#include <util/file.h>
#include <util/varint.h>

#include <filesystem>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace Vcs::Store {

class Leveled : public Datastore::Backend {
public:
    struct Options {
        size_t memtable_size = 8u << 20;

        /// Number of snapshots to trigger a packing.
        size_t snapshots_to_pack = 4;

        /// Compression codec to use.
        Compression codec = Compression::Lz4;

        /// Flush in-core data to storage device after write.
        bool data_sync = true;

        /// If true, the store will be opened in read-only mode.
        bool read_only = false;

        /// Write each append to disk before return. Otherwise data may be temporary stored in
        /// a memory buffer and will not be written to a disk before explicit commit.
        bool use_wal = true;
    };

    /**
     * L0 storage for appended data.
     *
     * The purpose of this level is to implement fast write of incoming data.
     */
    class MemoryTable : public Datastore::Backend {
    public:
        struct Tag {
            DataHeader meta;
            uint32_t offset;
            uint32_t portion;
        };

    public:
        MemoryTable(File&& file, const size_t capacity, const Compression codec);

        /**
         * Content of an object.
         * */
        std::pair<std::string_view, Compression> Content(const Tag& tag) const;

        /**
         * Makes a postprocessing (building indexes, etc.)
         */
        void Finalize();

        /**
         * Flush written data to underlying storage.
         */
        void Flush();

        /**
         * Set of oids of stored objects.
         */
        const std::unordered_map<HashId, Tag>& Ids() const;

        /**
         * Loads content of the table from the file.
         *
         * @param finalized portion is finalized.
         */
        void Restore(bool finalized);

        /**
         * Total size of the raw data.
         */
        size_t Size() const noexcept;

    public:
        /** Metadata for an object with the given id. */
        DataHeader GetMeta(const HashId& id) const override;

        /** Check whether an object with the id exists in the store. */
        bool Exists(const HashId& id) const override;

        /** Load object. */
        Object Load(const HashId& id, const DataType expected) const override;

        /** Put an object into the datastore. */
        void Put(const HashId& id, const DataType type, const std::string_view content) override;

    private:
        File file_;
        /// Capacity of the table.
        size_t capacity_{0};
        /// Total size of appended data so far.
        size_t size_{0};
        /// Stored objects.
        std::unordered_map<HashId, Tag> oids_;
        /// Compression codec to use for processing an appending data.
        Compression codec_{Compression::None};
        const char* data_{nullptr};
        std::unique_ptr<FileMap> file_map_;
    };

    /**
     * Implement size efficient storage with zero-copy access.
     */
    class PackTable : public Datastore::Backend {
    public:
        PackTable(const std::filesystem::path& index, const std::filesystem::path& pack);

        /** Merges multiple packs into a single one. */
        static std::shared_ptr<PackTable> Merge(
            const std::vector<std::shared_ptr<PackTable>>& tables,
            const std::filesystem::path& path,
            size_t level
        );

        size_t Size() const noexcept;

    public:
        /** Metadata for an object with the given id. */
        DataHeader GetMeta(const HashId& id) const override;

        /** Check whether an object with the id exists in the store. */
        bool Exists(const HashId& id) const override;

        /** Load object. */
        Object Load(const HashId& id, const DataType expected) const override;

        /** Put an object into the datastore. */
        void Put(const HashId&, const DataType, const std::string_view) override;

    private:
        std::filesystem::path index_path_;
        std::filesystem::path pack_path_;

        std::pair<std::unique_ptr<FileMap>, File> pack_file_;
        std::pair<std::unique_ptr<FileMap>, File> index_file_;

        std::span<const std::byte> data_;
        std::span<const std::byte> index_;
    };

public:
    Leveled(const std::filesystem::path path, const Options& options);

    template <typename... Args>
    static auto Make(Args&&... args) {
        return std::make_shared<Leveled>(std::forward<Args>(args)...);
    }

public:
    /**
     * Commits writes done so far.
     */
    void Commit();

    /**
     * Dumps internal statistics.
     */
    void Dump(FILE* out) const;

    /**
     * Merge all memtables.
     */
    void Pack();

    /**
     * Finalize current memtable.
     */
    void Rotate();

private:
    /** Metadata for an object with the given id. */
    DataHeader GetMeta(const HashId& id) const override;

    /** Check whether an object with the id exists in the store. */
    bool Exists(const HashId& id) const override;

    /** Load object. */
    Object Load(const HashId& id, const DataType expected) const override;

    /** Put an object into the datastore. */
    void Put(const HashId& id, const DataType type, const std::string_view content) override;

private:
    /**
     * Loads snapshots at the open.
     */
    void LoadSnapshots(const std::filesystem::path& path);

    /**
     * Creates a memtable instance. The type will be depend of options.
     */
    std::pair<std::shared_ptr<MemoryTable>, std::filesystem::path> MakeMemtable() const;

    void MergeSnapshots();

    void FinalizeNoLock(bool pack);

    /**
     * Converts memtables into portion format.
     */
    static std::shared_ptr<PackTable> RepackMemtables(
        const std::vector<std::pair<std::shared_ptr<MemoryTable>, std::filesystem::path>>& snapshots,
        const std::filesystem::path path,
        bool sync
    );

private:
    /// Root directory of the storage.
    std::filesystem::path path_;
    Options options_;
    mutable std::shared_mutex mutex_;
    /// L0 in-memory tables.
    std::vector<std::pair<std::shared_ptr<MemoryTable>, std::filesystem::path>> snapshots_;
    std::vector<std::vector<std::shared_ptr<PackTable>>> levels_;
    size_t snap_counter_{0};
};

} // namespace Vcs::Store
