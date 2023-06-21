#include "pack.h"
#include "disk.h"

#include <util/split.h>

#include <contrib/lz4/lz4.h>
#include <contrib/xxhash/xxhash.h>

#include <queue>

namespace Vcs::Store {
namespace {

/**
 * Type to report write status.
 */
struct TableIsFull { };

class PackWriter {
public:
    explicit PackWriter(const Leveled::Options& options)
        : options_(options) {
    }

    std::tuple<std::filesystem::path, std::filesystem::path, HashId, size_t> Write(
        const std::filesystem::path& path,
        const std::function<void(std::vector<std::pair<HashId, Leveled::MemoryTable::Tag>>& oids)>&
            fill_oids,
        const std::function<std::tuple<const void*, const Disk::DataTag>(const Leveled::MemoryTable::Tag&)>&
            get_content
    ) {
        // Populate oids.
        fill_oids(oids_);
        // Reorder oids.
        std::sort(oids_.begin(), oids_.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        // Ensure uniqueness of the oids.
        oids_.erase(
            std::unique(
                oids_.begin(), oids_.end(), [](const auto& a, const auto& b) { return a.first == b.first; }
            ),
            oids_.end()
        );

        offsets_.resize(oids_.size());

        const auto& [index_path, data_path] = std::make_pair(path / "index.tmp", path / "pack.tmp");

        // Write data file.
        const auto [data_hash, data_size] = WriteData(data_path, get_content);
        // Write index file.
        WriteIndex(index_path);
        // Validate written content.
        CheckPackTable(Leveled::PackTable(index_path, data_path), oids_);

        return std::make_tuple(index_path, data_path, data_hash, data_size);
    }

private:
    static void CheckPackTable(
        const Leveled::PackTable& pack,
        const std::vector<std::pair<HashId, Leveled::MemoryTable::Tag>>& oids
    ) {
        for (const auto& [id, tag] : oids) {
            const auto meta = pack.GetMeta(id);
            // No record with the id.
            if (!meta) {
                throw std::runtime_error(fmt::format("cannot locate '{}'", id));
            }
            if (meta.Bytes() != tag.meta.Bytes()) {
                throw std::runtime_error(fmt::format("metadata size mismatch"));
            }
            if (std::memcmp(meta.data, tag.meta.data, meta.Bytes()) != 0) {
                throw std::runtime_error(fmt::format("metadata mismatch"));
            }
        }
    }

    std::vector<uint32_t> GroupObjects(
        const std::vector<std::pair<HashId, Leveled::MemoryTable::Tag>>& oids, bool original
    ) const {
        std::vector<uint32_t> index;
        //
        index.reserve(oids.size());
        //
        for (size_t i = 0, end = oids.size(); i != end; ++i) {
            index.push_back(i);
        }
        if (original) {
            return index;
        }

        // Put blos last.
        const auto blobs = std::partition(index.begin(), index.end(), [&](const uint32_t i) {
            return oids[i].second.meta.Type() != DataType::Blob;
        });
        // Reorder blobs by size.
        std::sort(blobs, index.end(), [&](const uint32_t l, const uint32_t r) {
            return std::make_tuple(oids[l].second.meta.Size(), oids[l].first)
                 > std::make_tuple(oids[r].second.meta.Size(), oids[r].first);
        });

        // Put commits first.
        const auto renames = std::partition(index.begin(), blobs, [&](const uint32_t i) {
            return oids[i].second.meta.Type() == DataType::Commit;
        });
        // Put renames after commits.
        const auto trees = std::partition(renames, blobs, [&](const uint32_t i) {
            return oids[i].second.meta.Type() == DataType::Renames;
        });

        // Put trees after renames.
        const auto rest = std::partition(trees, blobs, [&](const uint32_t i) {
            return oids[i].second.meta.Type() == DataType::Tree;
        });
        // Reorder trees by size.
        std::sort(trees, rest, [&](const uint32_t l, const uint32_t r) {
            return std::make_tuple(oids[l].second.meta.Size(), oids[l].first)
                 > std::make_tuple(oids[r].second.meta.Size(), oids[r].first);
        });

        return index;
    }

    std::pair<HashId, size_t> WriteData(
        const std::filesystem::path& path,
        const std::function<std::tuple<const void*, const Disk::DataTag>(const Leveled::MemoryTable::Tag&)>&
            get_content
    ) {
        const std::vector<uint32_t> order = GroupObjects(oids_, !options_.group_by_type);
        File data_file = File::ForAppend(path);
        HashId::Builder builder;
        uint64_t offset = 0;

        // Write pack table (compress).
        for (const auto i : order) {
            const auto [buf, tag] = get_content(oids_[i].second);

            {
                const uint32_t len = tag.Length();
                // Hash data.
                builder.Append(&tag, sizeof(tag)).Append(buf, len);
                // Copy data tag.
                data_file.Write(&tag, sizeof(tag));
                // Copy data entry.
                data_file.Write(buf, len);
                // Remember offset of the object.
                offsets_[i] = offset;
                // Advance pointer.
                offset += len + sizeof(tag);
            }
        }
        // Ensure data has been written to disk.
        if (options_.data_sync) {
            data_file.FlushData();
        }
        // Close data file.
        data_file.Close();

        return std::make_pair(builder.Build(), offset);
    }

    void WriteIndex(const std::filesystem::path& path) const {
        // Build index.
        std::vector<Disk::IndexEntry> index;
        // Allocate memory.
        index.reserve(oids_.size());
        // Fill the index.
        for (size_t i = 0, end = oids_.size(); i != end; ++i) {
            const auto& [id, tag] = oids_[i];

            index.emplace_back(id, tag.meta, offsets_[i]);
        }

        // Build fanout table.
        std::vector<uint32_t> fanout(256, index.size());

        for (auto it = index.begin(), end = index.end(); it != end;) {
            fanout[it->oid.Data()[0]] = it - index.begin();

            it = std::upper_bound(it, end, it->oid, [](const auto& val, const auto& item) {
                return val.Data()[0] < item.oid.Data()[0];
            });
        }
        // Fill fanout gaps.
        for (ssize_t i = (ssize_t)fanout.size() - 2; i >= 0; --i) {
            if (fanout[i] == index.size()) {
                fanout[i] = fanout[i + 1];
            }
        }

        File index_file = File::ForAppend(path);
        // Write fanout table.
        index_file.Write(fanout.data(), fanout.size() * sizeof(decltype(fanout)::value_type));
        // Write index.
        index_file.Write(index.data(), index.size() * sizeof(decltype(index)::value_type));
        // Ensure data has been written to disk.
        if (options_.data_sync) {
            index_file.FlushData();
        }
        // Close index file.
        index_file.Close();
    }

private:
    Leveled::Options options_;
    std::vector<std::pair<HashId, Leveled::MemoryTable::Tag>> oids_;
    std::vector<uint64_t> offsets_;
};

std::optional<Disk::IndexEntry> FindIndexEntry(const HashId& id, const std::span<const std::byte> buf) {
    std::span<const uint32_t, 256> fanout(reinterpret_cast<const uint32_t*>(buf.data()), 256);

    std::span<const Disk::IndexEntry> index(
        reinterpret_cast<const Disk::IndexEntry*>(reinterpret_cast<const uint32_t*>(buf.data()) + 256),
        reinterpret_cast<const Disk::IndexEntry*>(buf.data() + buf.size())
    );

    const auto f = id.Data()[0];
    const auto l = index.begin() + fanout[f];
    const auto r = index.begin() + (f == 0xFF ? index.size() : fanout[f + 1]);

    auto oi = std::lower_bound(l, r, id, [](const auto& item, const auto& id) { return item.oid < id; });

    if (oi != r && oi->oid == id) {
        return *oi;
    } else {
        return {};
    }
}

} // namespace

///////////////////////////////////////////////////////////////////////////////////////////////////////////

Leveled::MemoryTable::MemoryTable(const Options& options, File&& file)
    : options_(options)
    , file_(std::move(file))
    , data_(nullptr) {
    // Check capacity do not exceed 256 Mb.
    assert((256u << 20) >= options_.memtable_size);
}

void Leveled::MemoryTable::Restore(bool finalized) {
    const auto size = file_.Size();
    size_t offset = 0;
    while (offset < size) {
        Disk::LooseHeader hdr{};
        size_t content = offset;
        // Read file header.
        if (file_.Load(&hdr, sizeof(hdr), offset) != sizeof(hdr)) {
            throw std::runtime_error("cannot read file header");
        }
        // Validate data integrity.
        if (hdr.crc != XXH32(&hdr, offsetof(Disk::LooseHeader, crc), 0)) {
            throw std::runtime_error("header data corruption");
        } else {
            offset += sizeof(hdr);
        }
        // Content.
        if (offset + hdr.stored > size) {
            throw std::runtime_error("content data corruption");
        } else {
            offset += hdr.stored;
        }
        // XXH3 (64)
        if (offset + sizeof(uint64_t) > size) {
            throw std::runtime_error("crc data corruption");
        } else {
            offset += sizeof(uint64_t);
        }
        // OID
        if (offset + sizeof(HashId) > size) {
            throw std::runtime_error("oid data corruption");
        } else {
            unsigned char buf[sizeof(HashId)];
            if (file_.Load(buf, sizeof(buf), offset) != sizeof(buf)) {
                throw std::runtime_error("cannot load oid");
            }
            offset += sizeof(HashId);

            oids_.emplace(
                HashId::FromBytes(buf),
                Tag{
                    .meta = DataHeader::Make(hdr.Type(), hdr.Size()),
                    .offset = uint32_t(content),
                    .portion = 0,
                }
            );
        }
    }

    assert(size == offset);
    size_ = size;

    if (finalized) {
        file_map_ = std::make_unique<FileMap>(file_);
        data_ = (const char*)file_map_->Map();
    }
}

std::pair<std::string_view, Compression> Leveled::MemoryTable::Content(const Tag& tag) const {
    assert(data_);

    Disk::LooseHeader hdr{};
    // Read file header.
    if (file_.Load(&hdr, sizeof(hdr), tag.offset) != sizeof(hdr)) {
        throw std::runtime_error("cannot read file header");
    }
    // Validate data integrity.
    if (hdr.crc != XXH32(&hdr, offsetof(Disk::LooseHeader, crc), 0)) {
        throw std::runtime_error("header data corruption");
    }

    if (tag.offset + sizeof(Disk::LooseHeader) + hdr.stored > size_) {
        throw std::runtime_error("content data corruption");
    }

    return std::make_pair(
        std::string_view(data_ + tag.offset + sizeof(Disk::LooseHeader), hdr.stored), hdr.Codec()
    );
}

auto Leveled::MemoryTable::Ids() const -> const std::unordered_map<HashId, Tag>& {
    return oids_;
}

void Leveled::MemoryTable::Finalize() {
    file_.FlushData();

    file_map_ = std::make_unique<FileMap>(file_);
    data_ = (const char*)file_map_->Map();
}

void Leveled::MemoryTable::Flush() {
    file_.FlushData();
}

size_t Leveled::MemoryTable::Size() const noexcept {
    return size_;
}

DataHeader Leveled::MemoryTable::GetMeta(const HashId& id) const {
    if (const auto oi = oids_.find(id); oi != oids_.end()) {
        return oi->second.meta;
    }
    return DataHeader();
}

bool Leveled::MemoryTable::Exists(const HashId& id) const {
    return oids_.contains(id);
}

Object Leveled::MemoryTable::Load(const HashId& id, const DataType expected) const {
    const auto oi = oids_.find(id);
    // No object or type mismatch.
    if (oi == oids_.end() || (expected != DataType::None && oi->second.meta.Type() != expected)) {
        return Object();
    }
    Disk::LooseHeader hdr{};
    // Read file header.
    if (file_.Load(&hdr, sizeof(hdr), oi->second.offset) != sizeof(hdr)) {
        throw std::runtime_error("cannot read file header");
    }
    // Validate data integrity.
    if (hdr.crc != XXH32(&hdr, offsetof(Disk::LooseHeader, crc), 0)) {
        throw std::runtime_error("header data corruption");
    }

    // Load object content.
    return Object::Load(DataHeader::Make(hdr.Type(), hdr.Size()), [&](std::byte* buf, size_t buf_len) {
        const auto read_to_buffer = [&](std::byte* p, size_t size) {
            const auto content_offset = oi->second.offset + sizeof(Disk::LooseHeader);
            const auto crc_offset = content_offset + size;
            uint64_t content_crc;
            // Load content.
            if (file_.Load(p, size, content_offset) != size) {
                throw std::runtime_error("unexpected end of stream");
            }
            // Load checksum.
            if (file_.Load(&content_crc, sizeof(content_crc), crc_offset) != sizeof(content_crc)) {
                throw std::runtime_error("unexpected end of stream");
            }
            // Compare checksums.
            if (content_crc != XXH3_64bits(p, size)) {
                throw std::runtime_error("content data corruption");
            }
        };

        switch (hdr.Codec()) {
            case Compression::None: {
                read_to_buffer(buf, buf_len);
                break;
            }
            case Compression::Lz4: {
                auto comp = std::make_unique_for_overwrite<char[]>(hdr.stored);

                read_to_buffer(reinterpret_cast<std::byte*>(comp.get()), hdr.stored);
                // TODO: read directly from the memory if the portion was finalized.
                const int ret = ::LZ4_decompress_safe(comp.get(), (char*)buf, hdr.stored, buf_len);
                if (ret != int(buf_len)) {
                    throw std::runtime_error(fmt::format("cannot decompres content '{}'", ret));
                }
                break;
            }
        }
    });
}

void Leveled::MemoryTable::Put(const HashId& id, const DataType type, const std::string_view content) {
    // Portion already finalized and cannot accept data any more.
    if (data_) {
        throw TableIsFull();
    }
    // Object with the give id is already in the current portion.
    // No need to store twice.
    if (oids_.contains(id)) [[unlikely]] {
        return;
    }

    const auto write_to_file = [&](const Compression codec, const void* buf, size_t buf_len) {
        const size_t serialized_size = sizeof(Disk::LooseHeader) + buf_len + sizeof(uint64_t) + id.Size();
        // Check available capacity.
        if (serialized_size > options_.memtable_size - size_) {
            throw TableIsFull();
        }

        const uint64_t content_crc = XXH3_64bits(buf, buf_len);
        Disk::LooseHeader file_header{};
        file_header.tag = Disk::LooseHeader::MakeTag(codec, type);
        file_header.original = content.size();
        // Setup length of stored data.
        file_header.stored = buf_len;
        // Setup header checksum.
        file_header.crc = XXH32(&file_header, offsetof(Disk::LooseHeader, crc), 0);
        // Write file header.
        file_.Write(&file_header, sizeof(file_header));
        // Write file content.
        file_.Write(buf, buf_len);
        // Write content checksum.
        file_.Write(&content_crc, sizeof(content_crc));
        // Write key.
        file_.Write(id.Data(), id.Size());
        // Return total size of written bytes.
        return serialized_size;
    };

    const auto tag =
        Tag{.meta = DataHeader::Make(type, content.size()), .offset = uint32_t(size_), .portion = 0};

    switch (options_.codec) {
        case Compression::None: {
            size_ += write_to_file(options_.codec, content.data(), content.size());
            break;
        }
        case Compression::Lz4: {
            auto buf_size = content.size() * options_.compression_penalty;
            auto buf = std::make_unique_for_overwrite<char[]>(buf_size);

            int len = ::LZ4_compress_default(content.data(), buf.get(), content.size(), buf_size);
            if (len == 0) {
                buf.reset();
                // Save content uncompressed.
                size_ += write_to_file(Compression::None, content.data(), content.size());
            } else {
                // Save compressed.
                size_ += write_to_file(options_.codec, buf.get(), len);
            }
            break;
        }
    }

    oids_.emplace(id, tag);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

Leveled::PackTable::PackTable(const std::filesystem::path& index, const std::filesystem::path& pack)
    : index_path_(index)
    , pack_path_(pack) {
    index_file_.second = File::ForRead(index);
    index_file_.first = std::make_unique<FileMap>(index_file_.second);

    index_ = {(const std::byte*)index_file_.first->Map(), index_file_.second.Size()};

    pack_file_.second = File::ForRead(pack);
    pack_file_.first = std::make_unique<FileMap>(pack_file_.second);
    data_ = {(const std::byte*)pack_file_.first->Map(), pack_file_.second.Size()};
}

void Leveled::PackTable::Enumerate(const std::function<bool(const HashId&, const DataHeader)>& cb) const {
    std::span<const Disk::IndexEntry> index(
        reinterpret_cast<const Disk::IndexEntry*>(reinterpret_cast<const uint32_t*>(index_.data()) + 256),
        reinterpret_cast<const Disk::IndexEntry*>(index_.data() + index_.size())
    );

    if (!cb) {
        return;
    }
    for (const auto& e : index) {
        if (!cb(e.oid, e.Meta())) {
            break;
        }
    }
}

size_t Leveled::PackTable::Size() const noexcept {
    return data_.size();
}

bool Leveled::PackTable::Exists(const HashId& id) const {
    return bool(FindIndexEntry(id, index_));
}

DataHeader Leveled::PackTable::GetMeta(const HashId& id) const {
    if (const auto& entry = FindIndexEntry(id, index_)) {
        return entry->Meta();
    }
    return DataHeader();
}

Object Leveled::PackTable::Load(const HashId& id, const DataType expected) const {
    const auto& entry = FindIndexEntry(id, index_);
    // No object.
    if (!entry) {
        return Object();
    }

    const DataHeader hdr = entry->Meta();
    // Type mismatch.
    if (expected != DataType::None && hdr.Type() != expected && hdr.Type() != DataType::Index) {
        return Object();
    }
    // Load objects's content.
    return Object::Load(hdr, [&](std::byte* buf, size_t buf_len) {
        const size_t offset = entry->Offset();
        Disk::DataTag tag;
        // Load content's size.
        if (offset + sizeof(tag) > data_.size()) {
            throw std::runtime_error("cannot load length (overflow)");
        } else {
            std::memcpy(&tag, data_.data() + offset, sizeof(tag));
        }
        // Ensure no buffer overrun.
        if (offset + sizeof(tag) + tag.Length() > data_.size()) {
            throw std::runtime_error("cannot load content (overflow)");
        }
        if (tag.Length() == 0) {
            return;
        }
        if (tag.IsCompressed()) {
            const int ret = ::LZ4_decompress_safe(
                reinterpret_cast<const char*>(data_.data()) + (offset + sizeof(tag)), (char*)buf,
                tag.Length(), buf_len
            );

            if (ret != int(buf_len)) {
                throw std::runtime_error(fmt::format("cannot decompres content '{}'", ret));
            }
        } else if (tag.Length() == buf_len) {
            std::memcpy(buf, data_.data() + offset + sizeof(tag), buf_len);
        } else {
            throw std::runtime_error(
                fmt::format("uncompressed size mismatch '{}' and '{}'", tag.Length(), buf_len)
            );
        }
    });
}

void Leveled::PackTable::Put(const HashId&, const DataType, const std::string_view) {
    // Pack table is read-only.
}

std::shared_ptr<Leveled::PackTable> Leveled::PackTable::MergeMemtables(
    const std::vector<std::pair<std::shared_ptr<MemoryTable>, std::filesystem::path>>& snapshots,
    const std::filesystem::path path,
    const Options& options
) {
    const auto& [index_path, data_path, data_hash, _] = PackWriter(options).Write(
        path,
        // Fill oids.
        [&](std::vector<std::pair<HashId, Leveled::MemoryTable::Tag>>& oids) {
            for (size_t i = 0, end = snapshots.size(); i < end; ++i) {
                for (auto [id, tag] : snapshots[i].first->Ids()) {
                    tag.portion = i;
                    oids.emplace_back(id, tag);
                }
            }
        },
        // Get record's content.
        [&](const Leveled::MemoryTable::Tag& tag) {
            // Populate content.
            const auto [content, codec] = snapshots[tag.portion].first->Content(tag);

            return std::make_tuple(
                content.data(), Disk::DataTag(content.size(), codec != Compression::None, false)
            );
        }
    );

    std::filesystem::rename(data_path, path / fmt::format("pack-{}.{:03}.pack", data_hash, 0));
    std::filesystem::rename(index_path, path / fmt::format("pack-{}.{:03}.index", data_hash, 0));

    return std::make_shared<PackTable>(
        path / fmt::format("pack-{}.{:03}.index", data_hash, 0),
        path / fmt::format("pack-{}.{:03}.pack", data_hash, 0)
    );
}

std::pair<std::shared_ptr<Leveled::PackTable>, size_t> Leveled::PackTable::MergePacks(
    const std::vector<std::shared_ptr<PackTable>>& tables,
    const std::filesystem::path& path,
    const Options& options
) {
    const auto& [index_path, data_path, data_hash, data_size] = PackWriter(options).Write(
        path,
        // Fill oids.
        [&](std::vector<std::pair<HashId, Leveled::MemoryTable::Tag>>& oids) {
            for (size_t i = 0; i < tables.size(); ++i) {
                std::span<const Disk::IndexEntry> index(
                    reinterpret_cast<const Disk::IndexEntry*>(
                        reinterpret_cast<const uint32_t*>(tables[i]->index_.data()) + 256
                    ),
                    reinterpret_cast<const Disk::IndexEntry*>(
                        tables[i]->index_.data() + tables[i]->index_.size()
                    )
                );

                for (const auto& e : index) {
                    oids.emplace_back(
                        e.oid,
                        MemoryTable::Tag{.meta = e.Meta(), .offset = e.Offset(), .portion = uint32_t(i)}
                    );
                }
            }
        },
        // Get record's content.
        [&](const Leveled::MemoryTable::Tag& tag) {
            const auto data = tables[tag.portion]->data_.data() + tag.offset;
            // Copy data.
            Disk::DataTag data_tag;
            // Length of the record.
            std::memcpy(&data_tag, data, sizeof(data_tag));

            return std::make_tuple(data + sizeof(Disk::DataTag), data_tag);
        }
    );

    const size_t level = std::log2(std::max<size_t>(1u, data_size / options.memtable_size))
                       / std::log2(std::max<size_t>(2u, options.snapshots_to_pack));

    const auto indx_path = path / fmt::format("pack-{}.{:03}.index", data_hash, level);
    const auto pack_path = path / fmt::format("pack-{}.{:03}.pack", data_hash, level);

    std::filesystem::rename(path / "pack.tmp", pack_path);
    std::filesystem::rename(path / "index.tmp", indx_path);

    // Remove processed packs.
    for (size_t i = 0; i < tables.size(); ++i) {
        if (indx_path != tables[i]->index_path_) {
            std::filesystem::remove(tables[i]->index_path_);
        }
        if (pack_path != tables[i]->pack_path_) {
            std::filesystem::remove(tables[i]->pack_path_);
        }
    }

    return std::make_pair(std::make_shared<PackTable>(indx_path, pack_path), level);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

Leveled::Leveled(std::filesystem::path path, const Options& options)
    : path_(std::move(path))
    , options_(options) {
    assert(options_.memtable_size);
    assert(options_.snapshots_to_pack);
    // Load existing state.
    if (std::filesystem::exists(path_)) {
        LoadSnapshots(path_);
    }

    if (!options_.read_only) {
        // Ensure root directory exists.
        std::filesystem::create_directories(path_);
        // Create a directory for L0 snapshots.
        std::filesystem::create_directory(path_ / "snap");
        // Create a directory for pack files.
        std::filesystem::create_directories(path_ / "pack");
    }
}

void Leveled::Commit() {
    if (options_.read_only) {
        return;
    }

    std::shared_lock lock(mutex_);

    if (snapshots_.size()) {
        snapshots_.back().first->Flush();
    }
}

void Leveled::Dump(FILE* out) const {
    std::shared_lock lock(mutex_);

    size_t data_size = 0;

    for (auto ri = snapshots_.rbegin(); ri != snapshots_.rend(); ++ri) {
        data_size += ri->first->Size();
    }

    for (auto li = levels_.begin(); li != levels_.end(); ++li) {
        for (auto pi = li->rbegin(); pi != li->rend(); ++pi) {
            data_size += (*pi)->Size();
        }
    }

    fmt::print(out, "Statistic:\n");
    fmt::print(out, "  data size: {}\n", data_size);
    fmt::print(out, "  snaspshots:  {}\n", snapshots_.size());
    fmt::print(out, "  levels:  {}\n", levels_.size());
    for (size_t i = 0; i < levels_.size(); ++i) {
        size_t s = 0;
        for (auto pi = levels_[i].begin(); pi != levels_[i].end(); ++pi) {
            s += (*pi)->Size();
        }

        fmt::print(out, "    level[{}]: {} - {}\n", i, levels_[i].size(), s);
    }
}

void Leveled::Pack(bool to_single) {
    if (!options_.read_only) {
        std::unique_lock lock(mutex_);
        // Finalized current mem-table.
        FinalizeNoLock();
        // Merge mem-tables into a pack file.
        // Optionally all packs may be merged into a single one.
        MergeSnapshots(to_single);
    }
}

void Leveled::Rotate() {
    if (!options_.read_only) {
        std::unique_lock lock(mutex_);
        // Finalized current mem-table.
        FinalizeNoLock();
    }
}

bool Leveled::Exists(const HashId& id) const {
    std::shared_lock lock(mutex_, std::defer_lock_t());

    if (!options_.read_only) {
        lock.lock();
    }

    for (auto ri = snapshots_.rbegin(); ri != snapshots_.rend(); ++ri) {
        if (ri->first->Exists(id)) {
            return true;
        }
    }

    for (auto li = levels_.begin(); li != levels_.end(); ++li) {
        for (auto pi = li->rbegin(); pi != li->rend(); ++pi) {
            if ((*pi)->Exists(id)) {
                return true;
            }
        }
    }

    return false;
}

DataHeader Leveled::GetMeta(const HashId& id) const {
    std::shared_lock lock(mutex_, std::defer_lock_t());

    if (!options_.read_only) {
        lock.lock();
    }

    for (auto ri = snapshots_.rbegin(); ri != snapshots_.rend(); ++ri) {
        if (auto meta = ri->first->GetMeta(id)) {
            return meta;
        }
    }

    for (auto li = levels_.begin(); li != levels_.end(); ++li) {
        for (auto pi = li->rbegin(); pi != li->rend(); ++pi) {
            if (auto meta = (*pi)->GetMeta(id)) {
                return meta;
            }
        }
    }

    return DataHeader();
}

Object Leveled::Load(const HashId& id, const DataType expected) const {
    std::shared_lock lock(mutex_, std::defer_lock_t());

    if (!options_.read_only) {
        lock.lock();
    }

    for (auto ri = snapshots_.rbegin(); ri != snapshots_.rend(); ++ri) {
        if (Object obj = ri->first->Load(id, expected)) {
            return obj;
        }
    }

    for (auto li = levels_.begin(); li != levels_.end(); ++li) {
        for (auto pi = li->rbegin(); pi != li->rend(); ++pi) {
            if (Object obj = (*pi)->Load(id, expected)) {
                return obj;
            }
        }
    }

    return Object();
}

void Leveled::Put(const HashId& id, const DataType type, const std::string_view content) {
    if (options_.read_only) {
        return;
    }

    std::unique_lock lock(mutex_);

    try {
        // Open memory table if needed.
        if (snapshots_.empty()) {
            snapshots_.push_back(MakeMemtable());
        }
        // Try to put data.
        snapshots_.back().first->Put(id, type, content);
        return;
    } catch (const TableIsFull&) {
        // Table is full. Allocate another one.
    }

    // Finalize current memtable and create new one.
    FinalizeNoLock();
    // Reopen memory tabl
    snapshots_.push_back(MakeMemtable());
    // Second attempt should be always successful.
    snapshots_.back().first->Put(id, type, content);
}

void Leveled::LoadSnapshots(const std::filesystem::path& path) {
    std::unordered_map<HashId, std::pair<std::filesystem::path, std::filesystem::path>> packs;
    std::unordered_map<HashId, size_t> levels;
    std::vector<std::pair<std::filesystem::path, std::optional<size_t>>> snaps;

    auto di = std::filesystem::recursive_directory_iterator(path);

    for (const auto& entry : di) {
        if (di.depth() != 1 || !entry.is_regular_file()) {
            continue;
        }

        const auto filename = entry.path().filename().string();

        const auto is_number = [](const std::string_view value) {
            return std::all_of(value.begin(), value.end(), [](char c) { return std::isdigit(c); });
        };

        if (filename.starts_with("pack-")) {
            const auto parts = SplitString<std::string, std::string_view>(filename, '.');
            // Name format: (pack-<hex>).<num>.(index|pack)
            if (parts.size() != 3 || (parts[2] != "index" && parts[2] != "pack")) {
                continue;
            }
            if (!HashId::IsHex(parts[0].substr(5))) {
                continue;
            }
            if (!is_number(parts[1])) {
                return;
            }

            const HashId hex = HashId::FromHex(parts[0].substr(5));
            // Set level.
            levels[hex] = std::stoul(std::string(parts[1]));
            // Set file path.
            if (parts[2] == "index") {
                packs[hex].first = entry.path();
            } else {
                packs[hex].second = entry.path();
            }
        } else if (filename.starts_with("memtable.")) {
            const auto parts = SplitString<std::string>(filename, '.');
            // Name format: memtable.(<num>|part)
            if (parts.size() != 2 || parts[0] != "memtable") {
                continue;
            }
            if (parts[1] != "part" && !is_number(parts[1])) {
                continue;
            }
            if (parts[1] == "part") {
                snaps.emplace_back(entry.path(), std::nullopt);
            } else {
                snaps.emplace_back(entry.path(), std::stoul(parts[1]));
            }
        } else {
            // Unknown file.
            continue;
        }
    }

    std::sort(snaps.rbegin(), snaps.rend(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });

    // Load packs
    for (const auto& [hex, paths] : packs) {
        const auto li = levels.find(hex);

        if (paths.first.empty() || paths.second.empty()) {
            continue;
        }

        if (options_.read_only) {
            levels_.resize(1);
            levels_[0].emplace_back(std::make_shared<PackTable>(paths.first, paths.second));
        } else {
            if (li->second >= levels_.size()) {
                levels_.resize(li->second + 1);
            }

            levels_[li->second].emplace_back(std::make_shared<PackTable>(paths.first, paths.second));
        }
    }

    // Load snapshots.
    for (const auto& [path, number] : snaps) {
        if (number) {
            // Finalized snapshot.
            auto p = std::make_shared<MemoryTable>(options_, File::ForRead(path));
            // Load oids.
            p->Restore(true);
            // Check table size.
            if (p->Size()) {
                snapshots_.emplace_back(std::move(p), path);
            }
        } else {
            auto p = std::make_shared<MemoryTable>(options_, File::ForAppend(path));
            // Load oids.
            // TODO: drop all objects after first corruption.
            p->Restore(false);
            // Check table size.
            if (p->Size()) {
                snapshots_.emplace_back(std::move(p), path);
            }
        }
    }
    // Adjust snap counter.
    for (const auto& [_, number] : snaps) {
        if (number) {
            snap_counter_ = std::max(snap_counter_, *number);
        }
    }
}

std::pair<std::shared_ptr<Leveled::MemoryTable>, std::filesystem::path> Leveled::MakeMemtable() const {
    const auto path = path_ / "snap" / "memtable.part";

    return std::make_pair(std::make_shared<MemoryTable>(options_, File::ForAppend(path)), path);
}

void Leveled::MergeSnapshots(bool to_single) {
    // Merge mem-tables.
    if (snapshots_.size()) {
        auto pack = PackTable::MergeMemtables(snapshots_, path_ / "pack", options_);
        // Append pack to the first level.
        if (levels_.empty()) {
            levels_.push_back({pack});
        } else {
            levels_[0].push_back(pack);
        }
        // Remove processed snapshots.
        for (const auto& [_, path] : snapshots_) {
            std::filesystem::remove(path);
        }
        // Clear.
        snapshots_.clear();
    }

    // Merge all packs.
    if (to_single) {
        std::vector<std::shared_ptr<PackTable>> packs;
        // Collect all packs.
        for (auto li = levels_.begin(); li != levels_.end(); ++li) {
            // Cannot merge more than 256 packs at a time.
            if (packs.size() + li->size() > 256) {
                break;
            }
            // Copy from the current level.
            for (auto pi = li->begin(); pi != li->end(); ++pi) {
                packs.push_back(*pi);
            }
            // Clear current level.
            li->clear();
        }
        // Already merged.
        if (packs.size() < 2) {
            return;
        }
        // Merge packs.
        auto [pack, l] = PackTable::MergePacks(packs, path_ / "pack", options_);
        // Allocate level.
        if (l >= levels_.size()) {
            levels_.resize(l + 1);
        }
        // Put new pack to the appropriate level.
        levels_[l].push_back(pack);
        return;
    }

    // Merge overcrowded levels.
    for (size_t i = 0, end = levels_.size() + 1; i != end;) {
        if (levels_.size() == i) {
            break;
        }
        if (levels_[i].size() >= options_.snapshots_to_pack) {
            // Merge all packs at the current level.
            auto [pack, l] = PackTable::MergePacks(levels_[i], path_ / "pack", options_);
            // Clear merged packs.
            levels_[i].clear();
            // Allocate level.
            if (l >= levels_.size()) {
                levels_.resize(l + 1);
            }
            // Put new pack to the appropriate level.
            levels_[l].push_back(pack);
            // Check updated level.
            i = std::min(l, i + 1);
        } else {
            ++i;
        }
    }
}

void Leveled::FinalizeNoLock() {
    assert(!options_.read_only);
    // Noting to finalize. Return.
    if (snapshots_.empty()) {
        return;
    }
    // Finalize current memtable.
    if (snapshots_.back().first->Size() == 0) {
        snapshots_.pop_back();
    } else {
        snapshots_.back().first->Finalize();
        // Rename portion.
        if (!snapshots_.back().second.empty()) {
            auto path = path_ / "snap" / fmt::format("memtable.{:05}", ++snap_counter_);
            // Rename.
            std::filesystem::rename(snapshots_.back().second, path);
            // Assign updated path.
            snapshots_.back().second = std::move(path);
        }
    }
    // Pack snapshots.
    if (snapshots_.size() >= options_.snapshots_to_pack) {
        MergeSnapshots(false);
    }
}

} // namespace Vcs::Store
