#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

template <std::integral T>
size_t EncodeVarint(T value, uint8_t* buf, size_t len) noexcept {
    size_t size = 0;

    while (size != len) {
        buf[size++] = (value & 0x7F) | (value > 0x7F ? 0x80 : 0x00);

        value >>= 7;

        if (!value) {
            break;
        }
    }

    return value ? 0 : size;
}

template <std::integral T>
bool DecodeVarint(const uint8_t* buf, size_t len, T& value) {
    value = T();

    for (size_t i = 0; i != len; ++i) {
        value |= T(buf[i] & 0x7F) << (7 * i);

        if (!(buf[i] & 0x80)) {
            return true;
        }
    }
    return false;
}
