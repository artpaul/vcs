#include <cmd/local/workspace.h>
#include <vcs/changes/path.h>
#include <vcs/object/commit.h>

#include <util/tty.h>

#include <contrib/cxxopts/cxxopts.hpp>
#include <contrib/fmt/fmt/color.h>
#include <contrib/fmt/fmt/format.h>

namespace Vcs {
namespace {

struct Options {
    HashId id{};
};

int DumpObject(const Options& options, const Workspace& repo) {
    const auto obj = repo.Objects().Load(options.id);

    if (obj.Type() == DataType::Blob) {
        const auto blob = obj.AsBlob();

        fmt::print("blob {} {}\n", options.id, obj.Size());
        // Disable output bufferization since the data is already in memory.
        std::setbuf(stdout, nullptr);
        // Write the blob.
        std::fwrite(blob.Data(), 1, blob.Size(), stdout);

        return 0;
    }

    if (obj.Type() == DataType::Commit) {
        const auto commit = obj.AsCommit();

        fmt::print("commit {} {}\n", options.id, obj.Size());
        fmt::print("tree       {}\n", commit.Tree());
        fmt::print("generation {}\n", commit.Generation());
        // Parents.
        for (const auto& p : commit.Parents()) {
            fmt::print("parent     {}\n", p);
        }
        // Message.
        fmt::print("message    {}\n", commit.Message());

        return 0;
    }

    if (obj.Type() == DataType::Index) {
        const auto index = obj.AsIndex();

        fmt::print("index {} {}\n", options.id, obj.Size());

        fmt::print("oid  {}\n", index.Id());
        fmt::print("type {}\n", int(index.Type()));
        fmt::print("size {}\n", index.Size());
        // Parts.
        for (const auto& p : index.Parts()) {
            fmt::print("blob {} {}\n", p.Id(), p.Size());
        }

        return 0;
    }

    if (obj.Type() == DataType::Tree) {
        const auto tree = obj.AsTree();

        fmt::print("tree {} {}\n", options.id, obj.Size());
        // Entries.
        for (const auto& e : tree.Entries()) {
            fmt::print("{} {} {} {}\n", e.Id(), int(e.Type()), e.Size(), e.Name());
        }

        return 0;
    }

    return 1;
}

} // namespace

int ExecuteDump(int argc, char* argv[], const std::function<Workspace&()>& cb) {
    Options options;

    {
        cxxopts::options spec(fmt::format("vcs {}", argv[0]));
        spec.add_options(
            "",
            {
                {"h,help", "print help"},
                {"args", "free args", cxxopts::value<std::vector<std::string>>()},
            }
        );

        spec.parse_positional("args");
        spec.custom_help("[<options>]");
        spec.positional_help("<object>");

        const auto& opts = spec.parse(argc, argv);
        if (opts.has("help")) {
            fmt::print("{}\n", spec.help());
            return 0;
        }
        if (opts.has("args")) {
            const auto& args = opts["args"].as<std::vector<std::string>>();
            const auto& repo = cb();

            if (const auto id = repo.ResolveReference(args[0])) {
                options.id = *id;
            }
        }
    }

    // Dump object.
    if (options.id) {
        return DumpObject(options, cb());
    }

    return 1;
}

} // namespace Vcs
