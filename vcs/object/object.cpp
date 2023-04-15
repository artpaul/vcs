#include "object.h"

#include <vcs/api/fbs/commit.fb.h>
#include <vcs/api/fbs/index.fb.h>
#include <vcs/api/fbs/tree.fb.h>

#include <contrib/fmt/fmt/compile.h>
#include <contrib/fmt/fmt/format.h>

#include <bit>

namespace Vcs {
namespace {

struct Adaptor {
    struct Tag {
        uint64_t type : 4;
        uint64_t size : 56;
    };

    static inline std::byte* GetData(void* data) noexcept {
        return std::bit_cast<std::byte*>(data) + sizeof(Tag);
    }

    static inline Tag* GetTag(void* data) noexcept {
        return std::bit_cast<Tag*>(data);
    }
};

/// Ensure Tag is 8 bytes in size.
static_assert(sizeof(Adaptor::Tag) == 8);

/// Checks that the object may be treated as it has the given type.
template <DataType Type, size_t Length>
static void ValidateTypecast(const std::shared_ptr<std::byte[]>& data, const char (&name)[Length]) {
    if (!data) {
        throw std::runtime_error(fmt::format(FMT_COMPILE("cannot convert null object to a {}"), name));
    }
    if (DataType(Adaptor::GetTag(data.get())->type) != Type) {
        throw std::runtime_error(fmt::format(FMT_COMPILE("object not a {}"), name));
    }
}

} // namespace

///////////////////////////////////////////////////////////////////////////////////////////////////////////

Object::Object(std::shared_ptr<std::byte[]>&& data) noexcept
    : ObjectBase(std::move(data)) {
}

Object::~Object() = default;

Object Object::Load(const DataType type, const std::string_view content) {
    auto buf = std::make_shared_for_overwrite<std::byte[]>(sizeof(Adaptor::Tag) + content.size());
    // Set object's tag.
    Adaptor::GetTag(buf.get())->type = uint8_t(type);
    Adaptor::GetTag(buf.get())->size = content.size();
    // Set content.
    std::memcpy(Adaptor::GetData(buf.get()), content.data(), content.size());
    // Done.
    return Object(std::move(buf));
}

Object Object::Load(const DataHeader header, const std::function<void(std::byte* buf, size_t len)>& cb) {
    auto size = header.Size();
    auto buf = std::make_shared_for_overwrite<std::byte[]>(sizeof(Adaptor::Tag) + size);
    // Set object's tag.
    Adaptor::GetTag(buf.get())->type = uint8_t(header.Type());
    Adaptor::GetTag(buf.get())->size = size;
    // Initialize content.
    cb(Adaptor::GetData(buf.get()), size);
    // Done.
    return Object(std::move(buf));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

Blob Object::AsBlob() const {
    ValidateTypecast<DataType::Blob>(data_, "blob");
    return Blob(data_);
}

Commit Object::AsCommit() const {
    ValidateTypecast<DataType::Commit>(data_, "commit");
    return Commit(data_);
}

Index Object::AsIndex() const {
    ValidateTypecast<DataType::Index>(data_, "index");
    return Index(data_);
}

Tree Object::AsTree() const {
    ValidateTypecast<DataType::Tree>(data_, "tree");
    return Tree(data_);
}

const void* Object::Data() const noexcept {
    return data_ ? Adaptor::GetData(data_.get()) : nullptr;
}

uint64_t Object::Size() const noexcept {
    return data_ ? Adaptor::GetTag(data_.get())->size : 0;
}

DataType Object::Type() const noexcept {
    return data_ ? DataType(Adaptor::GetTag(data_.get())->type) : DataType::None;
}

Blob::Blob(const std::shared_ptr<std::byte[]>& data) noexcept
    : ObjectBase(data) {
    assert(data_);
}

const char* Blob::Data() const noexcept {
    return reinterpret_cast<const char*>(Adaptor::GetData(data_.get()));
}

uint64_t Blob::Size() const noexcept {
    return Adaptor::GetTag(data_.get())->size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

constexpr Commit::Attribute::Attribute(const void* p) noexcept
    : p_(p) {
}

std::string_view Commit::Attribute::Name() const {
    if (const auto name = static_cast<const Fbs::Attribute*>(p_)->name()) {
        return std::string_view(name->data(), name->size());
    }
    return std::string_view();
}

std::string_view Commit::Attribute::Value() const {
    if (const auto value = static_cast<const Fbs::Attribute*>(p_)->value()) {
        return std::string_view(value->data(), value->size());
    }
    return std::string_view();
}

constexpr Commit::Signature::Signature(const void* p) noexcept
    : p_(p) {
}

std::string_view Commit::Signature::Name() const {
    if (p_) {
        if (const auto name = static_cast<const Fbs::Signature*>(p_)->name()) {
            return std::string_view(name->data(), name->size());
        }
    }
    return std::string_view();
}

std::string_view Commit::Signature::Id() const {
    if (p_) {
        if (const auto id = static_cast<const Fbs::Signature*>(p_)->id()) {
            return std::string_view(id->data(), id->size());
        }
    }
    return std::string_view();
}

uint64_t Commit::Signature::When() const {
    if (p_) {
        return static_cast<const Fbs::Signature*>(p_)->when();
    }
    return 0;
}

constexpr Commit::Signature::operator bool() const noexcept {
    return bool(p_);
}

Commit::Attribute Commit::RangeAttribute::Item(const void* p, size_t i) {
    return Attribute(static_cast<const flatbuffers::Vector<flatbuffers::Offset<Fbs::Attribute>>*>(p)
                         ->GetAs<Fbs::Attribute>(i));
}

size_t Commit::RangeAttribute::Size(const void* p) {
    return static_cast<const flatbuffers::Vector<flatbuffers::Offset<Fbs::Attribute>>*>(p)->size();
}

HashId Commit::RangeParents::Item(const void* p, size_t i) {
    return HashId::FromBytes(static_cast<const flatbuffers::Vector<uint8_t>*>(p)->data() + (20 * i), 20);
}

size_t Commit::RangeParents::Size(const void* p) {
    return static_cast<const flatbuffers::Vector<uint8_t>*>(p)->size() / 20;
}

Commit::Commit(const std::shared_ptr<std::byte[]>& data) noexcept
    : ObjectBase(data) {
    assert(data_);
}

Commit Commit::Load(const std::string_view data) {
    return Object::Load(DataType::Commit, data).AsCommit();
}

RepeatedField<Commit::Attribute, Commit::RangeAttribute> Commit::Attributes() const {
    return RepeatedField<Attribute, RangeAttribute>(
        Fbs::GetCommit(Adaptor::GetData(data_.get()))->attributes()
    );
}

Commit::Signature Commit::Author() const {
    return Signature(Fbs::GetCommit(Adaptor::GetData(data_.get()))->author());
}

Commit::Signature Commit::Committer() const {
    return Signature(Fbs::GetCommit(Adaptor::GetData(data_.get()))->committer());
}

uint64_t Commit::Generation() const {
    return Fbs::GetCommit(Adaptor::GetData(data_.get()))->generation();
}

std::string_view Commit::Message() const {
    if (const auto message = Fbs::GetCommit(Adaptor::GetData(data_.get()))->message()) {
        return std::string_view(message->data(), message->size());
    }
    return std::string_view();
}

RepeatedField<HashId, Commit::RangeParents> Commit::Parents() const {
    return RepeatedField<HashId, RangeParents>(Fbs::GetCommit(Adaptor::GetData(data_.get()))->parents());
}

HashId Commit::Renames() const {
    if (const auto renames = Fbs::GetCommit(Adaptor::GetData(data_.get()))->renames()) {
        return HashId::FromBytes(static_cast<const flatbuffers::Vector<uint8_t>*>(renames)->data(), 20);
    }
    return HashId();
}

uint64_t Commit::Timestamp() const {
    const auto commit = Fbs::GetCommit(Adaptor::GetData(data_.get()));

    if (const auto commiter = commit->committer()) {
        return commiter->when();
    } else if (const auto author = commit->author()) {
        return author->when();
    }

    return 0;
}

HashId Commit::Tree() const {
    if (auto id = Fbs::GetCommit(Adaptor::GetData(data_.get()))->tree()) {
        return HashId::FromBytes(id->data(), id->size());
    }
    return HashId();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

constexpr Index::Part::Part(const void* p) noexcept
    : p_(p) {
    assert(p_);
}

HashId Index::Part::Id() const {
    if (const auto id = static_cast<const Fbs::Part*>(p_)->id()) {
        return HashId::FromBytes(id->data(), id->size());
    }
    return HashId();
}

uint64_t Index::Part::Size() const {
    return static_cast<const Fbs::Part*>(p_)->size();
}

Index::Part Index::RangeParts::Item(const void* p, size_t i) {
    return Part(
        static_cast<const flatbuffers::Vector<flatbuffers::Offset<Fbs::Part>>*>(p)->GetAs<Fbs::Part>(i)
    );
}

size_t Index::RangeParts::Size(const void* p) {
    return static_cast<const flatbuffers::Vector<flatbuffers::Offset<Fbs::Part>>*>(p)->size();
}

Index::Index(const std::shared_ptr<std::byte[]>& data) noexcept
    : ObjectBase(data) {
    assert(data_);
}

HashId Index::Id() const {
    if (const auto id = Fbs::GetIndex(Adaptor::GetData(data_.get()))->id()) {
        return HashId::FromBytes(id->data(), id->size());
    }
    return HashId();
}

uint64_t Index::Size() const {
    uint64_t size = 0;
    for (const auto p : Parts()) {
        size += p.Size();
    }
    return size;
}

DataType Index::Type() const {
    return DataType(Fbs::GetIndex(Adaptor::GetData(data_.get()))->type());
}

RepeatedField<Index::Part, Index::RangeParts> Index::Parts() const {
    return RepeatedField<Part, RangeParts>(Fbs::GetIndex(Adaptor::GetData(data_.get()))->parts());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

constexpr Tree::Entry::Entry(const void* p) noexcept
    : p_(p) {
}

HashId Tree::Entry::Id() const {
    if (const auto id = static_cast<const Fbs::TreeEntry*>(p_)->id()) {
        return HashId::FromBytes(id->data(), id->size());
    }
    return HashId();
}

std::string_view Tree::Entry::Name() const {
    if (const auto name = static_cast<const Fbs::TreeEntry*>(p_)->name()) {
        return std::string_view(name->data(), name->size());
    }
    return std::string_view();
}

uint64_t Tree::Entry::Size() const {
    return static_cast<const Fbs::TreeEntry*>(p_)->size();
}

PathType Tree::Entry::Type() const {
    return PathType(static_cast<const Fbs::TreeEntry*>(p_)->type());
}

Tree::Entry Tree::RangeEntries::Item(const void* p, size_t i) {
    return Entry(static_cast<const flatbuffers::Vector<flatbuffers::Offset<Fbs::TreeEntry>>*>(p)
                     ->GetAs<Fbs::TreeEntry>(i));
}

size_t Tree::RangeEntries::Size(const void* p) {
    return static_cast<const flatbuffers::Vector<flatbuffers::Offset<Fbs::TreeEntry>>*>(p)->size();
}

Tree::Tree(const std::shared_ptr<std::byte[]>& data) noexcept
    : ObjectBase(data) {
    assert(data_);
}

Tree Tree::Load(const std::string_view data) {
    return Object::Load(DataType::Tree, data).AsTree();
}

RepeatedField<Tree::Entry, Tree::RangeEntries> Tree::Entries() const {
    return RepeatedField<Entry, RangeEntries>(Fbs::GetTree(Adaptor::GetData(data_.get()))->entries());
}

std::optional<Tree::Entry> Tree::Find(const std::string_view name) const {
    if (const auto entries = Fbs::GetTree(Adaptor::GetData(data_.get()))->entries()) {
        size_t l = 0;
        size_t r = entries->size();

        while (l < r) {
            const auto m = l + ((r - l) / 2);
            const auto e = entries->GetAs<Fbs::TreeEntry>(m);
            const auto cmp = name.compare(std::string_view(e->name()->data(), e->name()->size()));

            if (cmp == 0) {
                return Entry(e);
            } else if (cmp < 0) {
                r = m;
            } else {
                l = m + 1;
            }
        }
    }
    return std::nullopt;
}

bool Tree::Empty() const {
    if (const auto entries = Fbs::GetTree(Adaptor::GetData(data_.get()))->entries()) {
        return entries->size() == 0;
    }
    return true;
}

} // namespace Vcs
