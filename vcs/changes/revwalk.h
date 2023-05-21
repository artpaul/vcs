#pragma once

#include <vcs/object/hashid.h>
#include <vcs/object/object.h>
#include <vcs/object/store.h>

#include <functional>
#include <unordered_set>

namespace Vcs {

enum class WalkAction {
    /// Move to next commit.
    Continue,
    /// Hide all ancestors of a commit.
    Hide,
    /// Stop iteration.
    Stop,
};

class RevisionGraph {
public:
    class Page;
    class Revision;
    class Walker;

    struct Format {
        struct Record {
            /// Commit's generation number.
            uint64_t generation;
            /// Commit's timestamp.
            uint64_t timestamp;
            /// Number of parents.
            uint64_t parents : 4;
            /// Reserved.
            uint64_t reserved : 60;
        };

        static_assert(sizeof(Record) == 24);
        /// Ensure there is no need to explicitly call destructor for the object.
        static_assert(std::is_trivially_destructible_v<Record>);
    };

    class Chunk {
    public:
        virtual ~Chunk() = default;

        /** Returns a commit record for the specific id. */
        virtual std::optional<Revision> GetRevision(const HashId& id) const = 0;

        /** Returns hash value associated with a record by the index. */
        virtual HashId GetHash(const Format::Record* rec, const uint32_t idx) const = 0;
    };

    class Revision {
    public:
        struct RangeParents {
            static HashId Item(const Revision* p, const size_t i) {
                return p->chunk_->GetHash(p->record_, 2 + i);
            }

            static size_t Size(const Revision* p) noexcept {
                return p->record_->parents;
            }
        };

    public:
        Revision(const Format::Record* record, const Chunk* chunk) noexcept
            : chunk_(chunk)
            , record_(record) {
        }

        /** Generation number. */
        uint64_t Generation() const noexcept {
            return record_->generation;
        }

        /** Identifier of the commit. */
        HashId Id() const {
            return chunk_->GetHash(record_, 0);
        }

        /** Range of parent commits. */
        RepeatedField<HashId, RangeParents, Revision> Parents() const noexcept {
            return RepeatedField<HashId, RangeParents, Revision>(this);
        }

        /** Creation timestamp in UTC. */
        uint64_t Timestamp() const noexcept {
            return record_->timestamp;
        }

        /** Identifier of the root tree. */
        HashId Tree() const {
            return chunk_->GetHash(record_, 1);
        }

    private:
        const Chunk* chunk_;
        const Format::Record* record_;
    };

    /**
     * Walks through a commit-graph according to the given criteria.
     */
    class Walker {
    public:
        explicit Walker(const RevisionGraph& graph) noexcept;

        /** Lower bound of a generation range (include). */
        Walker& GenerationFrom(const uint64_t generation) noexcept;

        /** Upper bound of a generation range (include). */
        Walker& GenerationTo(const uint64_t generation) noexcept;

        /** Add a new root for the traversal. */
        Walker& Push(const HashId& commit_id);

        /** Add new roots for the traversal. */
        Walker& Push(const std::unordered_set<HashId>& commit_ids);

        /** Mark a commit (and its ancestors) uninteresting for the output. */
        Walker& Hide(const HashId& commit_id);

        /** Mark commits (and its ancestors) uninteresting for the output. */
        Walker& Hide(const std::unordered_set<HashId>& commit_ids);

        /** Simplify the history by first-parent. */
        Walker& SimplifyFirstParent(const bool value) noexcept;

        /** Enumerate commits. */
        void Walk(const std::function<WalkAction(const Revision&)>& cb) const;

    private:
        void WalkGeneric(const std::function<WalkAction(const Revision&)>& cb) const;

        void WalkLinear(const std::function<WalkAction(const Revision&)>& cb) const;

    private:
        const RevisionGraph& graph_;
        std::unordered_set<HashId> roots_;
        std::unordered_set<HashId> hidden_;
        uint64_t generation_from_;
        uint64_t generation_to_;
        bool first_parent_;
    };

public:
    explicit RevisionGraph(const Datastore& odb);

    ~RevisionGraph();

    /** Returns commit's info. */
    Revision GetRevision(const HashId& id) const;

private:
    Datastore odb_;
    std::unique_ptr<Page> page_;
};

} // namespace Vcs
