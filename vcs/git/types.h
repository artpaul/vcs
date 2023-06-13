#pragma once

#include <vcs/object/hashid.h>

#include <contrib/json/nlohmann.hpp>

namespace Vcs {
namespace Git {

struct Remap {
    HashId git;
    HashId vcs;

    static Remap Load(const std::string_view data) {
        auto json = nlohmann::json::parse(data);
        Remap remap;

        remap.git = HashId::FromHex(json["git"].get<std::string>());
        remap.vcs = HashId::FromHex(json["vcs"].get<std::string>());

        return remap;
    }

    static std::string Save(const Remap& rec) {
        auto json = nlohmann::json::object();

        json["git"] = rec.git.ToHex();
        json["vcs"] = rec.vcs.ToHex();

        return json.dump();
    }
};

} // namespace Git
} // namespace Vcs
