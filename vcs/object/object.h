#pragma once

#include "data.h"
#include "hashid.h"
#include "path.h"

#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Vcs {

class Blob;
class Commit;
class Index;
class Renames;
class Tag;
class Tree;

template <typename T>
class ObjectBase {
public:
    explicit constexpr operator bool() const noexcept {
        return bool(data_);
    }

    constexpr T& operator=(const T& other) {
        if (this != &other) {
            data_ = other.data_;
        }
        return *this;
    }

    constexpr T& operator=(T&& other) noexcept {
        if (this != &other) {
            data_.swap(other.data_);
        }
        return *this;
    }

protected:
    constexpr ObjectBase() noexcept = default;

    ObjectBase(const std::shared_ptr<std::byte[]>& data) noexcept
        : data_(data) {
    }

    ObjectBase(std::shared_ptr<std::byte[]>&& data) noexcept
        : data_(std::move(data)) {
    }

protected:
    std::shared_ptr<std::byte[]> data_;
};

template <typename T, typename A>
class RepeatedField {
public:
    class iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const T;
        using pointer = const T*;
        using reference = const T&;

        constexpr iterator(const void* p, size_t i) noexcept
            : p_(p)
            , i_(i) {
        }

        const T operator*() const {
            assert(i_ < A::Size(p_));
            return A::Item(p_, i_);
        }

        const T operator[](const size_t n) const {
            assert(i_ + n < A::Size(p_));
            return A::Item(p_, i_ + n);
        }

        constexpr iterator& operator++() noexcept {
            i_++;
            return *this;
        }

        constexpr iterator operator++(int) noexcept {
            return iterator(p_, i_++);
        }

        constexpr iterator& operator--() noexcept {
            --i_;
            return *this;
        }

        constexpr iterator operator--(int) noexcept {
            return iterator(p_, i_--);
        }

        constexpr iterator& operator+=(const difference_type n) noexcept {
            i_ += n;
            return *this;
        }

        constexpr iterator& operator-=(const difference_type n) noexcept {
            assert(difference_type(i_) >= n);
            i_ -= n;
            return *this;
        }

        constexpr bool operator==(const iterator& other) const noexcept {
            return p_ == other.p_ && i_ == other.i_;
        }

        constexpr auto operator<=>(const iterator& other) const noexcept {
            return std::make_tuple(p_, i_) <=> std::make_tuple(other.p_, other.i_);
        }

        friend constexpr iterator operator+(const iterator& lhs, const difference_type n) noexcept {
            return iterator(lhs.p_, lhs.i_ + n);
        }

        friend constexpr iterator operator+(const difference_type n, const iterator& lhs) noexcept {
            return iterator(lhs.p_, lhs.i_ + n);
        }

        friend constexpr iterator operator-(const iterator& lhs, const difference_type n) noexcept {
            return iterator(lhs.p_, lhs.i_ - n);
        }

        friend constexpr difference_type operator-(const iterator& lhs, const iterator& rhs) noexcept {
            assert(lhs.p_ == rhs.p_);
            return lhs.i_ - rhs.i_;
        }

    private:
        const void* const p_;
        size_t i_;
    };

public:
    constexpr RepeatedField(const void* p) noexcept
        : p_(p) {
    }

    constexpr iterator begin() const noexcept {
        return iterator(p_, 0);
    }

    constexpr iterator end() const {
        return iterator(p_, size());
    }

    bool empty() const {
        return size() == 0;
    }

    size_t size() const {
        return p_ ? A::Size(p_) : 0;
    }

    T operator[](const size_t i) const {
        assert(p_);
        assert(i < size());
        return A::Item(p_, i);
    }

    explicit operator bool() const {
        return !empty();
    }

private:
    const void* const p_;
};

/**
 * Opaque data object.
 */
class Object final : public ObjectBase<Object> {
public:
    constexpr Object() noexcept = default;
    ~Object();

    /** Loads object content. */
    static Object Load(const DataType type, const std::string_view content);

    /** Loads object content. */
    static Object Load(const DataHeader header, const std::function<void(std::byte* buf, size_t len)>& cb);

public:
    /// Treat object as a blob.
    Blob AsBlob() const;

    /// Treat object as a commit.
    Commit AsCommit() const;

    /// Treat object as an index.
    Index AsIndex() const;

    /// Treat object as a rename table.
    Renames AsRenames() const;

    /// Treat object as a tree.
    Tree AsTree() const;

    /// Serialized content of the object.
    const void* Data() const noexcept;

    /// Size of the object's content.
    uint64_t Size() const noexcept;

    /// Type of the object.
    DataType Type() const noexcept;

private:
    Object(std::shared_ptr<std::byte[]>&& data) noexcept;
};

/**
 * Plain binary data.
 */
class Blob final : public ObjectBase<Blob> {
    friend class Object;

public:
    /// Content of a file or a symlink.
    const char* Data() const noexcept;

    /// Size of the content.
    uint64_t Size() const noexcept;

public:
    operator std::string_view() const noexcept {
        return std::string_view(Data(), Size());
    }

private:
    Blob(const std::shared_ptr<std::byte[]>& data) noexcept;
};

/**
 * Commit object.
 */
class Commit final : public ObjectBase<Commit> {
    friend class Object;

public:
    class Attribute {
    public:
        explicit constexpr Attribute(const void* p) noexcept;

        /// Name of the attribute.
        std::string_view Name() const;

        /// Value of the attribute.
        std::string_view Value() const;

    private:
        const void* const p_;
    };

    class Signature {
    public:
        explicit constexpr Signature(const void* p) noexcept;

        std::string_view Id() const;

        std::string_view Name() const;

        uint64_t When() const;

        explicit constexpr operator bool() const noexcept;

    private:
        const void* const p_;
    };

    struct RangeAttribute {
        static Attribute Item(const void* p, size_t i);
        static size_t Size(const void* p);
    };

    struct RangeParents {
        static HashId Item(const void* p, size_t i);
        static size_t Size(const void* p);
    };

public:
    /// Loads commit object from memory buffer.
    static Commit Load(const std::string_view data);

public:
    /// List of user-defined attributes.
    RepeatedField<Attribute, RangeAttribute> Attributes() const;

    /// The original author of the code.
    Signature Author() const;

    /// A person who committed the code.
    Signature Committer() const;

    /// Generation number.
    uint64_t Generation() const;

    /// Description of the commit.
    std::string_view Message() const;

    /// Range of parent commits.
    RepeatedField<HashId, RangeParents> Parents() const;

    /// Id of an object with history adjustments for the root tree.
    HashId Renames() const;

    /// Creation timestamp in UTC.
    uint64_t Timestamp() const;

    /// Identifier of the root tree.
    HashId Tree() const;

private:
    Commit(const std::shared_ptr<std::byte[]>& data) noexcept;
};

/**
 * The index holds references to the parts of object's content.
 */
class Index final : public ObjectBase<Index> {
    friend class Object;

public:
    class Part {
    public:
        explicit constexpr Part(const void* p) noexcept;

        /// Identifier of the object.
        HashId Id() const;

        /// Size of the object.
        uint64_t Size() const;

    private:
        const void* const p_;
    };

    struct RangeParts {
        static Part Item(const void* p, size_t i);
        static size_t Size(const void* p);
    };

public:
    /// Identfier of an original object.
    HashId Id() const;

    /// Size of the object.
    uint64_t Size() const;

    /// Type of the object.
    DataType Type() const;

    /// Range of an object parts.
    RepeatedField<Part, RangeParts> Parts() const;

    /// Makes DataHeader.
    explicit operator DataHeader() const {
        return DataHeader::Make(Type(), Size());
    }

private:
    Index(const std::shared_ptr<std::byte[]>& data) noexcept;
};

class Renames final : public ObjectBase<Renames> {
    friend class Object;

public:
    class CopyInfo {
    public:
        explicit constexpr CopyInfo(const void* p, const size_t i) noexcept;

        /// Source revision.
        HashId CommitId() const;

        /// Source path.
        std::string_view Source() const;

        /// Target path.
        std::string_view Path() const;

    private:
        const void* const p_;
        const size_t i_;
    };

    struct RangeCommits {
        static HashId Item(const void* p, size_t i);
        static size_t Size(const void* p);
    };

    struct RangeCopyInfo {
        static CopyInfo Item(const void* p, size_t i);
        static size_t Size(const void* p);
    };

    struct RangeReplaces {
        static std::string_view Item(const void* p, size_t i);
        static size_t Size(const void* p);
    };

public:
    /// Loads renames object from memory buffer.
    static Renames Load(const std::string_view data);

public:
    /// Dense list of source commits.
    RepeatedField<HashId, RangeCommits> Commits() const;

    /// List of copied entries.
    RepeatedField<CopyInfo, RangeCopyInfo> Copies() const;

    /// List of replaced entries.
    RepeatedField<std::string_view, RangeReplaces> Replaces() const;

private:
    Renames(const std::shared_ptr<std::byte[]>& data) noexcept;
};

/**
 * List of directory entries.
 */
class Tree final : public ObjectBase<Tree> {
    friend class Object;

public:
    class Entry {
    public:
        explicit constexpr Entry(const void* p) noexcept;

        /// Identifier of an object.
        HashId Id() const;

        /// Entry's name.
        std::string_view Name() const;

        /// Size of the object.
        uint64_t Size() const;

        /// Entry's type.
        PathType Type() const;

    private:
        const void* const p_;
    };

    struct RangeEntries {
        static Entry Item(const void* p, size_t i);
        static size_t Size(const void* p);
    };

public:
    /// Loads tree object from memory buffer.
    static Tree Load(const std::string_view data);

public:
    /// Range of the entries.
    RepeatedField<Entry, RangeEntries> Entries() const;

    /// Finds entry by name.
    std::optional<Entry> Find(const std::string_view name) const;

    /// Tree has no entries.
    bool Empty() const;

private:
    Tree(const std::shared_ptr<std::byte[]>& data) noexcept;
};

} // namespace Vcs
