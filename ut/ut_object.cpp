#include <vcs/api/fbs/commit.fb.h>
#include <vcs/api/fbs/index.fb.h>
#include <vcs/api/fbs/tree.fb.h>
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
}

TEST(ObjectCommit, Parents) {
    CommitBuilder commit;
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
    commit.tree = HashId::Make(DataType::Tree, TreeBuilder().Serialize());
    commit.generation = 1;
    commit.committer.when = 1;
    commit.message = "test";

    auto data = commit.Serialize();
    auto item = Fbs::GetCommit(data.data());

    EXPECT_EQ(FromFlatBuffers(item->tree()).ToHex(), commit.tree.ToHex());
    EXPECT_EQ(item->generation(), 1u);
    EXPECT_EQ(item->committer()->when(), 1u);
    EXPECT_EQ(item->message()->str(), "test");
}

TEST(ObjectIndex, Parts) {
    const auto id = HashId::Make(DataType::Blob, "");
    const auto data = IndexBuilder(id, DataType::Blob).Append(HashId(), 5).Append(HashId(), 7).Serialize();
    const auto item = Fbs::GetIndex(data.data());
    const auto index = Object::Load(DataType::Index, data).AsIndex();

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
    EXPECT_EQ(tree.Find("main.cpp")->Name(), "main.cpp");
    EXPECT_EQ(tree.Find("test.txt")->Name(), "test.txt");
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
