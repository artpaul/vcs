#pragma once

#include <vcs/changes/revwalk.h>
#include <vcs/object/hashid.h>
#include <vcs/object/store.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace Vcs::Git {

class Converter {
    struct Options {
        /// Cache oids of converted blobs.
        bool use_blob_chache = true;
        /// Store hash of original commit in converted object.
        bool store_original_hash = true;
    };

public:
    explicit Converter(const std::filesystem::path& path);

    ~Converter();

    /**
     * Sets remap handler.
     */
    Converter& SetRemap(std::function<HashId(const HashId&)> remap);

    /**
     * Converts commit to internal format.
     *
     * @param id git commit.
     * @param odb object storage.
     * @return HashId
     */
    HashId ConvertCommit(const HashId& id, Datastore* odb);

    /**
     * Lists commits reachable from the head in order suitable for conversion.
     */
    void ListCommits(const std::string& head, const std::function<WalkAction(const HashId&)>& cb) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Vcs::Git