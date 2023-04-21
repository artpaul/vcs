#include <vcs/stage.h>
#include <vcs/store/memory.h>

#include <contrib/gtest/gtest.h>

using namespace Vcs;

static PathEntry MakeBlob(const std::string_view content, Datastore* odb) {
    return PathEntry{
        .id = odb->Put(DataType::Blob, content),
        .type = PathType::File,
        .size = content.size(),
    };
}

static HashId MakeLibTree(Datastore* odb) {
    StageArea index(odb);

    index.Add("lib/lib/empty", MakeBlob("", odb));
    index.Add("lib/test.h", MakeBlob("int test();", odb));
    index.Add("test", MakeBlob("", odb));

    return index.SaveTree(odb);
}

TEST(StageArea, Add) {
    MemoryStore mem;
    StageArea index(&mem);

    // Updating of root entry is prohibited.
    ASSERT_FALSE(index.Add("", PathEntry()));

    ASSERT_TRUE(index.Add("lib/test.h", MakeBlob("int test();", &mem)));
    ASSERT_TRUE(index.Add("lib/test.cpp", MakeBlob("#include \"test.h\"", &mem)));
    // Parent directories should be automatically created on insertion.
    EXPECT_TRUE(index.GetEntry("lib"));
    EXPECT_TRUE(IsDirectory(index.GetEntry("lib")->type));

    EXPECT_EQ(index.ListTree("").size(), 1u);
    EXPECT_EQ(index.ListTree("lib").size(), 2u);
}

TEST(StageArea, GetRoot) {
    MemoryStore mem;

    // The root entry always exists.
    EXPECT_TRUE(StageArea(&mem).GetEntry(""));
    EXPECT_EQ(StageArea(&mem).ListTree("").size(), 0u);
    // The root entry is always type of tree.
    EXPECT_TRUE(IsDirectory(StageArea(&mem).GetEntry("")->type));

    EXPECT_TRUE(StageArea(&mem, MakeLibTree(&mem)).GetEntry(""));
}

TEST(StageArea, GetEntry) {
    MemoryStore mem;
    StageArea index(&mem, MakeLibTree(&mem));

    // Make the index mutable.
    ASSERT_TRUE(index.Remove("test"));
    EXPECT_FALSE(index.GetEntry("test"));
    EXPECT_TRUE(index.GetEntry("test", true));

    // Entry should be loaded for the base tree.
    EXPECT_TRUE(index.GetEntry("lib/test.h"));
}

TEST(StageArea, ListTree) {
    MemoryStore mem;
    StageArea index(&mem, MakeLibTree(&mem));

    EXPECT_EQ(index.ListTree("").size(), 2u);
    EXPECT_EQ(index.ListTree("lib").size(), 2u);

    // Make the index mutable.
    ASSERT_TRUE(index.Remove("test"));
    EXPECT_EQ(index.ListTree("lib").size(), 2u);
    // Entries should be loaded for the base tree.
    EXPECT_EQ(index.ListTree("lib/lib").size(), 1u);
}

TEST(StageArea, Remove) {
    MemoryStore mem;
    StageArea index(&mem);

    // Removing of root entry is prohibited.
    ASSERT_FALSE(index.Remove(""));

    ASSERT_TRUE(index.Add("lib/test.h", MakeBlob("int test();", &mem)));
    ASSERT_TRUE(index.Remove("lib/test.h"));
    ASSERT_TRUE(index.Remove("lib"));

    EXPECT_FALSE(index.GetEntry("lib"));
    // Entry was ephemeral and was completely removed from the tree.
    EXPECT_FALSE(index.GetEntry("lib", true));
    EXPECT_EQ(index.ListTree("lib", true).size(), 0u);
}

TEST(StageArea, RestoreRemoved) {
    MemoryStore mem;
    StageArea index(&mem, MakeLibTree(&mem));

    ASSERT_TRUE(index.Remove("lib"));
    EXPECT_FALSE(index.GetEntry("lib"));
    EXPECT_FALSE(index.GetEntry("lib/test.h"));
    EXPECT_TRUE(index.GetEntry("lib", true));
    EXPECT_TRUE(index.GetEntry("lib/test.h", true));

    ASSERT_TRUE(index.Add("lib/test.cpp", MakeBlob("int test();", &mem)));
    EXPECT_TRUE(index.GetEntry("lib"));
    EXPECT_TRUE(index.GetEntry("lib/test.cpp"));
    // Entries from the state prior delete should not reappear.
    EXPECT_FALSE(index.GetEntry("lib/test.h"));

    ASSERT_TRUE(index.Remove("test"));
    EXPECT_TRUE(index.GetEntry("test", true));
    ASSERT_TRUE(index.Add("test/test.h", MakeBlob("int test();", &mem)));
}

TEST(StageArea, SaveTree) {
    MemoryStore mem;
    StageArea index(&mem);

    ASSERT_TRUE(index.Add("lib/lib/empty", MakeBlob("", &mem)));
    ASSERT_TRUE(index.Add("lib/test.h", MakeBlob("int test();", &mem)));
    ASSERT_TRUE(index.Add("test", MakeBlob("", &mem)));

    ASSERT_TRUE(index.SaveTree(&mem));
}

TEST(StageArea, SaveTreeUpdate) {
    MemoryStore mem;
    HashId tree_id;

    {
        StageArea index(&mem, MakeLibTree(&mem));

        ASSERT_TRUE(index.Remove("lib/test.h"));
        ASSERT_TRUE(index.Add("test", MakeBlob("", &mem)));
        ASSERT_TRUE(index.Add("lib/test/empty", MakeBlob("", &mem)));

        tree_id = index.SaveTree(&mem);
    }

    ASSERT_TRUE(tree_id);

    StageArea index(&mem, tree_id);

    EXPECT_TRUE(index.GetEntry("lib/test/empty"));
    EXPECT_FALSE(index.GetEntry("lib/test.h"));
}

TEST(StageArea, SaveTreeChunked) {
    MemoryStore mem(1 << 20, 1024);
    StageArea index(&mem);
    const auto blob = MakeBlob("int test();", &mem);

    for (size_t i = 0; i < 100; ++i) {
        ASSERT_TRUE(index.Add("name" + std::to_string(i), blob));
    }

    const auto id = index.SaveTree(&mem);

    EXPECT_EQ(mem.GetType(id), DataType::Index);
    EXPECT_EQ(mem.GetType(id, true), DataType::Tree);

    ASSERT_EQ(mem.LoadTree(id).Entries().size(), 100u);
}
