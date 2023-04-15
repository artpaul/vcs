#include <vcs/store/memory.h>

#include <contrib/gtest/gtest.h>

#include <iostream>

using namespace Vcs;

static constexpr std::string_view text("one line of text");

TEST(MemoryStore, Capacity) {
    std::vector<std::pair<HashId, std::string>> blobs;
    // Make blobs.
    {
        std::string content(text);

        for (size_t i = 0, end = 40; i < end; ++i) {
            blobs.emplace_back(HashId::Make(DataType::Blob, content), content);

            if (i + 1 != end) {
                content = "\n" + content;
            }
        }
    }

    {
        MemoryStore mem(1024);
        // Put an object.
        mem.Put(DataType::Blob, blobs[0].second);
        // Check that the object exists in the storage.
        ASSERT_TRUE(mem.IsExists(blobs[0].first));
        // Put more objects.
        for (size_t i = 1; i < 35; ++i) {
            mem.Put(DataType::Blob, blobs[i].second);
        }
        // Check existence.
        ASSERT_FALSE(mem.IsExists(blobs[0].first));
    }

    {
        MemoryStore mem(1024);
        // Put and object.
        mem.Put(DataType::Blob, blobs[0].second);
        // Check that the object exists in the storage.
        ASSERT_TRUE(mem.IsExists(blobs[0].first));
        // Put more objects.
        for (size_t i = 1; i < 15; ++i) {
            mem.Put(DataType::Blob, blobs[i].second);
        }
        // Touch the object.
        mem.LoadBlob(blobs[0].first);
        // Put more objects.
        for (size_t i = 15; i < 35; ++i) {
            mem.Put(DataType::Blob, blobs[i].second);
        }
        // Check existence.
        ASSERT_TRUE(mem.IsExists(blobs[0].first));
    }
}

TEST(MemoryStore, ChunkSize) {
    std::string content(text);
    // Make a big string.
    for (size_t i = 0, end = 10; i < end; ++i) {
        if (i + 1 != end) {
            content += "\n" + content;
        }
    }
    // Ensure the string is big enough.
    ASSERT_EQ(content.size(), 8703u);

    {
        MemoryStore mem(4 << 20, 1 << 20);
        const auto id = mem.Put(DataType::Blob, content);

        ASSERT_TRUE(mem.IsExists(id));
        ASSERT_EQ(mem.GetType(id), DataType::Blob);
        EXPECT_EQ(std::string_view(mem.LoadBlob(id)), content);
    }

    {
        MemoryStore mem(1 << 20, 512);
        const auto id = mem.Put(DataType::Blob, content);

        ASSERT_TRUE(mem.IsExists(id));
        ASSERT_EQ(mem.GetType(id), DataType::Index);
        EXPECT_EQ(mem.LoadIndex(id).Type(), DataType::Blob);
        EXPECT_EQ(mem.LoadIndex(id).Size(), content.size());
        EXPECT_EQ(std::string_view(mem.LoadBlob(id)), content);
    }
}
