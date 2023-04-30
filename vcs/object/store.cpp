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
    constexpr Impl() noexcept
        : cache_(false) {
    }

    Impl(std::shared_ptr<Impl> other, std::shared_ptr<Backend> backend, bool cache) noexcept
        : backend_(std::move(backend))
        , upsteram_(std::move(other))
        , cache_(cache) {
        assert(backend_);
        assert(upsteram_);
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

    void Put(const HashId& id, const DataType type, const std::string_view content) {
        if (backend_) {
            backend_->Put(id, type, content);
        }
        if (upsteram_) {
            upsteram_->Put(id, type, content);
        }
    }

private:
    std::shared_ptr<Backend> backend_;
    std::shared_ptr<Impl> upsteram_;
    /// Put objects received from upstream into local backend.
    bool cache_;
};

Datastore::Datastore(const size_t chunk_size)
    : chunk_size_(chunk_size)
    , impl_(std::make_shared<Impl>()) {
}

Datastore::Datastore(const Datastore& other, std::shared_ptr<Backend> backend, bool cache)
    : chunk_size_(other.chunk_size_)
    , impl_(std::make_shared<Impl>(other.impl_, std::move(backend), cache)) {
}

Datastore::~Datastore() = default;

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

HashId Datastore::Put(const DataType type, const std::string_view content) {
    const auto put_single_object = [this](HashId id, const DataType type, const std::string_view content) {
        id = bool(id) ? id : HashId::Make(type, content);

        impl_->Put(id, type, content);

        return id;
    };

    // The data is small enough to be saved as single object.
    if (chunk_size_ >= (DataHeader::Make(type, content.size()).Bytes() + content.size())) {
        return put_single_object(HashId(), type, content);
    }

    IndexBuilder builder(HashId::Make(type, content), type);
    // Split content by parts.
    for (size_t offset = 0, end = content.size(); offset != end;) {
        const size_t size = std::min(chunk_size_, content.size() - offset);
        const HashId& id = HashId::Make(DataType::Blob, content.substr(offset, size));

        // Part of the content is saved as a blob object.
        put_single_object(id, DataType::Blob, content.substr(offset, size));
        // Save part.
        builder.Append(id, size);

        offset += size;
    }

    return put_single_object(HashId(), DataType::Index, builder.Serialize());
}

} // namespace Vcs
