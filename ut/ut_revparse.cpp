#include <ut/lib/graph.h>
#include <vcs/changes/revwalk.h>
#include <vcs/changes/stage.h>
#include <vcs/common/revparse.h>
#include <vcs/store/memory.h>

#include <contrib/gtest/gtest.h>

#include <unordered_map>

using namespace Vcs;

namespace {

static PathEntry MakeBlob(const std::string_view content, Datastore odb) {
    const auto [id, type] = odb.Put(DataType::Blob, content);

    return PathEntry{
        .id = id,
        .data = type,
        .type = PathType::File,
        .size = content.size(),
    };
}

static HashId MakeLibTree(Datastore odb) {
    StageArea index(odb);

    index.Add("lib/lib/empty", MakeBlob("", odb));
    index.Add("lib/test.h", MakeBlob("int test();", odb));
    index.Add("test", MakeBlob("", odb));

    return index.SaveTree(odb);
}

/// https://git-scm.com/docs/git-rev-parse.html#_specifying_revisions
class RevpasrseTest
    : public ::testing::Test
    , protected ReferenceResolver {
public:
    RevpasrseTest()
        : odb_(Datastore::Make<Store::MemoryCache<Store::NoLock>>())
        , graph_(odb_) {
        tree_id_ = MakeLibTree(odb_);
    }

    void SetUp() override {
        labels_ = {
            {'A', 10}, {'B', 8}, {'C', 9}, {'D', 5}, {'E', 6},
            {'F', 7},  {'G', 1}, {'H', 2}, {'I', 3}, {'J', 4},
        };

        graph_.MakeCommit(labels_['G']);
        graph_.MakeCommit(labels_['H']);
        graph_.MakeCommit(labels_['I']);
        graph_.MakeCommit(labels_['J']);
        graph_.MakeCommit(labels_['E']);

        graph_.MakeCommit(labels_['D'], {labels_['G'], labels_['H']});
        graph_.MakeCommit(labels_['F'], {labels_['I'], labels_['J']});
        graph_.MakeCommit(labels_['B'], {labels_['D'], labels_['E'], labels_['F']}, tree_id_);
        graph_.MakeCommit(labels_['C'], {labels_['F']}, tree_id_);
        graph_.MakeCommit(labels_['A'], {labels_['B'], labels_['C']}, tree_id_);
    }

protected:
    /** Gets nth ancestor of the commit. */
    std::optional<HashId> DoGetNthAncestor(const HashId& id, uint64_t count) const final {
        std::optional<HashId> result;

        if (count == 0) {
            return std::make_optional(id);
        }

        RevisionGraph::Walker(RevisionGraph(odb_))
            .Push(id)
            .SimplifyFirstParent(true)
            .Walk([&, first = true](const RevisionGraph::Revision& r) mutable {
                // Walking starts from the given id. Do not count it.
                if (first) {
                    first = false;
                    return WalkAction::Continue;
                }
                if (--count == 0) {
                    result = r.Id();
                    return WalkAction::Stop;
                } else {
                    return WalkAction::Continue;
                }
            });

        return result;
    }

    /** Gets nth parent of a commit. */
    std::optional<HashId> DoGetNthParent(const HashId& id, const uint64_t n) const final {
        if (n == 0) {
            return id;
        }
        if (const auto c = odb_.LoadCommit(id); c.Parents().size() >= n) {
            return c.Parents()[n - 1];
        } else {
            return {};
        }
    }

    /** Lookups object by name. */
    std::optional<HashId> DoLookup(const std::string_view name) const final {
        if (name.empty()) {
            return {};
        }
        if (HashId::IsHex(name)) {
            return HashId::FromHex(name);
        }
        if (name == "HEAD") {
            return graph_.GetHashId(labels_.find('A')->second);
        }
        if (auto li = labels_.find(name[0]); li != labels_.end()) {
            return graph_.GetHashId(li->second);
        } else {
            return {};
        }
    }

protected:
    Datastore odb_;
    UT::Graph graph_;
    HashId tree_id_;
    std::unordered_map<char, UT::KeyId> labels_;
};

TEST_F(RevpasrseTest, Head) {
    ASSERT_TRUE(Resolve("@"));
    ASSERT_TRUE(Resolve("@~"));
    ASSERT_TRUE(Resolve("HEAD"));
    ASSERT_TRUE(Resolve("HEAD~"));
    // @ alone is a shortcut for HEAD.
    EXPECT_EQ(Resolve("@"), Resolve("HEAD"));
    EXPECT_EQ(Resolve("@~"), Resolve("HEAD~"));
}

TEST_F(RevpasrseTest, Invalid) {
    // No third parent.
    EXPECT_FALSE(Resolve("A^3"));
    // No parents at all.
    EXPECT_FALSE(Resolve("G~1"));
    // Invalid pathspec.
    EXPECT_FALSE(Resolve("A~1x"));
}

TEST_F(RevpasrseTest, Single) {
    const std::vector<std::vector<std::string>> cases = {
        {"A", "A^0", "A~0"},
        {"B", "A^", "A^1", "A~1"},
        {"C", "A^2"},
        {"D", "A^^", "A^1^1", "A~2"},
        {"E", "B^2", "A^^2"},
        {"F", "B^3", "A^^3"},
        {"G", "A^^^", "A^1^1^1", "A~3"},
        {"H", "D^2", "B^^2", "A^^^2", "A~2^2"},
        {"I", "F^", "B^3^", "A^^3^"},
        {"J", "F^2", "B^3^2", "A^^3^2"},
    };

    for (const auto& expressions : cases) {
        // Ensure all expressions are resolving to a valid value.
        for (const auto& e : expressions) {
            ASSERT_TRUE(Resolve(e));
        }
        // Compare result of expressions.
        for (size_t i = 1; i < expressions.size(); ++i) {
            EXPECT_EQ(Resolve(expressions[i - 1]), Resolve(expressions[i]));
        }
        // Compare with actual hash.
        EXPECT_EQ(Resolve(expressions[0]), graph_.GetHashId(labels_[expressions[0][0]]));
    }
}

} // namespace
