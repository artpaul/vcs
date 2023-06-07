#include <util/arena.h>
#include <util/split.h>
#include <util/varint.h>

#include <contrib/gtest/gtest.h>

TEST(Utils, Arena) {
    Arena arena(1024);

    // Allocate some bytes.
    ASSERT_TRUE(arena.Allocate(5));
    // Allocate some bytes at aligned address.
    ASSERT_EQ(reinterpret_cast<uintptr_t>(arena.Allocate(10, 16)) & 15, 0u);
    // Force creation of a new chunk.
    ASSERT_TRUE(arena.Allocate(2048));
}

TEST(Utils, SplitPath) {
    const auto check_abc = []<typename T>(const std::vector<T>& parts) {
        ASSERT_EQ(parts.size(), 3u);
        EXPECT_EQ(parts[0], "a");
        EXPECT_EQ(parts[1], "b");
        EXPECT_EQ(parts[2], "c");
    };

    check_abc(SplitPath("a/b/c"));
    check_abc(SplitPath("/a/b/c"));
    check_abc(SplitPath("/a//b/c/"));
    check_abc(SplitString<std::string>("/a//b/c/", '/'));
    check_abc(SplitString<std::string>("a.b.c", '.'));
}

TEST(Utils, Varlen) {
    uint8_t buf[10];
    uint32_t val = 0;

    ASSERT_EQ(EncodeVarint(100, buf, sizeof(buf)), 1u);
    ASSERT_TRUE(DecodeVarint(buf, sizeof(buf), val));
    EXPECT_EQ(val, 100u);
}
