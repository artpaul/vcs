#include "store.h"
#include "serialize.h"

#include <contrib/fmt/fmt/format.h>

namespace Vcs {
namespace {

Object ConstructObjectFromIndex(const Index& index, const Datastore* odb) {
    return Object::Load(DataHeader::Make(index.Type(), index.Size()), [&](std::byte* buf, size_t) {
        for (const auto& p : index.Parts()) {
            const auto blob = odb->LoadBlob(p.Id());
            // Validate size of the part.
            if (blob.Size() != p.Size()) {
                throw std::runtime_error(
                    fmt::format("invalid blob size: expected is {} but actual is {}", p.Size(), blob.Size())
                );
            }
            // Copy data.
            std::memcpy(buf, blob.Data(), p.Size());
            // Move pointer.
            buf += p.Size();
        }
    });
}

} // namespace

class Datastore::Impl {
public:
    constexpr Impl(const size_t chunk_size) noexcept
        : chunk_size_(chunk_size)
        , cache_(false) {
    }

    Impl(std::shared_ptr<Impl> other, std::shared_ptr<Backend> backend, bool cache) noexcept
        : backend_(std::move(backend))
        , upsteram_(std::move(other))
        , chunk_size_(upsteram_->chunk_size_)
        , cache_(cache) {
        assert(backend_);
    }

    size_t ChunkSize() const noexcept {
        return chunk_size_;
    }

    DataHeader GetMeta(const HashId& id) const {
        if (backend_) {
            if (auto meta = backend_->GetMeta(id)) {
                return meta;
            }
        }
        if (upsteram_) {
            if (auto meta = upsteram_->GetMeta(id)) {
                return meta;
            }
        }
        return DataHeader();
    }

    bool Exists(const HashId& id) const {
        if (backend_) {
            if (backend_->Exists(id)) {
                return true;
            }
        }
        if (upsteram_) {
            if (upsteram_->Exists(id)) {
                return true;
            }
        }
        return false;
    }

    Object Load(const HashId& id, const DataType expected) const {
        if (backend_) {
            if (auto obj = backend_->Load(id, expected)) {
                return obj;
            }
        }
        if (upsteram_) {
            if (auto obj = upsteram_->Load(id, expected)) {
                if (backend_ && cache_) {
                    backend_->Put(id, obj);
                }
                return obj;
            }
        }
        return Object();
    }

    HashId Put(const DataType type, const std::string_view content) {
        HashId id = HashId::Make(type, content);

        PutImpl(id, type, content);

        return id;
    }

private:
    void PutImpl(const HashId& id, const DataType type, const std::string_view content) {
        if (chunk_size_ < content.size()) {
            throw std::runtime_error(
                fmt::format("content size {} exceeds size of chunk {}", content.size(), chunk_size_)
            );
        }

        if (backend_) {
            backend_->Put(id, type, content);
        }
        if (upsteram_) {
            upsteram_->PutImpl(id, type, content);
        }
    }

private:
    std::shared_ptr<Backend> backend_;
    std::shared_ptr<Impl> upsteram_;
    size_t chunk_size_;
    /// Put objects received from the upstream into local backend.
    bool cache_;
};

Datastore::Datastore(const size_t chunk_size)
    : impl_(std::make_shared<Impl>(chunk_size)) {
}

Datastore::Datastore(const Datastore& other, std::shared_ptr<Backend> backend, bool cache)
    : impl_(std::make_shared<Impl>(other.impl_, std::move(backend), cache)) {
}

Datastore::~Datastore() = default;

size_t Datastore::GetChunkSize() const noexcept {
    return impl_->ChunkSize();
}

DataHeader Datastore::GetMeta(const HashId& id, bool resolve) const {
    if (auto meta = impl_->GetMeta(id)) {
        if (resolve && meta.Type() == DataType::Index) {
            return static_cast<DataHeader>(LoadIndex(id));
        } else {
            return meta;
        }
    }
    return DataHeader();
}

DataType Datastore::GetType(const HashId& id, bool resolve) const {
    return GetMeta(id, resolve).Type();
}

bool Datastore::IsExists(const HashId& id) const {
    return impl_->Exists(id);
}

Object Datastore::Load(const HashId& id) const {
    return Load(id, DataType::None);
}

Object Datastore::Load(const HashId& id, DataType expected) const {
    return impl_->Load(id, expected);
}

Blob Datastore::LoadBlob(const HashId& id) const {
    const auto obj = Load(id, DataType::Blob);

    if (obj.Type() == DataType::Index) {
        return ConstructObjectFromIndex(obj.AsIndex(), this).AsBlob();
    } else {
        return obj.AsBlob();
    }
}

Commit Datastore::LoadCommit(const HashId& id) const {
    const auto obj = Load(id, DataType::Commit);

    if (obj.Type() == DataType::Index) {
        return ConstructObjectFromIndex(obj.AsIndex(), this).AsCommit();
    } else {
        return obj.AsCommit();
    }
}

Index Datastore::LoadIndex(const HashId& id) const {
    return Load(id, DataType::Index).AsIndex();
}

Renames Datastore::LoadRenames(const HashId& id) const {
    const auto obj = Load(id, DataType::Renames);

    if (obj.Type() == DataType::Index) {
        return ConstructObjectFromIndex(obj.AsIndex(), this).AsRenames();
    } else {
        return obj.AsRenames();
    }
}

Tree Datastore::LoadTree(const HashId& id) const {
    const auto obj = Load(id, DataType::Tree);

    if (obj.Type() == DataType::Index) {
        return ConstructObjectFromIndex(obj.AsIndex(), this).AsTree();
    } else {
        return obj.AsTree();
    }
}

std::pair<HashId, DataType> Datastore::Put(const DataType type, const std::string_view content) {
    // The data is small enough to be saved as single object.
    if (impl_->ChunkSize() >= content.size()) {
        return std::make_pair(impl_->Put(type, content), type);
    }

    IndexBuilder builder(HashId::Make(type, content), type);
    // Split content by parts.
    for (size_t offset = 0, end = content.size(); offset != end;) {
        const size_t size = std::min(impl_->ChunkSize(), content.size() - offset);
        // Save part the content as a blob object.
        builder.Append(impl_->Put(DataType::Blob, content.substr(offset, size)), size);

        offset += size;
    }

    // Save index.
    return std::make_pair(impl_->Put(DataType::Index, builder.Serialize()), DataType::Index);
}

std::pair<HashId, DataType> Datastore::Put(const DataHeader meta, InputStream input) {
    const auto put_single_object =
        [this](const DataHeader meta, InputStream& input, HashId::Builder* hasher) {
            auto buf = std::make_unique_for_overwrite<char[]>(meta.Size());
            auto content = std::string_view(buf.get(), meta.Size());

            // Load content of the object into the temporary buffer.
            if (const size_t read = input.Load(buf.get(), content.size()); read != content.size()) {
                throw std::runtime_error(fmt::format(
                    "unexpected end of stream: expected is {} but actual is {}", content.size(), read
                ));
            }
            // Append data to the hasher.
            if (hasher) {
                hasher->Append(content);
            }
            // Store object.
            return impl_->Put(meta.Type(), content);
        };

    // The data is small enough to be saved as single object.
    if (impl_->ChunkSize() >= meta.Size()) {
        return std::make_pair(put_single_object(meta, input, nullptr), meta.Type());
    }

    HashId::Builder hasher;
    IndexBuilder index(HashId(), meta.Type());
    // Split content by parts.
    for (size_t offset = 0, end = meta.Size(); offset != end;) {
        const size_t size = std::min(impl_->ChunkSize(), meta.Size() - offset);
        // Save part the content as a blob object.
        index.Append(put_single_object(DataHeader::Make(DataType::Blob, size), input, &hasher), size);

        offset += size;
    }
    // Set id of the whole object.
    index.SetId(hasher.Build());

    // Save index.
    return std::make_pair(impl_->Put(DataType::Index, index.Serialize()), DataType::Index);
}

} // namespace Vcs
