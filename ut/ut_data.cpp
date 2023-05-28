#include <vcs/object/data.h>

#include <contrib/gtest/gtest.h>

using namespace Vcs;

TEST(DataHeader, Bytes) {
    EXPECT_EQ(DataHeader().Bytes(), 1u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 0ull).Bytes(), 1u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 1ull).Bytes(), 2u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 115ull).Bytes(), 2u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 1234ull).Bytes(), 3u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 123456ull).Bytes(), 4u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 123456789ull).Bytes(), 5u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 12345678901ull).Bytes(), 6u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 1234567890123ull).Bytes(), 7u);
    EXPECT_THROW(DataHeader::Make(DataType::Blob, 1234567890123456ull).Bytes(), std::invalid_argument);
    EXPECT_THROW(DataHeader::Make(DataType::Blob, 123456789012345678ull).Bytes(), std::invalid_argument);
}

TEST(DataHeader, Size) {
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 0).Size(), 0u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 1).Size(), 1u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 115).Size(), 115u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 12323185).Size(), 12323185u);
}

TEST(DataHeader, Type) {
    EXPECT_EQ(DataHeader::Make(DataType::None, 115).Type(), DataType::None);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 115).Type(), DataType::Blob);
    EXPECT_EQ(DataHeader::Make(DataType::Commit, 115).Type(), DataType::Commit);
    EXPECT_EQ(DataHeader::Make(DataType::Tree, 115).Type(), DataType::Tree);
}

TEST(DataHeader, Constexpr) {
    constexpr auto d = DataHeader::Make(DataType::Blob, 115);

    static_assert(d.Bytes() == 2u);
    static_assert(d.Size() == 115u);
    static_assert(d.Type() == DataType::Blob);
    static_assert(d.Data() != nullptr);

    EXPECT_EQ(d.Bytes(), 2u);
}
