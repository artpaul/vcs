#include "revwalk.h"

#include <util/arena.h>

#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>
#include <cassert>
#include <limits>
#include <queue>

namespace Vcs {

class RevisionGraph::Page : public Chunk {
public:
    Page()
        : arena_(4096) {
    }

    Revision Push(const HashId& id, const Commit& commit) {
        absl::WriterMutexLock lock(&mutex_);
        // Allocate memory for a revision record.
        auto buf = arena_.Allocate(
            sizeof(Format::Record) + (2 + commit.Parents().size()) * sizeof(HashId), alignof(Format::Record)
        );
        // Create revision record.
        auto node = new (buf) Format::Record();
        // Hashes are stored right after the revision record in the same memory block.
        const auto append_hash = [&node](const HashId& id, const uint8_t offset) {
            std::byte* q = std::bit_cast<std::byte*>(node + 1) + (offset * sizeof(HashId));
            // Copy id.
            std::memcpy(q, id.Data(), id.Size());
        };
        // Copy commit info.
        node->generation = commit.Generation();
        node->timestamp = commit.Timestamp();
        node->parents = commit.Parents().size();
        node->reserved = 0;
        // Copy commit's id.
        append_hash(id, 0);
        // Copy root tree.
        append_hash(commit.Tree(), 1);
        // Copy parents.
        for (size_t i = 0, end = commit.Parents().size(); i != end; ++i) {
            append_hash(commit.Parents()[i], 2 + i);
        }
        // Store revision.
        return commits_.emplace(id, Revision(node, this)).first->second;
    }

    std::optional<Revision> GetRevision(const HashId& id) const override {
        absl::ReaderMutexLock lock(&mutex_);
        if (auto ci = commits_.find(id); ci != commits_.end()) {
            return ci->second;
        }
        return std::nullopt;
    }

    HashId GetHash(const Format::Record* rec, const uint32_t idx) const override {
        if (idx >= 2u + rec->parents) {
            return HashId();
        }
        return HashId::FromBytes(
            std::bit_cast<const std::byte*>(rec + 1) + (idx * sizeof(HashId)), sizeof(HashId)
        );
    }

private:
    mutable absl::Mutex mutex_;
    /// Memory storage.
    Arena arena_;
    /// Index of the stored commits.
    absl::flat_hash_map<HashId, Revision> commits_;
};

RevisionGraph::Walker::Walker(const RevisionGraph& graph) noexcept
    : graph_(graph)
    , generation_from_(0)
    , generation_to_(std::numeric_limits<uint64_t>::max())
    , first_parent_(false) {
}

RevisionGraph::Walker& RevisionGraph::Walker::GenerationFrom(const uint64_t generation) noexcept {
    generation_from_ = generation;
    return *this;
}

RevisionGraph::Walker& RevisionGraph::Walker::GenerationTo(const uint64_t generation) noexcept {
    generation_to_ = generation;
    return *this;
}

RevisionGraph::Walker& RevisionGraph::Walker::Push(const HashId& commit_id) {
    roots_.insert(commit_id);
    return *this;
}

RevisionGraph::Walker& RevisionGraph::Walker::Push(const std::unordered_set<HashId>& commit_ids) {
    roots_.insert(commit_ids.begin(), commit_ids.end());
    return *this;
}

RevisionGraph::Walker& RevisionGraph::Walker::Hide(const HashId& commit_id) {
    hidden_.insert(commit_id);
    return *this;
}

RevisionGraph::Walker& RevisionGraph::Walker::Hide(const std::unordered_set<HashId>& commit_ids) {
    hidden_.insert(commit_ids.begin(), commit_ids.end());
    return *this;
}

RevisionGraph::Walker& RevisionGraph::Walker::SimplifyFirstParent(const bool value) noexcept {
    first_parent_ = value;
    return *this;
}

void RevisionGraph::Walker::WalkLinear(const std::function<WalkAction(const Revision&)>& cb) const {
    HashId id = *roots_.begin();

    while (true) {
        // Load current commit.
        const auto c = graph_.GetRevision(id);
        // Stop walking if we're out of generation range.
        if (c.Generation() < generation_from_) {
            break;
        }
        // Report current commit if we're already in generation range.
        if (c.Generation() <= generation_to_) {
            switch (cb(c)) {
                case WalkAction::Continue:
                    break;
                case WalkAction::Hide:
                case WalkAction::Stop:
                    return;
            }
        }
        // Move to the parent commit.
        if (c.Parents()) {
            id = c.Parents()[0];
        } else {
            break;
        }
    }
}

void RevisionGraph::Walker::WalkGeneric(const std::function<WalkAction(const Revision&)>& cb) const {
    struct GenerationLess {
        bool operator()(const Revision& a, Revision& b) const noexcept {
            return a.Generation() < b.Generation();
        }
    };

    std::unordered_set<HashId> hidden(hidden_);
    std::unordered_set<HashId> marked;
    std::priority_queue<Revision, std::vector<Revision>, GenerationLess> queue;
    size_t hidden_in_queue = hidden.size();

    // Initialize queue with hidden.
    for (const auto& id : hidden_) {
        marked.insert(id);
        queue.push(graph_.GetRevision(id));
    }
    // Initialize queue with roots, but only with whose which
    // hasn't been added yet.
    for (const auto& id : roots_) {
        if (marked.insert(id).second) {
            queue.push(graph_.GetRevision(id));
        }
    }

    while (!queue.empty() && hidden_in_queue < queue.size()) {
        const auto commit = queue.top();
        const auto id = commit.Id();
        queue.pop();
        // Forget about the commit if it out of the generation range.
        if (commit.Generation() < generation_from_) {
            continue;
        }
        bool hide = false;
        // Check whether the current commit needs to be reported
        // or hidden.
        if (hidden.find(id) != hidden.end()) {
            assert(hidden_in_queue);

            hidden_in_queue--;
            hide = true;
        } else if (commit.Generation() <= generation_to_) {
            switch (cb(commit)) {
                case WalkAction::Continue:
                    break;
                case WalkAction::Hide:
                    hide = true;
                    break;
                case WalkAction::Stop:
                    return;
            }
        }
        // Equeue parent commits.
        for (const auto& p : commit.Parents()) {
            // Hide parent commit.
            if (hide) {
                if (hidden.insert(p).second) {
                    hidden_in_queue++;
                }
            }
            // Even parent of hidden commit needs to be put
            // into the queue, because we need to hide all commits
            // reachable from hidden one.
            if (marked.insert(p).second) {
                queue.push(graph_.GetRevision(p));
            }

            if (first_parent_) {
                break;
            }
        }
    }
}

void RevisionGraph::Walker::Walk(const std::function<WalkAction(const Revision&)>& cb) const {
    if (!cb || roots_.empty()) {
        return;
    }

    if (first_parent_ && roots_.size() == 1 && hidden_.empty()) {
        WalkLinear(cb);
    } else {
        WalkGeneric(cb);
    }
}

RevisionGraph::RevisionGraph(const Datastore& odb)
    : odb_(odb)
    , page_(std::make_unique<Page>()) {
}

RevisionGraph::~RevisionGraph() = default;

RevisionGraph::Revision RevisionGraph::GetRevision(const HashId& id) const {
    if (const auto rev = page_->GetRevision(id)) {
        return *rev;
    }

    return page_->Push(id, odb_.LoadCommit(id));
}

} // namespace Vcs
