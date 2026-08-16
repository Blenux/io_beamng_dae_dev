/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "msgpack_reader.h"

namespace cdae {

MsgPackValue MsgPackReader::parse_value()
{
    if (!check(1)) {
        error = MsgPackError::UnexpectedEnd;
        return {};
    }

    const uint8_t tag = read_u8();

    if ((tag & 0xF0) == 0x80) {
        const uint32_t n = tag & 0x0F;
        return parse_map(n);
    }
    if ((tag & 0xF0) == 0x90) {
        const uint32_t n = tag & 0x0F;
        return parse_array(n);
    }
    if ((tag & 0xE0) == 0xA0) {
        const uint32_t n = tag & 0x1F;
        if (!check(n)) {
            error = MsgPackError::UnexpectedEnd;
            return {};
        }
        std::string_view s(reinterpret_cast<const char *>(data_ + pos_), n);
        pos_ += n;
        return MsgPackValue::make_string(s);
    }
    if (tag <= 0x7F) {
        return MsgPackValue::make_uint(tag);
    }
    if (tag >= 0xE0) {
        return MsgPackValue::make_int(int8_t(tag));
    }

    switch (tag) {
        case 0xC0: return {};
        case 0xC2: return MsgPackValue::make_bool(false);
        case 0xC3: return MsgPackValue::make_bool(true);

        case 0xCC:
            if (!check(1)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return MsgPackValue::make_uint(read_u8());
        case 0xCD:
            if (!check(2)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return MsgPackValue::make_uint(read_u16());
        case 0xCE:
            if (!check(4)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return MsgPackValue::make_uint(read_u32());
        case 0xCF:
            if (!check(8)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return MsgPackValue::make_uint(read_u64());

        case 0xD0:
            if (!check(1)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return MsgPackValue::make_int(int8_t(read_u8()));
        case 0xD1:
            if (!check(2)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return MsgPackValue::make_int(read_i16());
        case 0xD2:
            if (!check(4)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return MsgPackValue::make_int(read_i32());
        case 0xD3:
            if (!check(8)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return MsgPackValue::make_int(read_i64());

        case 0xCA:
            if (!check(4)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return MsgPackValue::make_float(read_f32());
        case 0xCB:
            if (!check(8)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return MsgPackValue::make_float(read_f64());

        case 0xD9: {
            if (!check(1)) { error = MsgPackError::UnexpectedEnd; return {}; }
            uint32_t n = read_u8();
            if (!check(n)) { error = MsgPackError::UnexpectedEnd; return {}; }
            std::string_view s(reinterpret_cast<const char *>(data_ + pos_), n);
            pos_ += n;
            return MsgPackValue::make_string(s);
        }
        case 0xDA: {
            if (!check(2)) { error = MsgPackError::UnexpectedEnd; return {}; }
            uint32_t n = read_u16();
            if (!check(n)) { error = MsgPackError::UnexpectedEnd; return {}; }
            std::string_view s(reinterpret_cast<const char *>(data_ + pos_), n);
            pos_ += n;
            return MsgPackValue::make_string(s);
        }
        case 0xDB: {
            if (!check(4)) { error = MsgPackError::UnexpectedEnd; return {}; }
            uint32_t n = read_u32();
            if (!check(n)) { error = MsgPackError::UnexpectedEnd; return {}; }
            std::string_view s(reinterpret_cast<const char *>(data_ + pos_), n);
            pos_ += n;
            return MsgPackValue::make_string(s);
        }

        case 0xC4: {
            if (!check(1)) { error = MsgPackError::UnexpectedEnd; return {}; }
            uint32_t n = read_u8();
            if (!check(n)) { error = MsgPackError::UnexpectedEnd; return {}; }
            MsgPackValue v = MsgPackValue::make_binary(data_ + pos_, n);
            pos_ += n;
            return v;
        }
        case 0xC5: {
            if (!check(2)) { error = MsgPackError::UnexpectedEnd; return {}; }
            uint32_t n = read_u16();
            if (!check(n)) { error = MsgPackError::UnexpectedEnd; return {}; }
            MsgPackValue v = MsgPackValue::make_binary(data_ + pos_, n);
            pos_ += n;
            return v;
        }
        case 0xC6: {
            if (!check(4)) { error = MsgPackError::UnexpectedEnd; return {}; }
            uint32_t n = read_u32();
            if (!check(n)) { error = MsgPackError::UnexpectedEnd; return {}; }
            MsgPackValue v = MsgPackValue::make_binary(data_ + pos_, n);
            pos_ += n;
            return v;
        }

        case 0xDC:
            if (!check(2)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return parse_array(read_u16());
        case 0xDD:
            if (!check(4)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return parse_array(read_u32());

        case 0xDE:
            if (!check(2)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return parse_map(read_u16());
        case 0xDF:
            if (!check(4)) { error = MsgPackError::UnexpectedEnd; return {}; }
            return parse_map(read_u32());

        default:
            error = MsgPackError::UnsupportedType;
            return {};
    }
}

MsgPackValue MsgPackReader::parse_array(uint32_t n)
{
    if (n == 0) {
        return MsgPackValue::make_array({});
    }
    std::vector<MsgPackValue> elems;
    elems.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        elems.push_back(parse_value());
        if (error != MsgPackError::None) {
            return {};
        }
    }
    return MsgPackValue::make_array(std::move(elems));
}

MsgPackValue MsgPackReader::parse_map(uint32_t n)
{
    if (n == 0) {
        return MsgPackValue::make_map({}, {});
    }
    std::vector<MsgPackValue> keys;
    std::vector<MsgPackValue> vals;
    keys.reserve(n);
    vals.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        keys.push_back(parse_value());
        if (error != MsgPackError::None) return {};
        vals.push_back(parse_value());
        if (error != MsgPackError::None) return {};
    }
    return MsgPackValue::make_map(std::move(keys), std::move(vals));
}

const MsgPackValue *MsgPackReader::map_get(const MsgPackValue &map, std::string_view key) const
{
    if (map.type != MsgPackValue::Type::Map) {
        return nullptr;
    }
    for (size_t i = 0; i < map.map_keys.size(); i++) {
        const MsgPackValue &k = map.map_keys[i];
        if (k.type == MsgPackValue::Type::String && k.str == key) {
            return &map.map_vals[i];
        }
    }
    return nullptr;
}

} // namespace cdae
