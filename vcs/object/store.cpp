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

Datastore::Datastore(const size_t chunk_size) noexcept
    : chunk_size_(chunk_size) {
}

Datastore::Datastore(const Datastore& other, std::shared_ptr<Backend> backend, bool cache)
    : chunk_size_(other.chunk_size_)
    , backends_(other.backends_) {
    backends_.emplace_back(std::move(backend), cache);
}

DataHeader Datastore::GetMeta(const HashId& id, bool resolve) const {
    for (auto ri = backends_.rbegin(); ri != backends_.rend(); ++ri) {
        if (auto meta = ri->first->GetMeta(id)) {
            if (resolve && meta.Type() == DataType::Index) {
                return static_cast<DataHeader>(LoadIndex(id));
            } else {
                return meta;
            }
        }
    }
    return DataHeader();
}

DataType Datastore::GetType(const HashId& id, bool resolve) const {
    return GetMeta(id, resolve).Type();
}

bool Datastore::IsExists(const HashId& id) const {
    for (auto ri = backends_.rbegin(); ri != backends_.rend(); ++ri) {
        if (ri->first->Exists(id)) {
            return true;
        }
    }
    return false;
}

Object Datastore::Load(const HashId& id) const {
    return Load(id, DataType::None);
}

Object Datastore::Load(const HashId& id, DataType expected) const {
    bool cache = false;
    for (auto ri = backends_.rbegin(); ri != backends_.rend(); ++ri) {
        cache |= ri->second;
        if (auto obj = ri->first->Load(id, expected)) {
            // Put object in cache if needed.
            if (cache) {
                for (auto ci = backends_.rbegin(); ci != ri; ++ci) {
                    if (ci->second) {
                        ci->first->Put(id, obj);
                    }
                }
            }
            // Return object.
            return obj;
        }
    }
    return Object();
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

        for (auto ri = backends_.rbegin(); ri != backends_.rend(); ++ri) {
            ri->first->Put(id, type, content);
        }

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
