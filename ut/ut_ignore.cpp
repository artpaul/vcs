#include <vcs/common/ignore.h>

#include <contrib/gtest/gtest.h>

using namespace Vcs;

TEST(IgnoreRules, MatchSimple) {
    static constexpr std::string_view kIgnores = R"__(
# Builds
/out/

# CMake
CMakeSettings.json

# IDEs
.vs
.vscode

# Junk
/junk/

build/tmp

)__";

    IgnoreRules rules(kIgnores);

    ASSERT_EQ(rules.Count(), 6);
    EXPECT_EQ(rules[0].pattern, "out");
    EXPECT_EQ(rules[0].flags, Rule::kFullPath | Rule::kDirectory);

    EXPECT_TRUE(*rules.Match("a/b/c/CMakeSettings.json", false));
    EXPECT_TRUE(*rules.Match("build/tmp", false));
    EXPECT_TRUE(*rules.Match("build/tmp", true));
    EXPECT_TRUE(*rules.Match("out", true));

    EXPECT_FALSE(rules.Match("out", false));
    EXPECT_FALSE(rules.Match("tmp", false));
}

TEST(IgnoreRules, MatchWildcard) {
    static constexpr std::string_view kIgnores = R"__(
# Django #
*.pyc
foo/*
)__";

    IgnoreRules rules(kIgnores);

    ASSERT_EQ(rules.Count(), 2);
    // Check rules.
    EXPECT_EQ(rules[0].pattern, "*.pyc");
    EXPECT_EQ(rules[0].flags, Rule::kHasWildcard);

    EXPECT_EQ(rules[1].pattern, "foo/*");
    EXPECT_EQ(rules[1].flags, Rule::kHasWildcard | Rule::kFullPath);

    EXPECT_TRUE(*rules.Match("a/b/c.pyc", false));
    EXPECT_TRUE(*rules.Match("a/b/c.pyc", true));
    EXPECT_TRUE(*rules.Match("foo/test.json", false));
    EXPECT_TRUE(*rules.Match("foo/bar", true));

    // EXPECT_FALSE(rules.Match("foo/bar/hello.c", false));
}

TEST(IgnoreRules, Negate) {
    static constexpr std::string_view kIgnores = R"__(
/*
!/foo/
/foo/*
!/foo/bar/
)__";

    IgnoreRules rules(kIgnores);

    ASSERT_EQ(rules.Count(), 4);
    // Check rules.
    EXPECT_EQ(rules[0].flags, Rule::kFullPath | Rule::kHasWildcard);
    EXPECT_EQ(rules[3].flags, Rule::kFullPath | Rule::kDirectory | Rule::kNegative);

    EXPECT_TRUE(*rules.Match("foo/bar", false));
    EXPECT_TRUE(*rules.Match("foo", false));
    EXPECT_TRUE(*rules.Match("bar", false));
    EXPECT_TRUE(*rules.Match("foo/abc", false));

    EXPECT_FALSE(*rules.Match("foo", true));
    EXPECT_FALSE(*rules.Match("foo/bar", true));
}

TEST(IgnoreRules, StarStar) {
    static constexpr std::string_view kIgnores = R"__(
a/**/b
)__";

    IgnoreRules rules(kIgnores);

    ASSERT_EQ(rules.Count(), 1);

    EXPECT_TRUE(*rules.Match("a/b", true));
    EXPECT_TRUE(*rules.Match("a/x/b", true));
}
