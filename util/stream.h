#pragma once

#include <cassert>
#include <cstddef>
#include <memory>

class InputStream {
public:
    template <typename S>
    explicit InputStream(S& s)
        : source_(std::addressof(s))
        , do_read_([](void* source, void* buf, size_t len) {
            return static_cast<S*>(source)->Read(buf, len);
        }) {
        assert(source_);
    }

public:
    /* Reads some data from the stream. */
    size_t Read(void* buf, const size_t len) {
        return do_read_(source_, buf, len);
    }

    /* Loads some data from the stream. */
    size_t Load(void* buf, const size_t len) {
        std::byte* p = std::bit_cast<std::byte*>(buf);
        std::byte* end = p + len;

        while (p != end) {
            if (const size_t read = do_read_(source_, p, end - p)) {
                p += read;
            } else {
                break;
            }
        }

        return len - (end - p);
    }

private:
    /// Pointer to an object which implements Read.
    void* source_;
    ///
    size_t (*do_read_)(void*, void*, size_t);
};
