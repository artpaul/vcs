#pragma once

#include <vcs/object/hashid.h>
#include <vcs/object/object.h>

#include <optional>
#include <string_view>

namespace Vcs {

class ReferenceResolver {
public:
    virtual ~ReferenceResolver() = default;

    /**
     * Parses an reference specification.
     *
     * @param ref refspec to parse.
     * @return ** std::optional<HashId>
     */
    std::optional<HashId> Resolve(const std::string_view ref) const;

protected:
    /**
     * Gets nth ancestor of a commit.
     *
     * @param id id of the commit object.
     */
    virtual std::optional<HashId> DoGetNthAncestor(const HashId& id, const uint64_t n) const = 0;

    /**
     * Gets nth parent of a commit.
     *
     * @param id id of the commit object.
     * @param n position of a parent (1-based).
     */
    virtual std::optional<HashId> DoGetNthParent(const HashId& id, const uint64_t n) const = 0;

    /** Lookups object by name. */
    virtual std::optional<HashId> DoLookup(const std::string_view name) const = 0;
};

} // namespace  Vcs
