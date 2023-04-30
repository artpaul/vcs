#include <vcs/changes/changelist.h>
#include <vcs/changes/stage.h>
#include <vcs/store/memory.h>

#include <contrib/gtest/gtest.h>

using namespace Vcs;

static PathEntry MakeBlob(const std::string_view content, Datastore odb) {
    return PathEntry{
        .id = odb.Put(DataType::Blob, content),
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

TEST(ChangelistBuilderTest, Changes) {
    auto mem = Datastore::Make<Store::MemoryCache>();

    HashId tree_lib = MakeTreeLib(mem);
    HashId tree_util = MakeTreeUtil(mem);

    std::vector<Change> changes;
    ChangelistBuilder(mem, changes).Changes(tree_lib, tree_util);

    ASSERT_EQ(changes.size(), 5u);

    ASSERT_EQ(changes[0].action, PathAction::Delete);
    ASSERT_EQ(changes[0].path, "bin");

    ASSERT_EQ(changes[1].action, PathAction::Delete);
    ASSERT_EQ(changes[1].path, "bin/main.cpp");

    ASSERT_EQ(changes[2].action, PathAction::Change);
    ASSERT_EQ(changes[2].path, "lib/test.h");

    ASSERT_EQ(changes[3].action, PathAction::Add);
    ASSERT_EQ(changes[3].type, PathType::Directory);
    ASSERT_EQ(changes[3].path, "util");

    ASSERT_EQ(changes[4].action, PathAction::Add);
    ASSERT_EQ(changes[4].path, "util/string.h");
}

TEST(ChangelistBuilderTest, ChangesNoDirectoryExpansion) {
    auto mem = Datastore::Make<Store::MemoryCache>();

    HashId tree_lib = MakeTreeLib(mem);
    HashId tree_util = MakeTreeUtil(mem);

    std::vector<Change> changes;
    ChangelistBuilder(mem, changes).SetExpandDirectories(false).Changes(tree_lib, tree_util);

    ASSERT_EQ(changes.size(), 3u);

    ASSERT_EQ(changes[0].action, PathAction::Delete);
    ASSERT_EQ(changes[0].type, PathType::Directory);
    ASSERT_EQ(changes[0].path, "bin");

    ASSERT_EQ(changes[1].action, PathAction::Change);
    ASSERT_EQ(changes[1].path, "lib/test.h");

    ASSERT_EQ(changes[2].action, PathAction::Add);
    ASSERT_EQ(changes[2].type, PathType::Directory);
    ASSERT_EQ(changes[2].path, "util");
}
