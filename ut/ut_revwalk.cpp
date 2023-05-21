#include <ut/lib/graph.h>
#include <vcs/changes/revwalk.h>
#include <vcs/store/memory.h>

#include <contrib/gtest/gtest.h>

#include <unordered_set>

using namespace Vcs;

namespace {

class Forbidden : public Datastore::Backend {
public:
    Forbidden(const std::unordered_set<HashId>& ids)
        : ids_(ids) {
    }

protected:
    DataHeader GetMeta(const HashId& id) const override {
        CheckId(id);
        return DataHeader();
    }

    bool Exists(const HashId& id) const override {
        CheckId(id);
        return false;
    }

    Object Load(const HashId& id, const DataType) const override {
        CheckId(id);
        return Object();
    }

    void Put(const HashId&, DataType, std::string_view) override {
    }

private:
    void CheckId(const HashId& id) const {
        if (ids_.find(id) != ids_.end()) {
            throw std::runtime_error(fmt::format("hash id is forbidden {}", id.ToHex()));
        }
    }

private:
    std::unordered_set<HashId> ids_;
};

} // namespace

TEST(RevisionGraphWalker, CommitsInBranch) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    UT::Graph graph(mem);

    graph.MakeCommit(1);
    graph.MakeCommit(2, {1});
    graph.MakeCommit(3, {2});
    graph.MakeCommit(4, {3});
    graph.MakeCommit(5, {2});

    auto filter = mem.Chain<Forbidden>(std::unordered_set<HashId>{graph.GetHashId(1)});
    RevisionGraph revs(filter);

    std::vector<HashId> commits;

    RevisionGraph::Walker(revs)
        .Push(graph.GetHashId(4))
        .Hide(graph.GetHashId(5))
        .Walk([&](const RevisionGraph::Revision& r) {
            commits.push_back(r.Id());
            return WalkAction::Continue;
        });

    ASSERT_EQ(commits.size(), 2u);
    EXPECT_EQ(commits[0], graph.GetHashId(4));
    EXPECT_EQ(commits[1], graph.GetHashId(3));
    // Check the filter is in the effect.
    EXPECT_ANY_THROW(revs.GetRevision(graph.GetHashId(1)));
}

TEST(RevisionGraphWalker, HideAll) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    UT::Graph graph(mem);

    graph.MakeCommit(1);
    graph.MakeCommit(2, {1});
    graph.MakeCommit(3, {1});

    size_t counter = 0;

    RevisionGraph revs(mem);
    RevisionGraph::Walker(revs)
        .Push(graph.GetHashId(2))
        .Push(graph.GetHashId(3))
        .Walk([&](const RevisionGraph::Revision&) {
            ++counter;
            return WalkAction::Hide;
        });

    EXPECT_EQ(counter, 2u);
}

TEST(RevisionGraphWalker, WalkFirstParent) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    UT::Graph graph(mem);

    graph.MakeCommit(1);
    graph.MakeCommit(2, {1});
    graph.MakeCommit(3, {2});
    graph.MakeCommit(4, {3});
    graph.MakeCommit(5, {4});

    RevisionGraph revs(mem);

    auto run_tests = [&](bool first_parent) {
        {
            size_t counter = 0;

            RevisionGraph::Walker(revs)
                .Push(graph.GetHashId(5))
                .SimplifyFirstParent(first_parent)
                .Walk([&](const RevisionGraph::Revision&) {
                    ++counter;
                    return WalkAction::Continue;
                });

            EXPECT_EQ(counter, 5u);
        }

        {
            size_t counter = 0;

            RevisionGraph::Walker(revs)
                .Push(graph.GetHashId(5))
                .Hide(graph.GetHashId(2))
                .SimplifyFirstParent(first_parent)
                .Walk([&](const RevisionGraph::Revision&) {
                    ++counter;
                    return WalkAction::Continue;
                });

            EXPECT_EQ(counter, 3u);
        }

        {
            size_t counter = 0;

            RevisionGraph::Walker(revs)
                .GenerationFrom(2)
                .GenerationTo(4)
                .Push(graph.GetHashId(5))
                .SimplifyFirstParent(first_parent)
                .Walk([&](const RevisionGraph::Revision&) {
                    ++counter;
                    return WalkAction::Continue;
                });

            EXPECT_EQ(counter, 3u);
        }
    };

    // Optimized version for linear walk.
    run_tests(true);
    // Generic version.
    run_tests(false);
}
