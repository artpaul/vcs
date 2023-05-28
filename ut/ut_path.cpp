#include <vcs/changes/path.h>

#include <contrib/gtest/gtest.h>

using namespace Vcs;

TEST(PathFilter, Match) {
    PathFilter filter({"a/b", "aa/bb", "a/b/c", "c"});

    EXPECT_TRUE(filter.Match(""));
    EXPECT_TRUE(filter.Match("a/b/c"));
    EXPECT_TRUE(filter.Match("a/b/d"));
    EXPECT_TRUE(filter.Match("c/b/d"));
    EXPECT_TRUE(filter.Match("aa/bb"));

    EXPECT_FALSE(filter.Empty());
    EXPECT_FALSE(filter.Match("a"));
    EXPECT_FALSE(filter.Match("a/bb"));
    EXPECT_FALSE(filter.Match("aa/bbb"));
    EXPECT_FALSE(filter.Match("d"));
    EXPECT_FALSE(filter.Match("aa/dd"));
}

TEST(PathFilter, IsParent) {
    PathFilter filter({"a/b", "aa/bb", "a/b/c", "c"});

    EXPECT_TRUE(filter.IsParent(""));
    EXPECT_TRUE(filter.IsParent("a"));
    EXPECT_TRUE(filter.IsParent("a/b/d"));

    EXPECT_FALSE(filter.IsParent("a/bb/d"));
}
