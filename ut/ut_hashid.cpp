#include <contrib/gtest/gtest.h>
#include <vcs/object/hashid.h>

#include <sstream>

using namespace Vcs;

static constexpr std::string_view STR_TEST = "test";
static constexpr std::string_view STR_HEX_ID = "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3";

static HashId MakeHashId(const std::string_view data) {
    return HashId::Builder().Append(data).Build();
}

TEST(HashId, Builder) {
    EXPECT_EQ(MakeHashId("").ToHex(), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    EXPECT_EQ(MakeHashId(STR_TEST).ToHex(), STR_HEX_ID);

    // Build from parts.
    EXPECT_EQ(
        HashId::Builder().Append("test").Build(), HashId::Builder().Append("te").Append("st").Build()
    );
}

TEST(HashId, Empty) {
    EXPECT_EQ(HashId().ToHex(), "0000000000000000000000000000000000000000");
    EXPECT_FALSE(bool(HashId()));
}

TEST(HashId, FromHex) {
    EXPECT_EQ(MakeHashId(STR_TEST), HashId::FromHex(STR_HEX_ID));
    EXPECT_EQ(HashId::FromHex(STR_HEX_ID).ToHex(), STR_HEX_ID);
    EXPECT_NE(MakeHashId(STR_TEST), HashId::FromHex("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
}

TEST(HashId, IsHex) {
    EXPECT_TRUE(HashId::IsHex("a94a8fe5ccb19ba61c4c0873d391e987982fbbd3"));
    EXPECT_TRUE(HashId::IsHex("a94A8fe5ccb19ba61c4c0873D391e987982fbbd3"));

    EXPECT_FALSE(HashId::IsHex("a94A8fe5ccb19ba61c4c0873D391e987982fbbdz"));
    EXPECT_FALSE(HashId::IsHex("x94a8fe5ccb19ba61c4c0873d391e987982fbbd3"));
    EXPECT_FALSE(HashId::IsHex("a94a8fe5ccb19ba61c"));
    EXPECT_FALSE(HashId::IsHex(""));
}

TEST(HashId, FmtOutput) {
    EXPECT_EQ(fmt::format("{}", HashId::FromHex(STR_HEX_ID)), STR_HEX_ID);
}

TEST(HashId, StreamOutput) {
    std::stringstream ss;

    ss << HashId::FromHex(STR_HEX_ID);

    EXPECT_EQ(ss.str(), STR_HEX_ID);
}
