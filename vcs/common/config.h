#pragma once

#include <contrib/json/nlohmann.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace Vcs {

enum class ConfigLocation {
    /// Application specific configuration.
    Application = 0,
    /// Workspace specific configuration.
    Workspace,
    /// Repository specific configuration.
    Repository,
    /// User specific configuration.
    User,
    /// System specific configuration.
    System,
    /// Default configuration.
    Default,
};

class Config {
public:
    class Backend {
    public:
        virtual ~Backend() = default;

        virtual std::optional<nlohmann::json> Get(const std::string_view key) const = 0;
    };

    /** Makes a file backend. */
    static std::unique_ptr<Backend> MakeBackend(const std::filesystem::path& path);

    /** Makes a json backend. */
    static std::unique_ptr<Backend> MakeBackend(nlohmann::json config);

    /** Makes an in-memory backend. */
    static std::unique_ptr<Backend> MakeBackend(std::map<std::string, nlohmann::json, std::less<>> config);

public:
    Config();

    Config(std::map<ConfigLocation, std::unique_ptr<Backend>> locations);

    /** Returns value by key. */
    std::optional<nlohmann::json> Get(const std::string_view key) const;

    /** Returns value by key from the specific location. */
    std::optional<nlohmann::json> Get(const std::string_view key, const ConfigLocation location) const;

public:
    void Reset(const ConfigLocation location, std::unique_ptr<Backend> backend);

private:
    std::optional<nlohmann::json> GetImpl(
        const std::string_view key, const std::optional<ConfigLocation> location
    ) const;

private:
    std::vector<std::unique_ptr<Backend>> backends_;
};

} // namespace Vcs
