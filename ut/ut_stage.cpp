#include <vcs/changes/stage.h>
#include <vcs/object/path.h>
#include <vcs/store/memory.h>

#include <contrib/gtest/gtest.h>

using namespace Vcs;

static PathEntry MakeBlob(const std::string_view content, Datastore* odb) {
    const auto [id, type] = odb->Put(DataType::Blob, content);

    return PathEntry{
        .id = id,
        .data = type,
        .type = PathType::File,
        .size = content.size(),
    };
}

static HashId MakeLibTree(Datastore* odb) {
    StageArea index(*odb);

    index.Add("lib/lib/empty", MakeBlob("", odb));
    index.Add("lib/test.h", MakeBlob("int test();", odb));
    index.Add("test", MakeBlob("", odb));

    return index.SaveTree(*odb);
}

TEST(StageArea, Add) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    StageArea index(mem);

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

TEST(StageArea, Copy) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    StageArea index(mem, MakeLibTree(&mem));

    ASSERT_TRUE(index.Copy("lib/test.h", "util/test.h"));
    ASSERT_TRUE(index.GetEntry("util"));
    ASSERT_TRUE(index.GetEntry("util/test.h"));

    EXPECT_EQ(index.GetEntry("lib/test.h")->id, index.GetEntry("util/test.h")->id);

    // Insert new entry.
    ASSERT_TRUE(index.Add("lib/data", MakeBlob("x0x0x0x0x", &mem)));
    // Source entry should be taken from the base tree.
    EXPECT_FALSE(index.Copy("lib/data", "util/data"));

    // Update an entry.
    ASSERT_TRUE(index.Add("lib/test.h", MakeBlob("#pragma once;\nint test();", &mem)));
    // Copy the entry.
    ASSERT_TRUE(index.Copy("lib/test.h", "test"));
    // Source entry should be taken from the base tree.
    EXPECT_NE(index.GetEntry("lib/test.h")->id, index.GetEntry("test")->id);
    // Copy will overwrite the current entry.
    EXPECT_NE(index.GetEntry("test")->id, MakeBlob("", &mem).id);
}

TEST(StageArea, GetRoot) {
    auto mem = Datastore::Make<Store::MemoryCache>();

    // The root entry always exists.
    EXPECT_TRUE(StageArea(mem).GetEntry(""));
    EXPECT_EQ(StageArea(mem).ListTree("").size(), 0u);
    // The root entry is always type of tree.
    EXPECT_TRUE(IsDirectory(StageArea(mem).GetEntry("")->type));

    EXPECT_TRUE(StageArea(mem, MakeLibTree(&mem)).GetEntry(""));
}

TEST(StageArea, GetEntry) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    StageArea index(mem, MakeLibTree(&mem));

    // Make the index mutable.
    ASSERT_TRUE(index.Remove("test"));
    EXPECT_FALSE(index.GetEntry("test"));
    EXPECT_TRUE(index.GetEntry("test", true));

    // Entry should be loaded for the base tree.
    EXPECT_TRUE(index.GetEntry("lib/test.h"));
}

TEST(StageArea, ListTree) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    StageArea index(mem, MakeLibTree(&mem));

    EXPECT_EQ(index.ListTree("").size(), 2u);
    EXPECT_EQ(index.ListTree("lib").size(), 2u);

    // Make the index mutable.
    ASSERT_TRUE(index.Remove("test"));
    EXPECT_EQ(index.ListTree("lib").size(), 2u);
    // Entries should be loaded for the base tree.
    EXPECT_EQ(index.ListTree("lib/lib").size(), 1u);
}

TEST(StageIndexCase, Move) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    StageArea index(mem, MakeLibTree(&mem));

    ASSERT_TRUE(index.Copy("lib/test.h", "util/test.h"));
    ASSERT_TRUE(index.Remove("lib/test.h"));

    EXPECT_TRUE(index.GetEntry("util"));
    EXPECT_TRUE(index.GetEntry("util/test.h"));
    EXPECT_FALSE(index.GetEntry("lib/test.h"));
}

TEST(StageArea, Remove) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    StageArea index(mem);

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
    auto mem = Datastore::Make<Store::MemoryCache>();
    StageArea index(mem, MakeLibTree(&mem));

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
    auto mem = Datastore::Make<Store::MemoryCache>();
    StageArea index(mem);

    ASSERT_TRUE(index.Add("lib/lib/empty", MakeBlob("", &mem)));
    ASSERT_TRUE(index.Add("lib/test.h", MakeBlob("int test();", &mem)));
    ASSERT_TRUE(index.Add("test", MakeBlob("", &mem)));

    ASSERT_TRUE(index.SaveTree(mem));
}

TEST(StageArea, SaveTreeEmpty) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    StageArea index(mem);

    // Root tree is always valid.
    ASSERT_TRUE(index.SaveTree(mem));

    ASSERT_TRUE(index.Add("lib/test.h", MakeBlob("int test();", &mem)));
    ASSERT_TRUE(index.Add("empty", PathEntry{.type = PathType::Directory}));

    // Empty directory was saved.
    ASSERT_TRUE(StageArea(mem, index.SaveTree(mem)).GetEntry("empty"));
    // Empty directory was not saved.
    ASSERT_FALSE(StageArea(mem, index.SaveTree(mem, false)).GetEntry("empty"));
}

TEST(StageArea, SaveTreeUpdate) {
    auto mem = Datastore::Make<Store::MemoryCache>();
    HashId tree_id;

    {
        StageArea index(mem, MakeLibTree(&mem));

        ASSERT_TRUE(index.Remove("lib/test.h"));
        ASSERT_TRUE(index.Add("test", MakeBlob("", &mem)));
        ASSERT_TRUE(index.Add("lib/test/empty", MakeBlob("", &mem)));
        ASSERT_TRUE(index.Add("empty", PathEntry{.type = PathType::Directory}));

        tree_id = index.SaveTree(mem);
    }

    ASSERT_TRUE(tree_id);

    StageArea index(mem, tree_id);

    ASSERT_TRUE(index.GetEntry("empty"));
    EXPECT_EQ(index.GetEntry("empty")->data, DataType::Tree);
    EXPECT_EQ(index.GetEntry("test")->data, DataType::Blob);
    EXPECT_TRUE(IsDirectory(index.GetEntry("empty")->type));
    EXPECT_TRUE(index.GetEntry("lib/test/empty"));
    EXPECT_FALSE(index.GetEntry("lib/test.h"));
}

TEST(StageArea, SaveTreeChunked) {
    auto mem = Datastore(1024).Chain<Store::MemoryCache>(1u << 20);
    HashId tree_id;

    {
        StageArea index(mem);
        const auto blob = MakeBlob("int test();", &mem);

        for (size_t i = 0; i < 50; ++i) {
            ASSERT_TRUE(index.Add("name" + std::to_string(i), blob));
            ASSERT_TRUE(index.Add("dir/name" + std::to_string(i), blob));
        }

        tree_id = index.SaveTree(mem);
    }

    ASSERT_TRUE(tree_id);

    StageArea index(mem, tree_id);

    EXPECT_EQ(mem.GetType(tree_id), DataType::Index);
    EXPECT_EQ(mem.GetType(tree_id, true), DataType::Tree);

    ASSERT_EQ(mem.LoadTree(tree_id).Entries().size(), 51u);

    ASSERT_TRUE(index.GetEntry("dir"));
    ASSERT_EQ(index.GetEntry("dir")->data, DataType::Index);
}
