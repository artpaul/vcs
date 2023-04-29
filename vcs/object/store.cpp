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

DataHeader Datastore::GetMeta(const HashId& id, bool resolve) const {
    const auto meta = DoGetMeta(id);

    if (resolve && meta.Type() == DataType::Index) {
        return static_cast<DataHeader>(LoadIndex(id));
    } else {
        return meta;
    }
}

DataType Datastore::GetType(const HashId& id, bool resolve) const {
    return GetMeta(id, resolve).Type();
}

bool Datastore::IsExists(const HashId& id) const {
    return DoIsExists(id);
}

Object Datastore::Load(const HashId& id) const {
    return DoLoad(id, DataType::None);
}

Object Datastore::Load(const HashId& id, DataType expected) const {
    return DoLoad(id, expected);
}

Blob Datastore::LoadBlob(const HashId& id) const {
    const auto obj = DoLoad(id, DataType::Blob);

    if (obj.Type() == DataType::Index) {
        return ConstructObjectFromIndex(obj.AsIndex(), this).AsBlob();
    } else {
        return obj.AsBlob();
    }
}

Commit Datastore::LoadCommit(const HashId& id) const {
    const auto obj = DoLoad(id, DataType::Commit);

    if (obj.Type() == DataType::Index) {
        return ConstructObjectFromIndex(obj.AsIndex(), this).AsCommit();
    } else {
        return obj.AsCommit();
    }
}

Index Datastore::LoadIndex(const HashId& id) const {
    return DoLoad(id, DataType::Index).AsIndex();
}

Renames Datastore::LoadRenames(const HashId& id) const {
    const auto obj = DoLoad(id, DataType::Renames);

    if (obj.Type() == DataType::Index) {
        return ConstructObjectFromIndex(obj.AsIndex(), this).AsRenames();
    } else {
        return obj.AsRenames();
    }
}

Tree Datastore::LoadTree(const HashId& id) const {
    const auto obj = DoLoad(id, DataType::Tree);

    if (obj.Type() == DataType::Index) {
        return ConstructObjectFromIndex(obj.AsIndex(), this).AsTree();
    } else {
        return obj.AsTree();
    }
}

HashId Datastore::Put(const DataType type, const std::string_view content) {
    // The data is small enough to be saved as single object.
    if (chunk_size_ >= (DataHeader::Make(type, content.size()).Bytes() + content.size())) {
        return DoPut(type, content);
    }

    IndexBuilder builder(HashId::Make(type, content), type);
    // Split content by parts.
    for (size_t offset = 0, end = content.size(); offset != end;) {
        const size_t size = std::min(chunk_size_, content.size() - offset);

        builder.Append(
            // Part of the content is saved as a blob object.
            DoPut(DataType::Blob, content.substr(offset, size)),
            // Size of the part.
            size
        );

        offset += size;
    }

    return DoPut(DataType::Index, builder.Serialize());
}

} // namespace Vcs
