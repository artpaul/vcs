#include <vcs/changes/changelist.h>
#include <vcs/changes/path.h>
#include <vcs/changes/stage.h>
#include <vcs/store/memory.h>

#include <contrib/gtest/gtest.h>

using namespace Vcs;

static PathEntry MakeBlob(const std::string_view content, Datastore odb) {
    const auto [id, type] = odb.Put(DataType::Blob, content);

    return PathEntry{
        .id = id,
        .data = type,
        .type = PathType::File,
        .size = content.size(),
    };
}

static HashId MakeTreeLib(Datastore odb) {
    StageArea index(odb);

    index.Add("bin/main.cpp", MakeBlob("extern main;", odb));
    index.Add("lib/test.h", MakeBlob("int test();", odb));

    return index.SaveTree(odb);
}

static HashId MakeTreeUtil(Datastore odb) {
    StageArea index(odb);

    index.Add("lib/test.h", MakeBlob("int test( return 0; );", odb));
    index.Add("util/string.h", MakeBlob("#include <cstddef>\nclass string { };", odb));

    return index.SaveTree(odb);
}

TEST(Change, CompareEntries) {
    EXPECT_FALSE(CompareEntries(PathEntry{}, PathEntry{}));
    EXPECT_FALSE(CompareEntries(PathEntry{.size = 1}, PathEntry{.size = 1}));
    EXPECT_FALSE(CompareEntries(PathEntry{.type = PathType::File}, PathEntry{.type = PathType::File}));

    EXPECT_TRUE(CompareEntries(PathEntry{.size = 1}, PathEntry{.size = 2}));
    EXPECT_TRUE(CompareEntries(PathEntry{.type = PathType::File}, PathEntry{.type = PathType::Directory}));

    EXPECT_TRUE(CompareEntries(PathEntry{.type = PathType::File}, PathEntry{.type = PathType::Executible})
                    .attributes);
    EXPECT_TRUE(CompareEntries(PathEntry{.size = 1}, PathEntry{.size = 2}).content);
    EXPECT_TRUE(
        CompareEntries(PathEntry{.type = PathType::File}, PathEntry{.type = PathType::Directory}).type
    );
}

TEST(ChangelistBuilder, Changes) {
    auto mem = Datastore::Make<Store::MemoryCache>();

    HashId tree_lib = MakeTreeLib(mem);
    HashId tree_util = MakeTreeUtil(mem);

    std::vector<Change> changes;
    ChangelistBuilder(mem, changes).Changes(tree_lib, tree_util);

    ASSERT_EQ(changes.size(), 5u);

    EXPECT_EQ(changes[0].action, PathAction::Delete);
    EXPECT_EQ(changes[0].path, "bin/main.cpp");

    EXPECT_EQ(changes[1].action, PathAction::Delete);
    EXPECT_EQ(changes[1].path, "bin");

    EXPECT_EQ(changes[2].action, PathAction::Change);
    EXPECT_EQ(changes[2].path, "lib/test.h");

    EXPECT_EQ(changes[3].action, PathAction::Add);
    EXPECT_EQ(changes[3].type, PathType::Directory);
    EXPECT_EQ(changes[3].path, "util");

    EXPECT_EQ(changes[4].action, PathAction::Add);
    EXPECT_EQ(changes[4].path, "util/string.h");
}

TEST(ChangelistBuilder, ChangesNoDirectoryExpansion) {
    auto mem = Datastore::Make<Store::MemoryCache>();

    HashId tree_lib = MakeTreeLib(mem);
    HashId tree_util = MakeTreeUtil(mem);

    std::vector<Change> changes;
    ChangelistBuilder(mem, changes)
        .SetExpandAdded(false)
        .SetExpandDeleted(false)
        .Changes(tree_lib, tree_util);

    ASSERT_EQ(changes.size(), 3u);

    EXPECT_EQ(changes[0].action, PathAction::Delete);
    EXPECT_EQ(changes[0].type, PathType::Directory);
    EXPECT_EQ(changes[0].path, "bin");

    EXPECT_EQ(changes[1].action, PathAction::Change);
    EXPECT_EQ(changes[1].path, "lib/test.h");

    EXPECT_EQ(changes[2].action, PathAction::Add);
    EXPECT_EQ(changes[2].type, PathType::Directory);
    EXPECT_EQ(changes[2].path, "util");
}

TEST(ChangelistBuilder, ExpandDeleted) {
    auto mem = Datastore::Make<Store::MemoryCache>();

    StageArea index(mem);

    index.Add("a/b/c", MakeBlob("b", mem));
    index.Add("a/b/d", MakeBlob("b", mem));
    index.Add("a/a", MakeBlob("b", mem));

    std::vector<Change> changes;
    ChangelistBuilder(mem, changes).Changes(index.SaveTree(mem), HashId());

    ASSERT_EQ(changes.size(), 5u);

    EXPECT_EQ(changes[0].path, "a/a");
    EXPECT_EQ(changes[0].type, PathType::File);

    EXPECT_EQ(changes[1].path, "a/b/c");
    EXPECT_EQ(changes[1].type, PathType::File);

    EXPECT_EQ(changes[2].path, "a/b/d");
    EXPECT_EQ(changes[2].type, PathType::File);

    EXPECT_EQ(changes[3].path, "a/b");
    EXPECT_EQ(changes[3].type, PathType::Directory);

    EXPECT_EQ(changes[4].path, "a");
    EXPECT_EQ(changes[4].type, PathType::Directory);
}

TEST(ChangelistBuilder, IncludeFilter) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    auto filter = PathFilter({"lib/test.h", "util"});

    const HashId tree_lib = MakeTreeLib(mem);
    const HashId tree_util = MakeTreeUtil(mem);

    std::vector<Change> changes;
    ChangelistBuilder(mem, changes).SetInclude(filter).Changes(tree_lib, tree_util);

    ASSERT_EQ(changes.size(), 3u);

    EXPECT_EQ(changes[0].action, PathAction::Change);
    EXPECT_EQ(changes[0].path, "lib/test.h");
    EXPECT_EQ(changes[0].type, PathType::File);

    EXPECT_EQ(changes[1].action, PathAction::Add);
    EXPECT_EQ(changes[1].path, "util");
    EXPECT_EQ(changes[1].type, PathType::Directory);

    EXPECT_EQ(changes[2].action, PathAction::Add);
    EXPECT_EQ(changes[2].path, "util/string.h");
    EXPECT_EQ(changes[2].type, PathType::File);
}
