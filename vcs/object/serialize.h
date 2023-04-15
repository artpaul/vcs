#pragma once

#include "object.h"

#include <functional>
#include <string>
#include <vector>

namespace Vcs {

/**
 * Serialize commit object into FlatBuffers format.
 */
struct CommitBuilder {
    struct Attribute {
        /// Name of the attribute.
        std::string name;
        /// Value of the attribute.
        std::string value;

        constexpr bool Empty() const noexcept {
            return name.empty();
        }
    };

    struct Signature {
        // Unique user identifier
        std::string id;
        /// Human readable name.
        std::string name;
        /// Creation timestamp in UTC.
        uint64_t when = 0;

        constexpr bool Empty() const noexcept {
            return id.empty() && name.empty() && when == 0;
        }
    };

    /// User-defined attributes.
    std::vector<Attribute> attributes;
    /// Author of the commit.
    Signature author;
    /// Commiter.
    Signature committer;
    /// Description of the commit.
    std::string message;

    /// Root tree.
    HashId tree{};
    /// Generation number.
    uint64_t generation{0};
    /// List of parent revisions.
    std::vector<HashId> parents;
    /// Id of an object with history adjustments for the root tree.
    HashId renames{};

public:
    std::string Serialize();
};

/**
 * Serialize index object into FlatBuffers format.
 */
class IndexBuilder {
public:
    constexpr IndexBuilder() noexcept = default;

    IndexBuilder(const HashId& id, DataType type) noexcept;

    IndexBuilder& Append(const HashId& id, const uint32_t size);

    IndexBuilder& SetId(const HashId& id) noexcept;

    IndexBuilder& SetType(const DataType type) noexcept;

    void EnumerateParts(const std::function<void(const HashId&, const uint32_t)>& cb) const;

    std::string Serialize() const;

private:
    struct Part {
        /// Identifier of a blob object.
        HashId id;
        /// Size of the blob object.
        uint32_t size;
    };

    /// Identfier of an original object.
    HashId id_{};
    /// Type of the object.
    DataType type_{};
    /// Object's parts.
    std::vector<Part> parts_;
};

/**
 * Serialize tree object into FlatBuffers format.
 */
class TreeBuilder {
public:
    TreeBuilder& Append(const std::string name, const PathEntry& entry);

    TreeBuilder& Append(const Tree::Entry& e);

    /// Returns whether any entries were appended.
    bool Empty() const noexcept;

    /// Serializes appended entries into FlatBuffers format.
    std::string Serialize();

private:
    size_t CalculateBufferSize() const noexcept;

private:
    std::vector<std::pair<std::string, PathEntry>> entries_;
};

} // namespace Vcs
