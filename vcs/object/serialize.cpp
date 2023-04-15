#include "serialize.h"

#include <vcs/api/fbs/commit.fb.h>
#include <vcs/api/fbs/tree.fb.h>

using namespace flatbuffers;

namespace Vcs {

std::string CommitBuilder::Serialize() {
    FlatBufferBuilder fbb;

    assert(this->tree);
    assert(this->generation > 0);

    auto saveSignature = [&](const Signature& sig) {
        Offset<String> name = 0;
        Offset<String> id = 0;
        if (!sig.name.empty()) {
            name = fbb.CreateString(sig.name);
        }
        if (!sig.id.empty()) {
            id = fbb.CreateString(sig.id);
        }
        Fbs::SignatureBuilder builder(fbb);
        builder.add_name(name);
        builder.add_id(id);
        builder.add_when(sig.when);
        return builder.Finish();
    };

    Offset<Vector<Offset<Fbs::Attribute>>> attributes = 0;
    Offset<Fbs::Signature> author = this->author.Empty() ? 0 : saveSignature(this->author);
    Offset<Fbs::Signature> committer = this->committer.Empty() ? 0 : saveSignature(this->committer);
    Offset<String> message = this->message.empty() ? 0 : fbb.CreateString(this->message);
    Offset<Vector<uint8_t>> tree = fbb.CreateVector(this->tree.Data(), this->tree.Size());
    Offset<Vector<uint8_t>> parents = 0;
    Offset<Vector<uint8_t>> renames =
        this->renames ? fbb.CreateVector(this->renames.Data(), this->renames.Size()) : 0;

    if (!this->parents.empty()) {
        const size_t size = this->parents[0].Size();
        std::vector<uint8_t> data(this->parents.size() * size);
        for (size_t i = 0; i < this->parents.size(); ++i) {
            memcpy(data.data() + i * size, this->parents[i].Data(), size);
        }
        parents = fbb.CreateVector(data);
    }
    if (!this->attributes.empty()) {
        // Sort by name.
        sort(this->attributes.begin(), this->attributes.end(),
             [](const auto& a, const auto& b) { return a.name < b.name; });

        std::vector<Offset<Fbs::Attribute>> tmp;
        for (const auto& attr : this->attributes) {
            if (attr.Empty()) {
                continue;
            }
            const auto name = fbb.CreateString(attr.name);
            const auto value = fbb.CreateString(attr.value);
            tmp.push_back(Fbs::CreateAttribute(fbb, name, value));
        }
        if (tmp.size()) {
            attributes = fbb.CreateVector(tmp);
        }
    }

    Fbs::CommitBuilder builder(fbb);

    builder.add_attributes(attributes);
    builder.add_author(author);
    builder.add_committer(committer);
    builder.add_generation(this->generation);
    builder.add_message(message);
    builder.add_parents(parents);
    builder.add_tree(tree);
    builder.add_renames(renames);

    fbb.Finish(builder.Finish());

    return std::string((const char*)fbb.GetBufferPointer(), fbb.GetSize());
}

TreeBuilder& TreeBuilder::Append(std::string name, const PathEntry& entry) {
    entries_.emplace_back(std::move(name), entry);
    return *this;
}

TreeBuilder& TreeBuilder::Append(const Tree::Entry& entry) {
    entries_.emplace_back(
        // Name.
        entry.Name(),
        // Attributes.
        PathEntry{.id = entry.Id(), .type = entry.Type(), .size = entry.Size()});
    return *this;
}

bool TreeBuilder::Empty() const noexcept {
    return entries_.empty();
}

std::string TreeBuilder::Serialize() {
    FlatBufferBuilder fbb(CalculateBufferSize());

    if (entries_.empty()) {
        fbb.Finish(Fbs::CreateTree(fbb, 0));
        return std::string((const char*)fbb.GetBufferPointer(), fbb.GetSize());
    }

    // Sort entries by name so later we can use binary search for entry lookup.
    std::sort(entries_.begin(), entries_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Validate entries.
    for (size_t i = 0, end = entries_.size(); i < end; ++i) {
        assert(entries_[i].first.empty() == false);

        if (i > 0) {
            assert(entries_[i - 1].first < entries_[i].first);
        }
    }

    std::vector<Offset<Fbs::TreeEntry>> entries;
    entries.reserve(entries_.size());
    for (const auto& [entry_name, entry] : entries_) {
        const auto id = fbb.CreateVector(entry.id.Data(), entry.id.Size());
        const auto name = fbb.CreateString(entry_name);

        Fbs::TreeEntryBuilder builder(fbb);

        builder.add_id(id);
        builder.add_type(Fbs::PathType(entry.type));
        builder.add_name(name);
        builder.add_size(entry.size);

        entries.push_back(builder.Finish());
    }

    fbb.Finish(Fbs::CreateTree(fbb, fbb.CreateVector(entries)));

    return std::string((const char*)fbb.GetBufferPointer(), fbb.GetSize());
}

size_t TreeBuilder::CalculateBufferSize() const noexcept {
    return entries_.size() >= 10 ? 4096 : 1024;
}

} // namespace Vcs
