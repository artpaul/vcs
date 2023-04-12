#include "hashid.h"

#include <contrib/sha/sha1.h>

namespace Vcs {
namespace {

constexpr char HEX_DIGITS[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
};

template <size_t N, size_t M>
void BytesToHex(const unsigned char (&data)[N], char (&buf)[M]) noexcept {
    static_assert(2 * N == M);

    for (size_t i = 0; i < N; ++i) {
        buf[2 * i] = HEX_DIGITS[(data[i] >> 4) & 0x0F];
        buf[2 * i + 1] = HEX_DIGITS[data[i] & 0x0F];
    }
}

uint8_t HexToByte(const char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    throw std::invalid_argument(fmt::format("invalid hex character '{}'", ch));
}

} // namespace

class HashId::Builder::Impl {
public:
    Impl() noexcept {
        platform_SHA1_Init(&ctx_);
    }

    void Append(const void* data, const size_t len) noexcept {
        platform_SHA1_Update(&ctx_, static_cast<const char*>(data), len);
    }

    HashId Build() noexcept {
        HashId id;
        platform_SHA1_Final(id.data_, &ctx_);
        return id;
    }

private:
    platform_SHA_CTX ctx_;
};

HashId::Builder::Builder()
    : impl_(std::make_unique<Impl>()) {
}

HashId::Builder::~Builder() = default;

HashId::Builder& HashId::Builder::Append(const DataHeader header) noexcept {
    impl_->Append(header.Data(), header.Bytes());
    return *this;
}

HashId::Builder& HashId::Builder::Append(const void* data, const size_t len) noexcept {
    impl_->Append(data, len);
    return *this;
}

HashId::Builder& HashId::Builder::Append(const std::string_view data) noexcept {
    impl_->Append(data.data(), data.size());
    return *this;
}

HashId HashId::Builder::Build() noexcept {
    return impl_->Build();
}

HashId HashId::FromBytes(const void* data, const size_t len) {
    if (len != sizeof(data_)) {
        throw std::invalid_argument(fmt::format("invalid size of bytes '{}'", len));
    }

    HashId id;
    std::memcpy(id.data_, data, len);
    return id;
}

HashId HashId::FromBytes(const std::string_view data) {
    return FromBytes(data.data(), data.size());
}

HashId HashId::FromHex(const std::string_view hex) {
    if (hex.size() != 2 * sizeof(data_)) {
        throw std::invalid_argument(fmt::format("invalid size of hex string '{}'", hex.size()));
    }

    HashId id;
    for (size_t i = 0; i < sizeof(data_); ++i) {
        id.data_[i] = HexToByte(hex[2 * i]) << 4 | HexToByte(hex[2 * i + 1]);
    }
    return id;
}

bool HashId::IsBytes(const std::string_view data) noexcept {
    return data.size() == sizeof(data_);
}

bool HashId::IsHex(const std::string_view hex) noexcept {
    if (hex.size() != 2 * sizeof(data_)) {
        return false;
    }
    for (const char ch : hex) {
        if ((ch < '0' || ch > '9') && (ch < 'a' || ch > 'f') && (ch < 'A' || ch > 'F')) {
            return false;
        }
    }
    return true;
}

HashId HashId::Make(const DataType type, const std::string_view content) {
    return Builder().Append(DataHeader::Make(type, content.size())).Append(content).Build();
}

std::string HashId::ToHex() const {
    char hex[2 * sizeof(data_)];
    BytesToHex(data_, hex);
    return std::string(hex, sizeof(hex));
}

std::string HashId::ToBytes() const {
    return std::string(reinterpret_cast<const char*>(data_), sizeof(data_));
}

std::ostream& operator<<(std::ostream& output, const HashId& id) {
    char hex[2 * sizeof(id.data_)];
    BytesToHex(id.data_, hex);
    output.write(hex, sizeof(hex));
    return output;
}

} // namespace Vcs
