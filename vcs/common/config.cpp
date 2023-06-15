#include "config.h"

#include <util/file.h>
#include <util/split.h>

#include <contrib/fmt/fmt/format.h>

namespace Vcs {
namespace {

class JsonBackend : public Config::Backend {
public:
    JsonBackend() = default;

    explicit JsonBackend(nlohmann::json config)
        : config_(std::move(config)) {
    }

    std::optional<nlohmann::json> Get(const std::string_view key) const override {
        const auto path =
            nlohmann::json::json_pointer(fmt::format("/{}", fmt::join(SplitPath(key, '.'), "/")));

        if (config_.contains(path)) {
            return config_.value(path, nlohmann::json());
        } else {
            return std::nullopt;
        }
    }

protected:
    nlohmann::json config_;
};

class FileBackend : public JsonBackend {
public:
    explicit FileBackend(const std::filesystem::path& path) {
        try {
            config_ = nlohmann::json::parse(StringFromFile(path));
        } catch (const std::system_error& e) {
            if (e.code() != std::errc::no_such_file_or_directory) {
                throw;
            }
        }
    }
};

class MemoryBackend : public Config::Backend {
public:
    MemoryBackend(std::map<std::string, nlohmann::json, std::less<>> values)
        : values_(std::move(values)) {
    }

    std::optional<nlohmann::json> Get(const std::string_view key) const override {
        if (auto vi = values_.find(key); vi != values_.end()) {
            return vi->second;
        }
        return std::nullopt;
    }

private:
    std::map<std::string, nlohmann::json, std::less<>> values_;
};

} // namespace

Config::Config()
    : backends_(size_t(ConfigLocation::Default) + 1) {
}

Config::Config(std::map<ConfigLocation, std::unique_ptr<Backend>> locations)
    : Config() {
    for (auto& [location, backend] : locations) {
        assert(backend);

        backends_.at(size_t(location)).swap(backend);
    }
}

std::unique_ptr<Config::Backend> Config::MakeBackend(const std::filesystem::path& path) {
    return std::make_unique<FileBackend>(path);
}

std::unique_ptr<Config::Backend> Config::MakeBackend(nlohmann::json config) {
    return std::make_unique<JsonBackend>(std::move(config));
}

std::unique_ptr<Config::Backend> Config::MakeBackend(
    std::map<std::string, nlohmann::json, std::less<>> config
) {
    return std::make_unique<MemoryBackend>(std::move(config));
}

std::optional<nlohmann::json> Config::Get(const std::string_view key) const {
    return GetImpl(key, std::nullopt);
}

std::optional<nlohmann::json> Config::Get(const std::string_view key, const ConfigLocation location) const {
    return GetImpl(key, location);
}

void Config::Reset(const ConfigLocation location, std::unique_ptr<Backend> backend) {
    backends_.at(size_t(location)).swap(backend);
}

std::optional<nlohmann::json> Config::GetImpl(
    const std::string_view key, const std::optional<ConfigLocation> location
) const {
    // Get from the specific location.
    if (location) {
        if (const auto& backend = backends_.at(size_t(*location))) {
            return backend->Get(key);
        }
        return std::nullopt;
    }
    // Get from first match.
    for (const auto& backend : backends_) {
        if (backend) {
            if (auto value = backend->Get(key)) {
                return value;
            }
        }
    }
    // Nothing was found.
    return std::nullopt;
}

} // namespace Vcs
