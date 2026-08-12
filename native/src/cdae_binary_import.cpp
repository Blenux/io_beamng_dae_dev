/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "cdae_binary_import.h"
#include "cdae_shape.h"
#include "msgpack_reader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <zstd.h>

namespace cdae {

/* -------------------------------------------------------------------- */
/** \name Helpers
 * \{ */

static std::vector<uint8_t> read_file_bytes(const char *filepath)
{
    std::vector<uint8_t> result;
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        return result;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        fclose(fp);
        return result;
    }
    result.resize(size);
    size_t read = fread(result.data(), 1, size, fp);
    fclose(fp);
    if (read != size_t(size)) {
        result.clear();
    }
    return result;
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

static std::vector<uint8_t> decompress_zstd(const uint8_t *data, size_t size)
{
    std::vector<uint8_t> result;
    unsigned long long frame_size = ZSTD_getFrameContentSize(data, size);
    if (frame_size == ZSTD_CONTENTSIZE_ERROR || frame_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        return result;
    }
    size_t uncompressed_size = size_t(frame_size);
    result.resize(uncompressed_size);
    size_t ret = ZSTD_decompress(result.data(), uncompressed_size, data, size);
    if (ZSTD_isError(ret)) {
        result.clear();
        return result;
    }
    if (ret != uncompressed_size) {
        result.resize(ret);
    }
    return result;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Body Parser
 * \{ */

static int32_t read_int32(MsgPackReader &reader, bool &ok)
{
    MsgPackValue v = reader.parse_value();
    if (v.type == MsgPackValue::Type::Int) { ok = true; return int32_t(v.i); }
    if (v.type == MsgPackValue::Type::Uint) { ok = true; return int32_t(v.u); }
    if (v.type == MsgPackValue::Type::Float) { ok = true; return int32_t(v.f); }
    ok = false;
    return 0;
}

static float read_float(MsgPackReader &reader, bool &ok)
{
    MsgPackValue v = reader.parse_value();
    if (v.type == MsgPackValue::Type::Float) { ok = true; return float(v.f); }
    if (v.type == MsgPackValue::Type::Int) { ok = true; return float(v.i); }
    if (v.type == MsgPackValue::Type::Uint) { ok = true; return float(v.u); }
    ok = false;
    return 0.0f;
}

static VectorBlock read_vector_block(MsgPackReader &reader, bool &ok)
{
    VectorBlock block;
    bool ok2;
    block.count = read_int32(reader, ok2);
    if (!ok2) { ok = false; return block; }
    block.element_size = read_int32(reader, ok2);
    if (!ok2) { ok = false; return block; }
    MsgPackValue bytes = reader.parse_value();
    if (bytes.type == MsgPackValue::Type::Nil) {
        ok = true;
        return block;
    }
    if (bytes.type == MsgPackValue::Type::Map || bytes.type == MsgPackValue::Type::Array) {
        ok = true;
        return block;
    }
    if (bytes.type != MsgPackValue::Type::Binary) {
        ok = false;
        return block;
    }
    block.bytes = std::move(bytes.bin);
    /* Clamp count to actual available data to prevent out-of-bounds reads. */
    if (block.element_size > 0) {
        int max_count = int(block.bytes.size()) / block.element_size;
        if (block.count > max_count) {
            block.count = max_count;
        }
    }
    ok = true;
    return block;
}

static bool read_float_array(MsgPackReader &reader, float *out, int size)
{
    MsgPackValue v = reader.parse_value();
    if (v.type == MsgPackValue::Type::Array) {
        if (int(v.arr.size()) < size) return false;
        for (int i = 0; i < size; i++) {
            if (v.arr[i].type == MsgPackValue::Type::Float) out[i] = float(v.arr[i].f);
            else if (v.arr[i].type == MsgPackValue::Type::Int) out[i] = float(v.arr[i].i);
            else if (v.arr[i].type == MsgPackValue::Type::Uint) out[i] = float(v.arr[i].u);
            else return false;
        }
        return true;
    }
    if (v.type == MsgPackValue::Type::Binary) {
        if (int(v.bin.size()) < size * 4) return false;
        const float *src = reinterpret_cast<const float *>(v.bin.data());
        for (int i = 0; i < size; i++) {
            out[i] = src[i];
        }
        return true;
    }
    return false;
}

static bool parse_body(ShapeData &shape, MsgPackReader &reader)
{
    bool ok;
    shape.smallest_visible_size = read_float(reader, ok);
    if (!ok) return false;
    shape.smallest_visible_dl = read_int32(reader, ok);
    if (!ok) return false;
    shape.radius = read_float(reader, ok);
    if (!ok) return false;
    shape.tube_radius = read_float(reader, ok);
    if (!ok) return false;
    if (!read_float_array(reader, shape.center, 3)) return false;
    if (!read_float_array(reader, shape.bounds, 6)) return false;

    /* Vectors */
    shape.raw_nodes = read_vector_block(reader, ok); if (!ok) return false;
    shape.raw_objects = read_vector_block(reader, ok); if (!ok) return false;
    shape.sub_shape_first_node = read_vector_block(reader, ok); if (!ok) return false;
    shape.sub_shape_first_object = read_vector_block(reader, ok); if (!ok) return false;
    shape.sub_shape_num_nodes = read_vector_block(reader, ok); if (!ok) return false;
    shape.sub_shape_num_objects = read_vector_block(reader, ok); if (!ok) return false;
    shape.default_rotations = read_vector_block(reader, ok); if (!ok) return false;
    shape.default_translations = read_vector_block(reader, ok); if (!ok) return false;
    shape.node_rotations = read_vector_block(reader, ok); if (!ok) return false;
    shape.node_translations = read_vector_block(reader, ok); if (!ok) return false;
    shape.node_uniform_scales = read_vector_block(reader, ok); if (!ok) return false;
    shape.node_aligned_scales = read_vector_block(reader, ok); if (!ok) return false;
    shape.node_arbitrary_scale_factors = read_vector_block(reader, ok); if (!ok) return false;
    shape.node_arbitrary_scale_rots = read_vector_block(reader, ok); if (!ok) return false;
    shape.ground_translations = read_vector_block(reader, ok); if (!ok) return false;
    shape.ground_rotations = read_vector_block(reader, ok); if (!ok) return false;
    shape.object_states = read_vector_block(reader, ok); if (!ok) return false;
    shape.triggers = read_vector_block(reader, ok); if (!ok) return false;
    shape.details = read_vector_block(reader, ok); if (!ok) return false;

    /* Names */
    int32_t names_count = read_int32(reader, ok);
    if (!ok) return false;
    for (int i = 0; i < names_count; i++) {
        MsgPackValue name = reader.parse_value();
        if (name.type == MsgPackValue::Type::String) {
            shape.names.push_back(std::string(name.str.data(), name.str.size()));
        } else {
            shape.names.push_back("");
        }
    }

    /* Meshes */
    int32_t meshes_count = read_int32(reader, ok);
    if (!ok) return false;
    for (int i = 0; i < meshes_count; i++) {
        Mesh mesh;
        mesh.is_dae = false;
        mesh.mesh_type = uint32_t(read_int32(reader, ok));
        if (!ok) return false;

        if (mesh.mesh_type == MESH_NULL) {
            shape.meshes.push_back(std::move(mesh));
            continue;
        }

        mesh.num_frames = read_int32(reader, ok); if (!ok) return false;
        mesh.num_mat_frames = read_int32(reader, ok); if (!ok) return false;
        mesh.parent_mesh = read_int32(reader, ok); if (!ok) return false;
        if (!read_float_array(reader, mesh.bounds, 6)) return false;
        if (!read_float_array(reader, mesh.center, 3)) return false;
        mesh.radius = read_float(reader, ok); if (!ok) return false;

        mesh.verts = read_vector_block(reader, ok); if (!ok) return false;
        mesh.tverts = read_vector_block(reader, ok); if (!ok) return false;
        mesh.tverts2 = read_vector_block(reader, ok); if (!ok) return false;
        mesh.colors = read_vector_block(reader, ok); if (!ok) return false;
        mesh.norms = read_vector_block(reader, ok); if (!ok) return false;
        mesh.encoded_norms = read_vector_block(reader, ok); if (!ok) return false;
        mesh.primitives = read_vector_block(reader, ok); if (!ok) return false;
        mesh.indices = read_vector_block(reader, ok); if (!ok) return false;
        mesh.tangents = read_vector_block(reader, ok); if (!ok) return false;

        mesh.verts_per_frame = read_int32(reader, ok); if (!ok) return false;
        mesh.mesh_flags = uint32_t(read_int32(reader, ok)); if (!ok) return false;

        shape.meshes.push_back(std::move(mesh));
    }

    /* Sequences (skip but consume) */
    int32_t seq_count = read_int32(reader, ok);
    if (!ok) return false;
    for (int i = 0; i < seq_count; i++) {
        read_int32(reader, ok); /* nameIndex */
        read_int32(reader, ok); /* flags */
        read_int32(reader, ok); /* numKeyframes */
        read_float(reader, ok); /* duration */
        read_int32(reader, ok); /* priority */
        read_int32(reader, ok); /* firstGroundFrame */
        read_int32(reader, ok); /* numGroundFrames */
        read_int32(reader, ok); /* baseRotation */
        read_int32(reader, ok); /* baseTranslation */
        read_int32(reader, ok); /* baseScale */
        read_int32(reader, ok); /* baseObjectState */
        read_int32(reader, ok); /* baseDecalState */
        read_int32(reader, ok); /* firstTrigger */
        read_int32(reader, ok); /* numTriggers */
        read_float(reader, ok); /* toolBegin */
        /* Integer sets: each is a msgpack array */
        for (int j = 0; j < 6; j++) {
            reader.parse_value();
        }
    }

    /* Materials */
    int32_t mat_count = read_int32(reader, ok);
    if (!ok) return false;
    /* PBR flag: each material has base_color[4], roughness, metallic.
     * Old CDAE files won't have this — detect by checking if next value is Bool vs String. */
    bool has_pbr = false;
    MsgPackValue first_val = reader.parse_value();
    if (first_val.type == MsgPackValue::Type::Bool) {
        has_pbr = first_val.b;
    }
    for (int i = 0; i < mat_count; i++) {
        Material mat;
        /* For old format (no PBR flag), first_val is the first material's name. */
        MsgPackValue name = (i == 0 && first_val.type != MsgPackValue::Type::Bool)
                                ? std::move(first_val)
                                : reader.parse_value();
        if (name.type == MsgPackValue::Type::String) {
            mat.name = std::string(name.str.data(), name.str.size());
        }
        mat.flags = uint32_t(read_int32(reader, ok));
        mat.reflectance_map = uint32_t(read_int32(reader, ok));
        mat.bump_map = uint32_t(read_int32(reader, ok));
        mat.detail_map = uint32_t(read_int32(reader, ok));
        mat.detail_scale = read_float(reader, ok);
        mat.reflection_amount = read_float(reader, ok);
        /* Read PBR properties if flag was set. */
        if (has_pbr) {
            float bc[4] = {0.8f, 0.8f, 0.8f, 1.0f};
            if (read_float_array(reader, bc, 4)) {
                mat.base_color[0] = bc[0];
                mat.base_color[1] = bc[1];
                mat.base_color[2] = bc[2];
                mat.base_color[3] = bc[3];
            }
            mat.roughness = read_float(reader, ok);
            if (!ok) { mat.roughness = 0.5f; ok = true; }
            mat.metallic = read_float(reader, ok);
            if (!ok) { mat.metallic = 0.0f; ok = true; }
        }
        shape.materials.push_back(std::move(mat));
    }

    return true;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Public API
 * \{ */

std::unique_ptr<ShapeData> parse_cdae(const char *filepath)
{
    std::vector<uint8_t> file_bytes = read_file_bytes(filepath);
    if (file_bytes.empty()) {
        throw std::runtime_error("Cannot read .cdae file");
    }
    return parse_cdae_bytes(file_bytes.data(), file_bytes.size());
}

std::unique_ptr<ShapeData> parse_cdae_bytes(const uint8_t *data, size_t size)
{
    if (size < 8) {
        throw std::runtime_error(".cdae file too small");
    }

    uint32_t version = read_u32_le(data);
    uint32_t header_size = read_u32_le(data + 4);
    uint32_t sm_version = version & 0xFF;

    if (sm_version != 15 && sm_version != 31) {
        throw std::runtime_error("Unsupported .cdae version");
    }

    if (size < 8 + header_size) {
        throw std::runtime_error(".cdae file truncated");
    }

    /* Parse header dict. */
    MsgPackReader header_reader(data + 8, header_size);
    MsgPackValue header = header_reader.parse_value();
    if (header.type != MsgPackValue::Type::Map) {
        throw std::runtime_error("Invalid .cdae header");
    }

    const MsgPackValue *compression = header_reader.map_get(header, "compression");
    bool is_compressed = (compression && compression->type == MsgPackValue::Type::Bool &&
                          compression->b);

    /* Read body. */
    size_t body_offset = 8 + header_size;
    if (body_offset >= size) {
        throw std::runtime_error(".cdae missing body");
    }

    const uint8_t *body_data = data + body_offset;
    size_t body_size = size - body_offset;

    std::vector<uint8_t> body_decompressed;
    const uint8_t *body_ptr;
    size_t body_len;

    if (is_compressed) {
        body_decompressed = decompress_zstd(body_data, body_size);
        if (body_decompressed.empty()) {
            throw std::runtime_error("Failed to decompress .cdae body");
        }
        body_ptr = body_decompressed.data();
        body_len = body_decompressed.size();
    } else {
        body_ptr = body_data;
        body_len = body_size;
    }

    /* Parse body. */
    auto shape = std::make_unique<ShapeData>();
    MsgPackReader body_reader(body_ptr, body_len);

    if (!parse_body(*shape, body_reader)) {
        throw std::runtime_error("Failed to parse .cdae body");
    }

    return shape;
}

/** \} */

} // namespace cdae
