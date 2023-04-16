#include <vcs/util/split.h>

#include <contrib/gtest/gtest.h>

TEST(Utils, SplitPath) {
    const auto check_abc = [](const std::vector<std::string_view>& parts) {
        ASSERT_EQ(parts.size(), 3u);
        EXPECT_EQ(parts[0], "a");
        EXPECT_EQ(parts[1], "b");
        EXPECT_EQ(parts[2], "c");
    };

    check_abc(SplitPath("a/b/c"));
    check_abc(SplitPath("/a/b/c"));
    check_abc(SplitPath("/a//b/c/"));
}
