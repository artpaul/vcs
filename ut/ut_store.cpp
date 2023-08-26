#include <vcs/object/serialize.h>
#include <vcs/store/disk.h>
#include <vcs/store/memory.h>

#include <contrib/gtest/gtest.h>

#include <iostream>

using namespace Vcs;

namespace {

static constexpr std::string_view text("one line of text");

class StringInput {
public:
    explicit StringInput(const std::string& data)
        : data_(data)
        , pos_(0) {
    }

    size_t Read(void* buf, size_t len) {
        if (pos_ < data_.size()) {
            len = std::min(len, data_.size() - pos_);
            std::memcpy(buf, data_.data(), len);
            pos_ += len;
            return len;
        }
        return 0;
    }

private:
    const std::string& data_;
    size_t pos_{0};
};

} // namespace

TEST(Datastore, DataTag) {
    EXPECT_EQ(Store::Disk::DataTag(100, true, false).Length(), 100u);
    EXPECT_EQ(Store::Disk::DataTag(134217727u, true, true).Length(), 134217727u);

    EXPECT_TRUE(Store::Disk::DataTag(1, true, false).IsCompressed());
    EXPECT_TRUE(Store::Disk::DataTag(1, false, true).IsDelta());
}

TEST(Datastore, Fanout) {
    std::vector<uint32_t> fanout(256, 0);
    std::vector<uint8_t> oids = {0x02, 0x02, 0x02, 0x04, 0x59, 0x59, 0xaa, 0xfb, 0xfd};

    // Build fanout table.
    for (auto it = oids.begin(), end = oids.end(); it != end;) {
        const auto next =
            std::upper_bound(it, end, *it, [](const auto& val, const auto& item) { return val < item; });

        fanout[*it] = next - oids.begin();

        it = next;
    }
    // Fill fanout gaps.
    for (size_t i = 1, end = fanout.size() - 1; i != end; ++i) {
        if (fanout[i] == 0) {
            fanout[i] += fanout[i - 1];
        }
    }
    // Last cell always holds the total number of oids in the index.
    fanout[fanout.size() - 1] = oids.size();

    EXPECT_EQ(fanout[0x00], 0u);
    EXPECT_EQ(fanout[0x01], 0u);
    EXPECT_EQ(fanout[0x02], 3u);
    EXPECT_EQ(fanout[0x03], 3u);
    EXPECT_EQ(fanout[0x04], 4u);
    EXPECT_EQ(fanout[0x59], 6u);
    EXPECT_EQ(fanout[0x70], 6u);
    EXPECT_EQ(fanout[0xaa], 7u);
    EXPECT_EQ(fanout[0xfb], 8u);
    EXPECT_EQ(fanout[0xfd], 9u);
    EXPECT_EQ(fanout[0xff], oids.size());
}

TEST(Datastore, Cache) {
    auto mem1 = Store::MemoryCache<Store::NoLock>::Make(1024);
    auto mem2 = Store::MemoryCache<Store::NoLock>::Make(1024);
    auto mem3 = Store::MemoryCache<Store::NoLock>::Make(1024);

    // Setup datastore.
    auto odb = Datastore().Chain(mem1).Chain(mem2).Cache(mem3);
    // Put blob directly into the first backend.
    auto [id, _] = Datastore().Chain(mem1).Put(DataType::Blob, text);

    ASSERT_EQ(odb.LoadBlob(id), text);

    // Blob was put directly into the first backend.
    EXPECT_TRUE(Datastore().Chain(mem1).IsExists(id));
    // Blob was cache in the third backend.
    EXPECT_TRUE(Datastore().Chain(mem3).IsExists(id));
    // Blob should not exist in the second backend.
    EXPECT_FALSE(Datastore().Chain(mem2).IsExists(id));
}

TEST(Datastore, GetChunkSize) {
    EXPECT_EQ(Datastore(1024).GetChunkSize(), 1024u);
    EXPECT_EQ(Datastore(4096).GetChunkSize(), 4096u);

    EXPECT_EQ(Datastore(1024).Chain<Store::MemoryCache<Store::NoLock>>(4096).GetChunkSize(), 1024u);
}

TEST(Datastore, InputStream) {
    auto mem = Store::MemoryCache<Store::NoLock>::Make(1024);
    auto odb = Datastore(256).Chain(mem);
    auto data = std::string();
    auto stream = StringInput(data);

    for (size_t i = 0; i < 20; ++i) {
        data.append(text);
    }

    auto [id, _] = odb.Put(DataHeader::Make(DataType::Blob, data.size()), InputStream(stream));

    ASSERT_TRUE(id);
    // Check validity of metadata.
    EXPECT_TRUE(odb.GetType(id).IsIndex());
    EXPECT_EQ(odb.GetType(id, true), DataType::Blob);
    EXPECT_EQ(odb.GetMeta(id, true).Size(), data.size());
    // Check equality of content.
    EXPECT_EQ(std::string_view(odb.LoadBlob(id)), data);
}

TEST(MemoryCache, Capacity) {
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
        auto mem = Datastore::Make<Store::MemoryCache<Store::NoLock>>(1024);
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
        auto mem = Datastore::Make<Store::MemoryCache<Store::NoLock>>(1024);
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

TEST(MemoryCache, BlobChunked) {
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
        auto mem = Datastore(1u << 20).Chain<Store::MemoryCache<Store::NoLock>>(4u << 20);
        const auto [id, _] = mem.Put(DataType::Blob, content);

        ASSERT_TRUE(mem.IsExists(id));
        ASSERT_EQ(mem.GetType(id), DataType::Blob);
        EXPECT_EQ(std::string_view(mem.LoadBlob(id)), content);
    }

    {
        auto mem = Datastore(1024).Chain<Store::MemoryCache<Store::NoLock>>(1u << 20);
        const auto [id, type] = mem.Put(DataType::Blob, content);

        ASSERT_TRUE(mem.IsExists(id));
        ASSERT_TRUE(type.IsIndex());
        ASSERT_TRUE(mem.GetType(id).IsIndex());
        EXPECT_EQ(mem.LoadIndex(id).Type(), DataType::Blob);
        EXPECT_EQ(mem.LoadIndex(id).Size(), content.size());
        EXPECT_EQ(std::string_view(mem.LoadBlob(id)), content);
    }
}

TEST(MemoryCache, TreeChunked) {
    TreeBuilder builder;

    for (size_t i = 0; i < 100; ++i) {
        builder.Append(
            "entry" + std::to_string(i),
            PathEntry{.id = HashId::Make(DataType::Blob, "abcde"), .type = PathType::File, .size = 5}
        );
    }

    auto mem = Datastore(1024).Chain<Store::MemoryCache<Store::NoLock>>(1u << 20);
    const auto [id, _] = mem.Put(DataType::Tree, builder.Serialize());

    ASSERT_TRUE(mem.IsExists(id));
    ASSERT_TRUE(mem.GetType(id).IsIndex());
    EXPECT_EQ(mem.LoadIndex(id).Type(), DataType::Tree);
    EXPECT_EQ(mem.LoadTree(id).Entries().size(), 100u);
}
