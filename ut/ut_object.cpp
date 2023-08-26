#include <vcs/api/fbs/commit.fb.h>
#include <vcs/api/fbs/index.fb.h>
#include <vcs/api/fbs/tree.fb.h>
#include <vcs/object/change.h>
#include <vcs/object/object.h>
#include <vcs/object/serialize.h>

#include <contrib/gtest/gtest.h>

using namespace Vcs;

static constexpr std::string_view kFileContent = "int main() { return 0; }";

static auto FromFlatBuffers(const flatbuffers::Vector<uint8_t>* id) {
    return HashId::FromBytes(id->data(), id->size());
}

static auto MakeBlobEntry(const std::string_view content) {
    return PathEntry{
        .id = HashId::Make(DataType::Blob, content),
        .type = PathType::File,
        .size = content.size(),
    };
}

TEST(Object, Empty) {
    Object obj;

    ASSERT_FALSE(obj);
    EXPECT_EQ(obj.Data(), nullptr);
    EXPECT_EQ(obj.Size(), 0ul);
    EXPECT_EQ(obj.Type(), DataType::None);
    EXPECT_THROW(obj.AsBlob(), std::runtime_error);
}

TEST(Object, Load) {
    const auto obj = Object::Load(DataType::Blob, kFileContent);

    ASSERT_TRUE(obj);
    // As opaque object.
    EXPECT_EQ(std::string_view((const char*)obj.Data(), obj.Size()), kFileContent);
    // As blob object.
    EXPECT_EQ(obj.AsBlob(), kFileContent);

    // Other type casts should throw.
    EXPECT_ANY_THROW(obj.AsCommit());
    EXPECT_ANY_THROW(obj.AsIndex());
    EXPECT_ANY_THROW(obj.AsRenames());
    EXPECT_ANY_THROW(obj.AsTree());
}

TEST(ObjectCommit, Parents) {
    CommitBuilder commit;
    commit.author.name = "John";
    commit.tree = HashId::Make(DataType::Tree, TreeBuilder().Serialize());
    commit.generation = 1;
    commit.parents.push_back(HashId());
    commit.parents.push_back(HashId());

    auto c = Object::Load(DataType::Commit, commit.Serialize()).AsCommit();

    ASSERT_EQ(c.Parents().size(), 2u);
    EXPECT_EQ(c.Parents()[0], HashId());
    EXPECT_EQ(c.Parents()[1], HashId());
}

TEST(ObjectCommit, Serialize) {
    CommitBuilder commit;
    commit.author.name = "John";
    commit.author.when = 1;
    commit.tree = HashId::Make(DataType::Tree, TreeBuilder().Serialize());
    commit.generation = 1;
    commit.message = "test";

    auto data = commit.Serialize();
    auto item = Fbs::GetCommit(data.data());

    EXPECT_EQ(FromFlatBuffers(item->tree()).ToHex(), commit.tree.ToHex());
    EXPECT_EQ(item->generation(), 1u);
    EXPECT_EQ(item->author()->when(), 1u);
    EXPECT_EQ(item->message()->str(), "test");
}

TEST(ObjectIndex, Parts) {
    const auto id = HashId::Make(DataType::Blob, "");
    const auto data = IndexBuilder(id, DataType::Blob).Append(HashId(), 5).Append(HashId(), 7).Serialize();
    const auto item = Fbs::GetIndex(data.data());
    const auto index = Object::Load(DataType::BlobIndex, data).AsIndex();

    /// Serialized form.
    EXPECT_EQ(FromFlatBuffers(item->id()), id);
    ASSERT_EQ(item->parts()->size(), 2u);
    EXPECT_EQ(item->parts()->Get(0)->size(), 5u);
    EXPECT_EQ(item->parts()->Get(1)->size(), 7u);
    /// Wrapper.
    EXPECT_EQ(index.Id(), id);
    EXPECT_EQ(index.Type(), DataType::Blob);
    ASSERT_EQ(index.Parts().size(), 2u);
    EXPECT_EQ(index.Parts()[0].Size(), 5u);
    EXPECT_EQ(index.Parts()[1].Size(), 7u);
}

TEST(ObjectRenames, Build) {
    RenamesBuilder builder;
    // Add copies.
    builder.copies.push_back(RenamesBuilder::CopyInfo{HashId::Make(DataType::Commit, ""), "a/b", "a/a/b"});
    builder.copies.push_back(RenamesBuilder::CopyInfo{HashId(), "a/b", "a/a/a"});
    builder.copies.push_back(RenamesBuilder::CopyInfo{HashId(), "a/b/c", "a/b/c"});
    // Add some replaces.
    builder.replaces.push_back("a/b/c");
    builder.replaces.push_back("a/b/b");
    builder.replaces.push_back("x/b/c/d");

    auto renames = Renames::Load(builder.Serialize());

    ASSERT_EQ(renames.Commits().size(), 2u);
    EXPECT_EQ(renames.Commits()[0], HashId());
    EXPECT_EQ(renames.Commits()[1], HashId::Make(DataType::Commit, ""));

    ASSERT_EQ(renames.Copies().size(), 3u);
    EXPECT_EQ(renames.Copies()[0].Path(), "a/a/a");
    EXPECT_EQ(renames.Copies()[0].CommitId(), HashId());
    EXPECT_EQ(renames.Copies()[0].Source(), "a/b");
    EXPECT_EQ(renames.Copies()[1].Path(), "a/a/b");
    EXPECT_EQ(renames.Copies()[1].CommitId(), HashId::Make(DataType::Commit, ""));
    EXPECT_EQ(renames.Copies()[1].Source(), "a/b");

    ASSERT_EQ(renames.Replaces().size(), 3u);
    EXPECT_EQ(renames.Replaces()[0], "a/b/b");
    EXPECT_EQ(renames.Replaces()[1], "a/b/c");
    EXPECT_EQ(renames.Replaces()[2], "x/b/c/d");
}

TEST(ObjectTree, EmptyTree) {
    const auto& tree = Tree::Load(TreeBuilder().Serialize());

    EXPECT_EQ(tree.Entries().size(), 0u);
    EXPECT_TRUE(tree.Empty());
    EXPECT_FALSE(tree.Find("unknown"));
}

TEST(ObjectTree, Find) {
    const auto& data = TreeBuilder()
                           .Append("test.txt", MakeBlobEntry("text file"))
                           .Append("main.cpp", MakeBlobEntry(kFileContent))
                           .Serialize();
    const auto& tree = Tree::Load(data);

    EXPECT_FALSE(tree.Find("unknown"));

    ASSERT_EQ(tree.Entries().size(), 2u);
    // Ensure sorted.
    ASSERT_EQ(tree.Entries()[0].Name(), "main.cpp");
    ASSERT_EQ(tree.Entries()[1].Name(), "test.txt");
    ASSERT_EQ(tree.Entries()[1].Type(), PathType::File);
    // Test find.
    EXPECT_EQ(tree.Find("main.cpp").Name(), "main.cpp");
    EXPECT_EQ(tree.Find("test.txt").Name(), "test.txt");

    EXPECT_FALSE(CompareEntries(static_cast<PathEntry>(tree.Find("test.txt")), MakeBlobEntry("text file")));
}

TEST(ObjectTree, Serialize) {
    PathEntry e;
    e.size = kFileContent.size();
    e.id = HashId::Make(DataType::Blob, kFileContent);
    e.type = PathType::Symlink;

    auto data = TreeBuilder().Append("main.cpp", e).Serialize();
    auto item = Fbs::GetTree(data.data());

    ASSERT_EQ(item->entries()->size(), 1u);

    auto entry = item->entries()->Get(0);
    EXPECT_EQ(FromFlatBuffers(entry->id()), e.id);
    EXPECT_EQ(entry->name()->c_str(), std::string_view("main.cpp"));
    EXPECT_EQ(entry->size(), e.size);
    EXPECT_EQ(static_cast<PathType>(entry->type()), e.type);
}

TEST(RangeIterator, RandomAccessIterator) {
    struct RangeVector {
        static int Item(const void* p, size_t i) {
            return static_cast<const std::vector<int>*>(p)->at(i);
        }

        static size_t Size(const void* p) {
            return static_cast<const std::vector<int>*>(p)->size();
        }
    };

    const std::vector<int> nums = {1, 3, 5, 7, 9, 11};

#define TEST_SEMANTIC(expr) \
   { \
      RepeatedField<int, RangeVector>::iterator a(&nums, 1); \
      RepeatedField<int, RangeVector>::iterator b(&nums, 4); \
      [[maybe_unused]] const auto n = std::distance(a, b); \
      expr; \
   }

    static_assert(std::is_same_v<
                  std::iterator_traits<RepeatedField<int, RangeVector>::iterator>::iterator_category,
                  std::random_access_iterator_tag>);

    TEST_SEMANTIC(EXPECT_EQ((a += n), b));
    TEST_SEMANTIC(auto c = std::addressof(a); EXPECT_EQ(std::addressof(a += n), c));
    TEST_SEMANTIC(auto c = std::addressof(b); EXPECT_EQ(std::addressof(b -= n), c));
    TEST_SEMANTIC(auto c = a + n; EXPECT_EQ(c, (a += n)));
    TEST_SEMANTIC(EXPECT_EQ((a + n), (n + a)));
    TEST_SEMANTIC(EXPECT_EQ((a + 0), a));
    TEST_SEMANTIC(EXPECT_EQ((a + (n - 1)), (--b)));
    TEST_SEMANTIC(EXPECT_EQ((b += -n), a));
    TEST_SEMANTIC(EXPECT_EQ((b -= n), a));
    TEST_SEMANTIC(auto c = b - n; EXPECT_EQ(c, (b -= n)));
    TEST_SEMANTIC(EXPECT_EQ(a[n], *b));
    TEST_SEMANTIC(EXPECT_TRUE(a <= b));

#undef TEST_SEMANTIC
}

TEST(RangeIterator, Throw) {
    struct Throw {
        static HashId Item(const void*, size_t) noexcept(false) {
            return HashId();
        }

        static size_t Size(const void*) {
            return 0;
        }
    };

    static_assert(!RepeatedField<HashId, Throw>::is_noexcept_item);
    static_assert(!RepeatedField<HashId, Throw>::is_noexcept_size);
    static_assert(!noexcept(std::declval<RepeatedField<HashId, Throw>>().empty()));
    static_assert(!noexcept(std::declval<RepeatedField<HashId, Throw>>()[0]));
}

TEST(RangeIterator, NoThrow) {
    struct S {
        constexpr size_t Size() const noexcept {
            return 3;
        }
    };

    struct NoThrow {
        static constexpr HashId Item(const S*, size_t) noexcept(true) {
            return HashId();
        }

        static constexpr size_t Size(const S* p) noexcept {
            return p->Size();
        }
    };

    S s;

    static_assert(RepeatedField<HashId, NoThrow, S>(&s).size() == 3);

    static_assert(RepeatedField<HashId, NoThrow, S>::is_noexcept_item);
    static_assert(RepeatedField<HashId, NoThrow, S>::is_noexcept_size);
    static_assert(noexcept(std::declval<RepeatedField<HashId, NoThrow, S>>()[0]));
}
