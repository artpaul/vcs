#include <vcs/object/data.h>

#include <contrib/gtest/gtest.h>

using namespace Vcs;

TEST(DataHeader, HeaderBytes) {
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 0).Bytes(), 1u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 1).Bytes(), 2u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 115).Bytes(), 2u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 12323185).Bytes(), 4u);
}

TEST(DataHeader, HeaderSize) {
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 0).Size(), 0u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 1).Size(), 1u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 115).Size(), 115u);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 12323185).Size(), 12323185u);
}

TEST(DataHeader, HeaderType) {
    EXPECT_EQ(DataHeader::Make(DataType::None, 115).Type(), DataType::None);
    EXPECT_EQ(DataHeader::Make(DataType::Blob, 115).Type(), DataType::Blob);
    EXPECT_EQ(DataHeader::Make(DataType::Commit, 115).Type(), DataType::Commit);
    EXPECT_EQ(DataHeader::Make(DataType::Tree, 115).Type(), DataType::Tree);
    EXPECT_EQ(DataHeader::Make(DataType::BlobRef, 115).Type(), DataType::BlobRef);
}

TEST(DataHeader, Constexpr) {
    constexpr auto d = DataHeader::Make(DataType::Blob, 115);

    static_assert(d.Bytes() == 2u);
    static_assert(d.Size() == 115u);
    static_assert(d.Type() == DataType::Blob);

    EXPECT_EQ(d.Bytes(), 2u);
}
