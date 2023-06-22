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
        size_t memtable_size = 64u << 20;

        /// Number of snapshots to trigger a packing.
        size_t snapshots_to_pack = 4;

        /// Compression codec to use.
        Compression codec = Compression::Lz4;

        /// Store content uncompressed  if compression ratio less than (1 - compression_penalty).
        double compression_penalty = 0.9;

        /// Flush in-core data to storage device after write.
        bool data_sync = true;

        /// Use delta encoding.
        bool delta_encoding = true;

        /// Group objects by type in pack file.
        bool group_by_type = true;

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
            /// Object's metadata.
            DataHeader meta;
            /// Stored data is delta.
            uint64_t delta : 1 {0};
            /// Offset in data file to a record contained the object.
            uint64_t offset : 55;
            /// Index of merging portion.
            uint64_t portion : 8;
        };

        static_assert(sizeof(Tag) == 16);

    public:
        MemoryTable(const Options& options, File&& file);

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
        Options options_;
        File file_;
        /// Total size of appended data so far.
        size_t size_{0};
        /// Stored objects.
        std::unordered_map<HashId, Tag> oids_;
        const char* data_{nullptr};
        std::unique_ptr<FileMap> file_map_;
    };

    /**
     * Implement size efficient storage with zero-copy access.
     */
    class PackTable : public Datastore::Backend {
    public:
        PackTable(const std::filesystem::path& index, const std::filesystem::path& pack);

        /** Converts memtables into a portion format. */
        static std::shared_ptr<PackTable> MergeMemtables(
            const std::vector<std::pair<std::shared_ptr<MemoryTable>, std::filesystem::path>>& snapshots,
            const std::filesystem::path path,
            const Options& options
        );

        /** Merges multiple packs into a single one. */
        static std::pair<std::shared_ptr<PackTable>, size_t> MergePacks(
            const std::vector<std::shared_ptr<PackTable>>& tables,
            const std::filesystem::path& path,
            const Options& options
        );

    public:
        /**
         * Enumerates all objects in the storage.
         *
         * @param cb data receiver.
         */
        void Enumerate(const std::function<bool(const HashId&, const DataHeader)>& cb) const;

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

        mutable std::mutex mutex_;
        std::shared_ptr<Datastore::Backend> cache_;
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
     *
     * @param to_signle if true all packs will be merged into single one.
     */
    void Pack(bool to_signle = false);

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

    void MergeSnapshots(bool to_signle);

    void FinalizeNoLock();

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
