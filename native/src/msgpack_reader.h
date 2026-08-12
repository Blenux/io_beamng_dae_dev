/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace cdae {

enum class MsgPackError {
    None = 0,
    UnexpectedEnd,
    InvalidType,
    UnsupportedType,
};

struct MsgPackValue {
    enum class Type : uint8_t {
        Nil,
        Bool,
        Int,
        Uint,
        Float,
        String,
        Binary,
        Array,
        Map,
    };

    Type type = Type::Nil;

    bool b = false;
    int64_t i = 0;
    uint64_t u = 0;
    double f = 0.0;

    std::string_view str;
    std::vector<uint8_t> bin;

    std::vector<MsgPackValue> arr;
    std::vector<MsgPackValue> map_keys;
    std::vector<MsgPackValue> map_vals;

    MsgPackValue() = default;

    static MsgPackValue make_bool(bool val) {
        MsgPackValue v;
        v.type = Type::Bool;
        v.b = val;
        return v;
    }
    static MsgPackValue make_int(int64_t val) {
        MsgPackValue v;
        v.type = Type::Int;
        v.i = val;
        return v;
    }
    static MsgPackValue make_uint(uint64_t val) {
        MsgPackValue v;
        v.type = Type::Uint;
        v.u = val;
        return v;
    }
    static MsgPackValue make_float(double val) {
        MsgPackValue v;
        v.type = Type::Float;
        v.f = val;
        return v;
    }
    static MsgPackValue make_string(std::string_view s) {
        MsgPackValue v;
        v.type = Type::String;
        v.str = s;
        return v;
    }
    static MsgPackValue make_binary(const uint8_t *data, size_t size) {
        MsgPackValue v;
        v.type = Type::Binary;
        v.bin.assign(data, data + size);
        return v;
    }
    static MsgPackValue make_array(std::vector<MsgPackValue> a) {
        MsgPackValue v;
        v.type = Type::Array;
        v.arr = std::move(a);
        return v;
    }
    static MsgPackValue make_map(std::vector<MsgPackValue> keys, std::vector<MsgPackValue> vals) {
        MsgPackValue v;
        v.type = Type::Map;
        v.map_keys = std::move(keys);
        v.map_vals = std::move(vals);
        return v;
    }
};

class MsgPackReader {
public:
    MsgPackError error = MsgPackError::None;

private:
    const uint8_t *data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;

    bool check(size_t n) const { return (pos_ + n) <= size_; }

    uint8_t read_u8() { return data_[pos_++]; }

    uint16_t read_u16() {
        uint16_t v;
        std::memcpy(&v, data_ + pos_, 2);
        pos_ += 2;
        return swap16(v);
    }
    uint32_t read_u32() {
        uint32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        pos_ += 4;
        return swap32(v);
    }
    uint64_t read_u64() {
        uint64_t v;
        std::memcpy(&v, data_ + pos_, 8);
        pos_ += 8;
        return swap64(v);
    }

    int16_t read_i16() { return int16_t(read_u16()); }
    int32_t read_i32() { return int32_t(read_u32()); }
    int64_t read_i64() { return int64_t(read_u64()); }

    float read_f32() {
        uint32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        pos_ += 4;
        v = swap32(v);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    double read_f64() {
        uint64_t v;
        std::memcpy(&v, data_ + pos_, 8);
        pos_ += 8;
        v = swap64(v);
        double d;
        std::memcpy(&d, &v, 8);
        return d;
    }

    static uint16_t swap16(uint16_t v) {
        return (uint16_t((v >> 8) & 0xFF)) | (uint16_t((v & 0xFF) << 8));
    }
    static uint32_t swap32(uint32_t v) {
        return ((v >> 24) & 0x000000FF) | ((v >> 8) & 0x0000FF00) |
               ((v << 8) & 0x00FF0000) | ((v << 24) & 0xFF000000);
    }
    static uint64_t swap64(uint64_t v) {
        return ((v >> 56) & 0x00000000000000FF) | ((v >> 40) & 0x000000000000FF00) |
               ((v >> 24) & 0x0000000000FF0000) | ((v >> 8) & 0x00000000FF000000) |
               ((v << 8) & 0x000000FF00000000) | ((v << 24) & 0x0000FF0000000000) |
               ((v << 40) & 0x00FF000000000000) | ((v << 56) & 0xFF00000000000000);
    }

public:
    MsgPackReader() = default;
    MsgPackReader(const uint8_t *data, size_t size) : data_(data), size_(size) {}

    MsgPackValue parse_value();

    std::vector<uint8_t> read_bytes(size_t n) {
        if (!check(n)) {
            error = MsgPackError::UnexpectedEnd;
            return {};
        }
        std::vector<uint8_t> s(data_ + pos_, data_ + pos_ + n);
        pos_ += n;
        return s;
    }

    size_t position() const { return pos_; }
    size_t remaining() const { return size_ - pos_; }
    bool done() const { return pos_ >= size_; }

    const MsgPackValue *map_get(const MsgPackValue &map, std::string_view key) const;

private:
    MsgPackValue parse_array(uint32_t n);
    MsgPackValue parse_map(uint32_t n);
};

} // namespace cdae
