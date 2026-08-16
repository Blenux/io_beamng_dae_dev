/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "cdae_binary_export.h"
#include "cdae_shape.h"
#include "msgpack_writer.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <zstd.h>

namespace cdae {

/* -------------------------------------------------------------------- */
/** \name Helpers
 * \{ */

static void write_vector_block(MsgPackWriter &w, const VectorBlock &block)
{
    w.write_int32(block.count);
    w.write_int32(block.element_size);
    if (block.bytes.empty()) {
        w.write_nil();
    } else {
        w.write_binary(block.bytes.data(), block.bytes.size());
    }
}

static void write_float_array(MsgPackWriter &w, const float *values, int count)
{
    w.write_array_size(uint32_t(count));
    for (int i = 0; i < count; i++) {
        w.write_float(values[i]);
    }
}

static std::vector<uint8_t> build_body(const ShapeData &shape)
{
    MsgPackWriter body;

    /* Shape info. */
    body.write_float(shape.smallest_visible_size);
    body.write_int32(shape.smallest_visible_dl);
    body.write_float(shape.radius);
    body.write_float(shape.tube_radius);
    write_float_array(body, shape.center, 3);
    write_float_array(body, shape.bounds, 6);

    /* Vectors. */
    write_vector_block(body, shape.raw_nodes);
    write_vector_block(body, shape.raw_objects);
    write_vector_block(body, shape.sub_shape_first_node);
    write_vector_block(body, shape.sub_shape_first_object);
    write_vector_block(body, shape.sub_shape_num_nodes);
    write_vector_block(body, shape.sub_shape_num_objects);
    write_vector_block(body, shape.default_rotations);
    write_vector_block(body, shape.default_translations);
    write_vector_block(body, shape.node_rotations);
    write_vector_block(body, shape.node_translations);
    write_vector_block(body, shape.node_uniform_scales);
    write_vector_block(body, shape.node_aligned_scales);
    write_vector_block(body, shape.node_arbitrary_scale_factors);
    write_vector_block(body, shape.node_arbitrary_scale_rots);
    write_vector_block(body, shape.ground_translations);
    write_vector_block(body, shape.ground_rotations);
    write_vector_block(body, shape.object_states);
    write_vector_block(body, shape.triggers);
    write_vector_block(body, shape.details);

    /* Names. */
    body.write_int32(int32_t(shape.names.size()));
    for (const std::string &name : shape.names) {
        body.write_string(name);
    }

    /* Meshes. */
    body.write_int32(int32_t(shape.meshes.size()));
    for (const Mesh &mesh : shape.meshes) {
        body.write_int32(int32_t(mesh.mesh_type));
        if (mesh.mesh_type == MESH_NULL) {
            continue;
        }
        body.write_int32(mesh.num_frames);
        body.write_int32(mesh.num_mat_frames);
        body.write_int32(mesh.parent_mesh);
        write_float_array(body, mesh.bounds, 6);
        write_float_array(body, mesh.center, 3);
        body.write_float(mesh.radius);

        write_vector_block(body, mesh.verts);
        write_vector_block(body, mesh.tverts);
        write_vector_block(body, mesh.tverts2);
        write_vector_block(body, mesh.colors);
        write_vector_block(body, mesh.norms);
        write_vector_block(body, mesh.encoded_norms);
        write_vector_block(body, mesh.primitives);
        write_vector_block(body, mesh.indices);
        write_vector_block(body, mesh.tangents);

        body.write_int32(mesh.verts_per_frame);
        body.write_int32(int32_t(mesh.mesh_flags));
    }

    /* Sequences. */
    body.write_int32(0);

    /* Materials. */
    body.write_int32(int32_t(shape.materials.size()));
    /* PBR flag: each material has base_color[4], roughness, metallic. */
    body.write_bool(true);
    for (const Material &mat : shape.materials) {
        body.write_string(mat.name);
        body.write_int32(int32_t(mat.flags));
        body.write_int32(int32_t(mat.reflectance_map));
        body.write_int32(int32_t(mat.bump_map));
        body.write_int32(int32_t(mat.detail_map));
        body.write_float(mat.detail_scale);
        body.write_float(mat.reflection_amount);
        /* PBR properties for round-trip fidelity. */
        body.write_float_array(mat.base_color, 4);
        body.write_float(mat.roughness);
        body.write_float(mat.metallic);
    }

    return body.take_bytes();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Public API
 * \{ */

bool write_cdae(const char *filepath, const ShapeData &shape, bool compress)
{
    std::vector<uint8_t> body_bytes = build_body(shape);

    /* Compress body if requested. */
    std::vector<uint8_t> compressed;
    if (compress) {
        size_t max_compressed = ZSTD_compressBound(body_bytes.size());
        compressed.resize(max_compressed);
        size_t ret = ZSTD_compress(compressed.data(), max_compressed,
                                    body_bytes.data(), body_bytes.size(), 3);
        if (ZSTD_isError(ret)) {
            throw std::runtime_error("ZSTD compression failed");
        }
        compressed.resize(ret);
    }

    /* Build header dict. */
    MsgPackWriter header;
    header.write_map_size(4);
    header.write_string("info");
    header.write_string("BeamNG CDAE export");
    header.write_string("compression");
    header.write_bool(compress);
    header.write_string("bodysize");
    header.write_int32(int32_t(compress ? compressed.size() : body_bytes.size()));
    header.write_string("objectNames");
    /* Header must list object names (meshes), not material names.
     * shape.names[0] is the root node; objects follow from index 1. */
    size_t obj_count = (shape.names.size() > 1) ? (shape.names.size() - 1) : 0;
    header.write_array_size(uint32_t(obj_count));
    for (size_t i = 1; i < shape.names.size(); i++) {
        header.write_string(shape.names[i]);
    }
    std::vector<uint8_t> header_bytes = header.take_bytes();

    /* Write file. */
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        throw std::runtime_error("Cannot open file for writing");
    }

    /* File version header: little-endian uint16(31), uint16(0), uint32(header_size). */
    /* Write as explicit little-endian bytes for cross-platform portability. */
    uint16_t ver = 31;
    uint16_t expver = 0;
    uint32_t hsize = uint32_t(header_bytes.size());
    fputc(ver & 0xFF, fp);
    fputc((ver >> 8) & 0xFF, fp);
    fputc(expver & 0xFF, fp);
    fputc((expver >> 8) & 0xFF, fp);
    fputc(hsize & 0xFF, fp);
    fputc((hsize >> 8) & 0xFF, fp);
    fputc((hsize >> 16) & 0xFF, fp);
    fputc((hsize >> 24) & 0xFF, fp);
    fwrite(header_bytes.data(), 1, header_bytes.size(), fp);

    if (compress) {
        fwrite(compressed.data(), 1, compressed.size(), fp);
    } else {
        fwrite(body_bytes.data(), 1, body_bytes.size(), fp);
    }

    fclose(fp);
    return true;
}

/** \} */

} // namespace cdae
