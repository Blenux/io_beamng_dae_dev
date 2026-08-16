/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace cdae {

class MsgPackWriter {
private:
    std::vector<uint8_t> bytes_;

    void write_u8(uint8_t v) { bytes_.push_back(v); }
    void write_u16_be(uint16_t v) {
        bytes_.push_back(uint8_t(v >> 8));
        bytes_.push_back(uint8_t(v));
    }
    void write_u32_be(uint32_t v) {
        bytes_.push_back(uint8_t(v >> 24));
        bytes_.push_back(uint8_t(v >> 16));
        bytes_.push_back(uint8_t(v >> 8));
        bytes_.push_back(uint8_t(v));
    }
    void write_u64_be(uint64_t v) {
        for (int i = 7; i >= 0; i--) bytes_.push_back(uint8_t(v >> (i * 8)));
    }
    void write_i64_be(int64_t v) { write_u64_be(uint64_t(v)); }
    void write_f32_be(float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        write_u32_be(u);
    }
    void write_f64_be(double v) {
        uint64_t u;
        std::memcpy(&u, &v, 8);
        write_u64_be(u);
    }

public:
    MsgPackWriter() = default;

    std::vector<uint8_t> take_bytes() { return std::move(bytes_); }
    size_t size() const { return bytes_.size(); }
    void clear() { bytes_.clear(); }

    void write_nil() { write_u8(0xC0); }
    void write_bool(bool v) { write_u8(v ? 0xC3 : 0xC2); }

    void write_int32(int32_t v) {
        if (v >= 0 && v <= 127) {
            write_u8(uint8_t(v));
        } else if (v < 0 && v >= -32) {
            write_u8(uint8_t(v));
        } else if (v >= -128 && v <= 127) {
            write_u8(0xD0);
            write_u8(uint8_t(v));
        } else if (v >= -32768 && v <= 32767) {
            write_u8(0xD1);
            write_u16_be(uint16_t(v));
        } else {
            write_u8(0xD2);
            write_u32_be(uint32_t(v));
        }
    }

    void write_int64(int64_t v) {
        if (v >= -2147483648LL && v <= 2147483647LL) {
            write_int32(int32_t(v));
        } else {
            write_u8(0xD3);
            write_i64_be(v);
        }
    }

    void write_uint32(uint32_t v) {
        if (v <= 127) {
            write_u8(uint8_t(v));
        } else if (v <= 255) {
            write_u8(0xCC);
            write_u8(uint8_t(v));
        } else if (v <= 65535) {
            write_u8(0xCD);
            write_u16_be(uint16_t(v));
        } else {
            write_u8(0xCE);
            write_u32_be(v);
        }
    }

    void write_float(float v) {
        write_u8(0xCA);
        write_f32_be(v);
    }

    void write_double(double v) {
        write_u8(0xCB);
        write_f64_be(v);
    }

    void write_string(std::string_view s) {
        size_t n = s.size();
        if (n <= 31) {
            write_u8(0xA0 | uint8_t(n));
        } else if (n <= 255) {
            write_u8(0xD9);
            write_u8(uint8_t(n));
        } else if (n <= 65535) {
            write_u8(0xDA);
            write_u16_be(uint16_t(n));
        } else {
            write_u8(0xDB);
            write_u32_be(uint32_t(n));
        }
        for (size_t i = 0; i < n; i++) {
            bytes_.push_back(uint8_t(s[i]));
        }
    }

    void write_string(const char *s) {
        write_string(std::string_view(s));
    }

    void write_binary(const uint8_t *data, size_t n) {
        if (n <= 255) {
            write_u8(0xC4);
            write_u8(uint8_t(n));
        } else if (n <= 65535) {
            write_u8(0xC5);
            write_u16_be(uint16_t(n));
        } else {
            write_u8(0xC6);
            write_u32_be(uint32_t(n));
        }
        for (size_t i = 0; i < n; i++) {
            bytes_.push_back(data[i]);
        }
    }

    void write_binary(const std::vector<uint8_t> &data) {
        write_binary(data.data(), data.size());
    }

    void write_array_size(uint32_t n) {
        if (n <= 15) {
            write_u8(0x90 | uint8_t(n));
        } else if (n <= 65535) {
            write_u8(0xDC);
            write_u16_be(uint16_t(n));
        } else {
            write_u8(0xDD);
            write_u32_be(n);
        }
    }

    void write_map_size(uint32_t n) {
        if (n <= 15) {
            write_u8(0x80 | uint8_t(n));
        } else if (n <= 65535) {
            write_u8(0xDE);
            write_u16_be(uint16_t(n));
        } else {
            write_u8(0xDF);
            write_u32_be(n);
        }
    }

    void write_float_array(const float *values, int count) {
        write_array_size(uint32_t(count));
        for (int i = 0; i < count; i++) {
            write_float(values[i]);
        }
    }
};

} // namespace cdae
