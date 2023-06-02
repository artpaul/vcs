#include <cmd/local/config.h>

#include <contrib/fmt/fmt/format.h>
#include <contrib/gtest/gtest.h>

using namespace Vcs;

TEST(Config, Get) {
    std::map<ConfigLocation, std::unique_ptr<Config::Backend>> configs;
    // User configuration.
    configs[ConfigLocation::User] =
        Config::MakeBackend(nlohmann::json::object({{"user", {{"email", "John@mail.com"}}}}));
    // Default configuration.
    configs[ConfigLocation::Default] =
        Config::MakeBackend(nlohmann::json::object({{"user", {{"name", "John"}}}}));

    Config config(std::move(configs));

    ASSERT_TRUE(config.Get("user.name"));
    ASSERT_TRUE(config.Get("user.email"));

    EXPECT_EQ(config.Get("user.name")->get<std::string>(), "John");
    EXPECT_EQ(config.Get("user.email")->get<std::string>(), "John@mail.com");

    EXPECT_FALSE(config.Get("user.name", ConfigLocation::User));
}
