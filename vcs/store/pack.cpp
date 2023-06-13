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

std::optional<Disk::IndexEntry> FindIndexEntry(const HashId& id, const std::span<const std::byte> buf) {
    std::span<const uint32_t, 256> fanout(reinterpret_cast<const uint32_t*>(buf.data()), 256);

    std::span<const Disk::IndexEntry> index(
        reinterpret_cast<const Disk::IndexEntry*>(reinterpret_cast<const uint32_t*>(buf.data()) + 256),
        reinterpret_cast<const Disk::IndexEntry*>(buf.data() + buf.size())
    );

    const auto f = id.Data()[0];
    const auto l = index.begin() + fanout[f];
    const auto r = index.begin() + (f == 0xFF ? index.size() : fanout[f + 1]);

    auto oi = std::lower_bound(l, r, id, [](const auto& item, const auto& id) { return item.id < id; });

    if (oi != r && oi->id == id) {
        return *oi;
    } else {
        return {};
    }
}

void CheckPackTable(const Leveled::PackTable& pack, const std::span<const Disk::IndexEntry> index) {
    for (const auto& e : index) {
        const auto meta = pack.GetMeta(e.id);
        // No record with the id.
        if (!meta) {
            throw std::runtime_error(fmt::format("cannot locate '{}'", e.id));
        }
        if (meta.Bytes() != e.GetMeta().Bytes()) {
            throw std::runtime_error(fmt::format("metadata size mismatch"));
        }
        if (std::memcmp(meta.data, e.GetMeta().data, meta.Bytes()) != 0) {
            throw std::runtime_error(fmt::format("metadata mismatch"));
        }
    }
}

} // namespace

///////////////////////////////////////////////////////////////////////////////////////////////////////////

Leveled::MemoryTable::MemoryTable(File&& file, const size_t capacity, const Compression codec)
    : file_(std::move(file))
    , capacity_(capacity)
    , codec_(codec)
    , data_(nullptr) {
    // Check capacity do not exceed 256 Mb.
    assert((256u << 20) >= capacity_);
}

void Leveled::MemoryTable::Restore(bool finalized) {
    const auto size = file_.Size();
    size_t offset = 0;
    while (offset < size) {
        Disk::FileHeader hdr{};
        size_t content = offset;
        // Read file header.
        if (file_.Load(&hdr, sizeof(hdr), offset) != sizeof(hdr)) {
            throw std::runtime_error("cannot read file header");
        }
        // Validate data integrity.
        if (hdr.crc != XXH32(&hdr, offsetof(Disk::FileHeader, crc), 0)) {
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

    Disk::FileHeader hdr{};
    // Read file header.
    if (file_.Load(&hdr, sizeof(hdr), tag.offset) != sizeof(hdr)) {
        throw std::runtime_error("cannot read file header");
    }
    // Validate data integrity.
    if (hdr.crc != XXH32(&hdr, offsetof(Disk::FileHeader, crc), 0)) {
        throw std::runtime_error("header data corruption");
    }

    if (tag.offset + sizeof(Disk::FileHeader) + hdr.stored > size_) {
        throw std::runtime_error("content data corruption");
    }

    return std::make_pair(
        std::string_view(data_ + tag.offset + sizeof(Disk::FileHeader), hdr.stored), hdr.Codec()
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
    Disk::FileHeader hdr{};
    // Read file header.
    if (file_.Load(&hdr, sizeof(hdr), oi->second.offset) != sizeof(hdr)) {
        throw std::runtime_error("cannot read file header");
    }
    // Validate data integrity.
    if (hdr.crc != XXH32(&hdr, offsetof(Disk::FileHeader, crc), 0)) {
        throw std::runtime_error("header data corruption");
    }

    // Load object content.
    return Object::Load(DataHeader::Make(hdr.Type(), hdr.Size()), [&](std::byte* buf, size_t buf_len) {
        const auto read_to_buffer = [&](std::byte* p, size_t size) {
            const auto content_offset = oi->second.offset + sizeof(Disk::FileHeader);
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

    const auto data_header = DataHeader::Make(type, content.size());
    const auto tag = Tag{.meta = data_header, .offset = uint32_t(size_), .portion = 0};

    const auto serialized_size = [](const HashId& id, const size_t buf_len) {
        return sizeof(Disk::FileHeader) + buf_len + sizeof(uint64_t) + id.Size();
    };

    const auto write_to_file = [&](const void* buf, size_t buf_len) {
        const uint64_t content_crc = XXH3_64bits(buf, buf_len);
        Disk::FileHeader file_header{};
        file_header.tag = Disk::FileHeader::MakeTag(codec_, type);
        file_header.original = content.size();
        // Setup length of stored data.
        file_header.stored = buf_len;
        // Setup header checksum.
        file_header.crc = XXH32(&file_header, offsetof(Disk::FileHeader, crc), 0);
        // Write file header.
        file_.Write(&file_header, sizeof(file_header));
        // Write file content.
        file_.Write(buf, buf_len);
        // Write content checksum.
        file_.Write(&content_crc, sizeof(content_crc));
        // Write key.
        file_.Write(id.Data(), id.Size());
        // Return total size of written bytes.
        return serialized_size(id, buf_len);
    };

    switch (codec_) {
        case Compression::None: {
            // Check available capacity.
            if (serialized_size(id, content.size()) > capacity_ - size_) {
                throw TableIsFull();
            }

            size_ += write_to_file(content.data(), content.size());
            break;
        }
        case Compression::Lz4: {
            auto buf_size = ::LZ4_compressBound(content.size());
            auto buf = std::make_unique_for_overwrite<char[]>(buf_size);

            int len = ::LZ4_compress_default(content.data(), buf.get(), content.size(), buf_size);

            if (len == 0) {
                throw std::runtime_error("cannot compress data");
            }
            // Check available capacity.
            if (serialized_size(id, len) > capacity_ - size_) {
                throw TableIsFull();
            }

            size_ += write_to_file(buf.get(), len);
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

size_t Leveled::PackTable::Size() const noexcept {
    return data_.size();
}

bool Leveled::PackTable::Exists(const HashId& id) const {
    return bool(FindIndexEntry(id, index_));
}

DataHeader Leveled::PackTable::GetMeta(const HashId& id) const {
    if (const auto& entry = FindIndexEntry(id, index_)) {
        return entry->GetMeta();
    }
    return DataHeader();
}

Object Leveled::PackTable::Load(const HashId& id, const DataType expected) const {
    const auto& entry = FindIndexEntry(id, index_);
    // No object.
    if (!entry) {
        return Object();
    }

    const DataHeader hdr = entry->GetMeta();
    // Type mismatch.
    if (expected != DataType::None && hdr.Type() != expected && hdr.Type() != DataType::Index) {
        return Object();
    }
    // Load objects's content.
    return Object::Load(hdr, [&](std::byte* buf, size_t buf_len) {
        const size_t offset = entry->GetOffset();
        uint32_t len;
        // Load content's size.
        if (offset + sizeof(len) > data_.size()) {
            throw std::runtime_error("cannot load length (overflow)");
        } else {
            std::memcpy(&len, data_.data() + offset, sizeof(len));
        }
        // Ensure no buffer overrun.
        if (offset + sizeof(len) + len > data_.size()) {
            throw std::runtime_error("cannot load content (overflow)");
        }
        if (len) {
            const int ret = ::LZ4_decompress_safe(
                reinterpret_cast<const char*>(data_.data()) + (offset + sizeof(len)), (char*)buf, len,
                buf_len
            );

            if (ret != int(buf_len)) {
                throw std::runtime_error(fmt::format("cannot decompres content '{}'", ret));
            }
        }
    });
}

void Leveled::PackTable::Put(const HashId&, const DataType, const std::string_view) {
    // Pack table is read-only.
}

std::shared_ptr<Leveled::PackTable> Leveled::PackTable::Merge(
    const std::vector<std::shared_ptr<PackTable>>& tables, const std::filesystem::path& path, size_t level
) {
    struct Iterator {
        std::span<const Disk::IndexEntry> index;
        size_t table;
        size_t idx;

        bool operator<(const Iterator& other) const noexcept {
            return other.index[other.idx].id < index[idx].id;
        }
    };

    File data_file = File::ForAppend(path / "pack.tmp");
    HashId::Builder builder;
    // Write pack table (compress)
    uint64_t offset = 0;

    std::priority_queue<Iterator> queue;
    std::vector<Disk::IndexEntry> index;

    for (size_t i = 0; i < tables.size(); ++i) {
        std::span<const Disk::IndexEntry> index(
            reinterpret_cast<const Disk::IndexEntry*>(
                reinterpret_cast<const uint32_t*>(tables[i]->index_.data()) + 256
            ),
            reinterpret_cast<const Disk::IndexEntry*>(tables[i]->index_.data() + tables[i]->index_.size())
        );

        queue.push(Iterator{.index = index, .table = i, .idx = 0});
    }

    const auto write_buffer = [&](const void* buf, const uint32_t len) {
        builder.Append(buf, len);
        // Copy data entry.
        data_file.Write(buf, len);
        // Advance pointer.
        offset += len;
    };

    while (!queue.empty()) {
        Iterator it = queue.top();
        queue.pop();

        if (index.empty() || index.back().id != it.index[it.idx].id) {
            index.emplace_back(
                Disk::IndexEntry::Make(it.index[it.idx].id, it.index[it.idx].GetMeta(), offset)
            );
            // Copy data.
            uint32_t len;
            // Length of the record.
            std::memcpy(&len, tables[it.table]->data_.data() + it.index[it.idx].GetOffset(), sizeof(len));
            // Copy record with length.
            write_buffer(tables[it.table]->data_.data() + it.index[it.idx].GetOffset(), len + sizeof(len));
        }

        if (++it.idx < it.index.size()) {
            queue.push(it);
        }
    }

    HashId data_hash = builder.Build();
    data_file.FlushData();

    // Write fanout table
    std::vector<uint32_t> fanout(256, index.size());

    for (auto it = index.begin(), end = index.end(); it != end;) {
        fanout[it->id.Data()[0]] = it - index.begin();

        it = std::upper_bound(it, end, it->id, [](const auto& val, const auto& item) {
            return val.Data()[0] < item.id.Data()[0];
        });
    }
    // Fill fanout gaps.
    for (ssize_t i = (ssize_t)fanout.size() - 2; i >= 0; --i) {
        if (fanout[i] == index.size()) {
            fanout[i] = fanout[i + 1];
        }
    }

    File index_file = File::ForAppend(path / "index.tmp");
    // Write fanout table.
    index_file.Write(fanout.data(), fanout.size() * sizeof(decltype(fanout)::value_type));
    // Write index.
    index_file.Write(index.data(), index.size() * sizeof(decltype(index)::value_type));
    // Ensure data has been written to disk.
    index_file.FlushData();

    // Validate pack table.
    CheckPackTable(PackTable(path / "index.tmp", path / "pack.tmp"), index);

    std::filesystem::rename(path / "pack.tmp", path / fmt::format("pack-{}.{:03}.pack", data_hash, level));
    std::filesystem::rename(
        path / "index.tmp", path / fmt::format("pack-{}.{:03}.index", data_hash, level)
    );

    // Remove processed packs.
    for (size_t i = 0; i < tables.size(); ++i) {
        std::filesystem::remove(tables[i]->index_path_);
        std::filesystem::remove(tables[i]->pack_path_);
    }

    return std::make_shared<PackTable>(
        path / fmt::format("pack-{}.{:03}.index", data_hash, level),
        path / fmt::format("pack-{}.{:03}.pack", data_hash, level)
    );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

Leveled::Leveled(std::filesystem::path path, const Options& options)
    : path_(std::move(path))
    , options_(options) {
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

void Leveled::Pack() {
    if (!options_.read_only) {
        std::unique_lock lock(mutex_);

        FinalizeNoLock(true);
    }
}

void Leveled::Rotate() {
    if (!options_.read_only) {
        std::unique_lock lock(mutex_);

        FinalizeNoLock(false);
    }
}

bool Leveled::Exists(const HashId& id) const {
    std::shared_lock lock(mutex_);

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
    std::shared_lock lock(mutex_);

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
    std::shared_lock lock(mutex_);

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
    FinalizeNoLock(false);
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

        if (li->second >= levels_.size()) {
            levels_.resize(li->second + 1);
        }

        levels_[li->second].emplace_back(std::make_shared<PackTable>(paths.first, paths.second));
    }

    // Load snapshots.
    for (const auto& [path, number] : snaps) {
        if (number) {
            // Finalized snapshot.
            auto p =
                std::make_shared<MemoryTable>(File::ForRead(path), options_.memtable_size, options_.codec);
            // Load oids.
            p->Restore(true);
            // Check table size.
            if (p->Size()) {
                snapshots_.emplace_back(std::move(p), path);
            }
        } else {
            auto p = std::make_shared<MemoryTable>(
                File::ForAppend(path), options_.memtable_size, options_.codec
            );
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

    return std::make_pair(
        std::make_shared<MemoryTable>(File::ForAppend(path), options_.memtable_size, options_.codec), path
    );
}

void Leveled::MergeSnapshots() {
    auto pack = RepackMemtables(snapshots_, path_ / "pack", options_.data_sync);

    for (size_t i = 0;; ++i) {
        if (i >= levels_.size()) {
            levels_.emplace_back();
        }

        levels_[i].push_back(pack);

        if (levels_[i].size() >= 4) {
            pack = PackTable::Merge(
                std::vector<std::shared_ptr<PackTable>>(
                    {levels_[i].begin(), std::next(levels_[i].begin(), 4)}
                ),
                path_ / "pack", i + 1
            );
            //
            levels_[i].erase(levels_[i].begin(), std::next(levels_[i].begin(), 4));
        } else {
            break;
        }
    }

    // Remove processed snapshots.
    for (const auto& [_, path] : snapshots_) {
        std::filesystem::remove(path);
    }

    snapshots_.clear();
}

void Leveled::FinalizeNoLock(bool pack) {
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
    if ((pack && !snapshots_.empty()) || snapshots_.size() >= options_.snapshots_to_pack) {
        MergeSnapshots();
    }
}

std::shared_ptr<Leveled::PackTable> Leveled::RepackMemtables(
    const std::vector<std::pair<std::shared_ptr<MemoryTable>, std::filesystem::path>>& snapshots,
    const std::filesystem::path path,
    bool sync
) {
    std::vector<std::pair<HashId, MemoryTable::Tag>> oids;
    std::vector<Disk::IndexEntry> index;

    // Merge all oids.
    for (size_t i = 0, end = snapshots.size(); i < end; ++i) {
        for (auto [id, tag] : snapshots[i].first->Ids()) {
            tag.portion = i;
            oids.emplace_back(id, tag);
        }
    }
    // Reorder oids.
    std::sort(oids.begin(), oids.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    // Ensure uniqueness of the oids.
    oids.erase(
        std::unique(
            oids.begin(), oids.end(), [](const auto& a, const auto& b) { return a.first == b.first; }
        ),
        oids.end()
    );

    index.reserve(oids.size());

    File data_file = File::ForAppend(path / "pack.tmp");
    HashId::Builder builder;
    uint64_t offset = 0;
    // Write pack table (compress).
    for (const auto& [id, tag] : oids) {
        const auto write_buffer = [&](const void* buf, const uint32_t len) {
            builder.Append(&len, sizeof(len)).Append(buf, len);
            // Copy data len entry.
            data_file.Write(&len, sizeof(len));
            // Copy data entry.
            data_file.Write(buf, len);
            // Advance pointer.
            offset += len + sizeof(len);
        };

        // Append index entry.
        index.emplace_back(Disk::IndexEntry::Make(id, tag.meta, offset));

        const auto [content, codec] = snapshots[tag.portion].first->Content(tag);

        switch (codec) {
            case Compression::None: {
                const auto buf_size = ::LZ4_compressBound(content.size());

                auto buf = std::make_unique_for_overwrite<char[]>(buf_size);

                const uint32_t len =
                    ::LZ4_compress_default(content.data(), buf.get(), content.size(), buf_size);

                if (len == 0) {
                    throw std::runtime_error(fmt::format("cannot compress data '{}'", len));
                }
                // Write data to file.
                write_buffer(buf.get(), len);
                break;
            }
            case Compression::Lz4: {
                // Write data to file.
                write_buffer(content.data(), content.size());
                break;
            }
        }
    }

    const HashId data_hash = builder.Build();
    // Ensure data has been written to disk.
    if (sync) {
        data_file.FlushData();
    }

    // Build fanout table.
    std::vector<uint32_t> fanout(256, index.size());

    for (auto it = index.begin(), end = index.end(); it != end;) {
        fanout[it->id.Data()[0]] = it - index.begin();

        it = std::upper_bound(it, end, it->id, [](const auto& val, const auto& item) {
            return val.Data()[0] < item.id.Data()[0];
        });
    }
    // Fill fanout gaps.
    for (ssize_t i = (ssize_t)fanout.size() - 2; i >= 0; --i) {
        if (fanout[i] == index.size()) {
            fanout[i] = fanout[i + 1];
        }
    }

    File index_file = File::ForAppend(path / "index.tmp");
    // Write fanout table.
    index_file.Write(fanout.data(), fanout.size() * sizeof(decltype(fanout)::value_type));
    // Write index.
    index_file.Write(index.data(), index.size() * sizeof(decltype(index)::value_type));
    // Ensure data has been written to disk.
    if (sync) {
        index_file.FlushData();
    }

    // Validate pack table.
    CheckPackTable(PackTable(path / "index.tmp", path / "pack.tmp"), index);

    std::filesystem::rename(path / "pack.tmp", path / fmt::format("pack-{}.{:03}.pack", data_hash, 0));
    std::filesystem::rename(path / "index.tmp", path / fmt::format("pack-{}.{:03}.index", data_hash, 0));

    return std::make_shared<PackTable>(
        path / fmt::format("pack-{}.{:03}.index", data_hash, 0),
        path / fmt::format("pack-{}.{:03}.pack", data_hash, 0)
    );
}

} // namespace Vcs::Store
