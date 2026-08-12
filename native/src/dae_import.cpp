/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "dae_import.h"
#include "cdae_shape.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pugixml.hpp"

namespace cdae {

/* -------------------------------------------------------------------- */
/** \name XML Helper Functions
 * \{ */

static std::string strip_ns(const std::string &tag)
{
    size_t pos = tag.find(':');
    if (pos != std::string::npos) {
        return tag.substr(pos + 1);
    }
    return tag;
}

static pugi::xml_node child_by_tag(const pugi::xml_node &parent, const char *tag)
{
    for (pugi::xml_node child = parent.first_child(); child; child = child.next_sibling()) {
        if (strip_ns(child.name()) == tag) {
            return child;
        }
    }
    return pugi::xml_node();
}

static std::vector<pugi::xml_node> children_by_tag(const pugi::xml_node &parent, const char *tag)
{
    std::vector<pugi::xml_node> result;
    for (pugi::xml_node child = parent.first_child(); child; child = child.next_sibling()) {
        if (strip_ns(child.name()) == tag) {
            result.push_back(child);
        }
    }
    return result;
}

static std::vector<float> parse_float_array_text(const char *text)
{
    std::vector<float> result;
    if (!text) {
        return result;
    }
    const char *p = text;
    while (*p) {
        while (*p && isspace(static_cast<unsigned char>(*p))) {
            p++;
        }
        if (!*p) {
            break;
        }
        char *end;
        float val = strtof(p, &end);
        if (end == p) {
            p++;
            continue;
        }
        result.push_back(val);
        p = end;
    }
    return result;
}

static std::vector<int> parse_int_array_text(const char *text)
{
    std::vector<int> result;
    if (!text) {
        return result;
    }
    const char *p = text;
    while (*p) {
        while (*p && isspace(static_cast<unsigned char>(*p))) {
            p++;
        }
        if (!*p) {
            break;
        }
        char *end;
        long val = strtol(p, &end, 10);
        if (end == p) {
            p++;
            continue;
        }
        result.push_back(int(val));
        p = end;
    }
    return result;
}

static std::string attr_source_id(const pugi::xml_node &node, const char *attr_name)
{
    pugi::xml_attribute attr = node.attribute(attr_name);
    if (attr.empty()) {
        return "";
    }
    const char *val = attr.value();
    if (val[0] == '#') {
        val++;
    }
    return std::string(val);
}

/* BLX - Safe string-to-int conversion using strtol with error checking.
 * Returns 0 on parse failure, clamps to INT_MIN/INT_MAX on overflow. */
static int safe_atoi(const char *str)
{
    if (!str || !*str) return 0;
    char *end = nullptr;
    errno = 0;
    long val = strtol(str, &end, 10);
    if (end == str || errno == ERANGE) return 0;
    return int(val);
}

/* BLX - Safe string-to-float conversion using strtof with error checking.
 * Returns 0.0f on parse failure. */
static float safe_atof(const char *str)
{
    if (!str || !*str) return 0.0f;
    char *end = nullptr;
    errno = 0;
    float val = strtof(str, &end);
    if (end == str || errno == ERANGE) return 0.0f;
    return val;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name DAE Source Data Structures
 * \{ */

struct DaeSource {
    std::vector<float> data;
    int count = 0;
    int stride = 1;
};

struct DaeInput {
    std::string semantic;
    std::string source;
    int offset = 0;
    int set = 0;
};

struct DaeTriangles {
    std::string material_name;
    int triangle_count = 0;
    std::vector<int> indices;
    std::vector<DaeInput> inputs;
};

struct DaeGeometry {
    std::string id;
    std::string name;
    std::map<std::string, DaeSource> sources;
    std::vector<DaeTriangles> triangles;
};

struct DaeNode {
    std::string name;
    std::string id;
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::string geometry_url;
    std::map<std::string, std::string> material_bindings;
    std::vector<DaeNode> children;
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name DAE XML Parsing
 * \{ */

static DaeSource parse_source(const pugi::xml_node &src_node)
{
    DaeSource src;
    pugi::xml_node float_array = child_by_tag(src_node, "float_array");
    if (float_array) {
        src.data = parse_float_array_text(float_array.text().get());
    }

    pugi::xml_node tech_common = child_by_tag(src_node, "technique_common");
    if (tech_common) {
        pugi::xml_node accessor = child_by_tag(tech_common, "accessor");
        if (accessor) {
            pugi::xml_attribute count_attr = accessor.attribute("count");
            pugi::xml_attribute stride_attr = accessor.attribute("stride");
            if (count_attr) {
                src.count = safe_atoi(count_attr.value());
            }
            if (stride_attr) {
                src.stride = safe_atoi(stride_attr.value());
            }
            else {
                src.stride = 1;
            }
        }
    }

    if (src.count == 0 && !src.data.empty()) {
        src.count = int(src.data.size()) / src.stride;
    }

    return src;
}

static DaeInput parse_input(const pugi::xml_node &input_node)
{
    DaeInput input;
    pugi::xml_attribute sem = input_node.attribute("semantic");
    pugi::xml_attribute src = input_node.attribute("source");
    pugi::xml_attribute offset = input_node.attribute("offset");
    pugi::xml_attribute set = input_node.attribute("set");

    if (sem) {
        input.semantic = sem.value();
    }
    if (src) {
        const char *val = src.value();
        if (val[0] == '#') {
            val++;
        }
        input.source = val;
    }
    if (offset) {
        input.offset = safe_atoi(offset.value());
    }
    if (set) {
        input.set = safe_atoi(set.value());
    }
    return input;
}

static DaeTriangles parse_triangles(const pugi::xml_node &tri_node)
{
    DaeTriangles result;
    pugi::xml_attribute mat = tri_node.attribute("material");
    pugi::xml_attribute count = tri_node.attribute("count");

    if (mat) {
        result.material_name = mat.value();
    }
    if (count) {
        result.triangle_count = safe_atoi(count.value());
    }

    pugi::xml_node p_elem = child_by_tag(tri_node, "p");
    if (p_elem) {
        result.indices = parse_int_array_text(p_elem.text().get());
    }

    for (pugi::xml_node input : children_by_tag(tri_node, "input")) {
        result.inputs.push_back(parse_input(input));
    }

    return result;
}

static DaeTriangles parse_polylist(const pugi::xml_node &poly_node)
{
    DaeTriangles result;
    pugi::xml_attribute mat = poly_node.attribute("material");
    pugi::xml_attribute count = poly_node.attribute("count");

    if (mat) {
        result.material_name = mat.value();
    }
    if (count) {
        result.triangle_count = safe_atoi(count.value());
    }

    for (pugi::xml_node input : children_by_tag(poly_node, "input")) {
        result.inputs.push_back(parse_input(input));
    }

    pugi::xml_node p_elem = child_by_tag(poly_node, "p");
    pugi::xml_node vcount_elem = child_by_tag(poly_node, "vcount");
    if (!p_elem || !vcount_elem) {
        return result;
    }

    std::vector<int> raw_indices = parse_int_array_text(p_elem.text().get());
    std::vector<int> vcount = parse_int_array_text(vcount_elem.text().get());

    int stride = 1;
    for (const auto &input : result.inputs) {
        if (input.offset + 1 > stride) {
            stride = input.offset + 1;
        }
    }

    /* Fan triangulate. */
    std::vector<int> tri_indices;
    int tri_count = 0;
    int cursor = 0;
    for (int n : vcount) {
        if (n < 3) {
            cursor += n;
            continue;
        }
        for (int i = 1; i < n - 1; i++) {
            tri_count++;
            for (int s = 0; s < stride; s++) {
                tri_indices.push_back(raw_indices[cursor * stride + s]);
            }
            for (int s = 0; s < stride; s++) {
                tri_indices.push_back(raw_indices[(cursor + i) * stride + s]);
            }
            for (int s = 0; s < stride; s++) {
                tri_indices.push_back(raw_indices[(cursor + i + 1) * stride + s]);
            }
        }
        cursor += n;
    }

    result.indices = std::move(tri_indices);
    result.triangle_count = tri_count;
    return result;
}

static DaeGeometry parse_geometry(const pugi::xml_node &geo_node)
{
    DaeGeometry geo;
    pugi::xml_attribute id_attr = geo_node.attribute("id");
    pugi::xml_attribute name_attr = geo_node.attribute("name");
    if (id_attr) {
        geo.id = id_attr.value();
    }
    if (name_attr) {
        geo.name = name_attr.value();
    }

    pugi::xml_node mesh = child_by_tag(geo_node, "mesh");
    if (!mesh) {
        return geo;
    }

    for (pugi::xml_node src : children_by_tag(mesh, "source")) {
        pugi::xml_attribute src_id = src.attribute("id");
        if (src_id) {
            geo.sources[src_id.value()] = parse_source(src);
        }
    }

    pugi::xml_node vertices = child_by_tag(mesh, "vertices");
    if (vertices) {
        pugi::xml_attribute vert_id = vertices.attribute("id");
        pugi::xml_node vert_input = child_by_tag(vertices, "input");
        if (vert_id && vert_input) {
            std::string vert_key = vert_id.value();
            std::string pos_src = attr_source_id(vert_input, "source");
            auto it = geo.sources.find(pos_src);
            if (it != geo.sources.end()) {
                geo.sources[vert_key] = it->second;
            }
        }
    }

    for (pugi::xml_node tri : children_by_tag(mesh, "triangles")) {
        geo.triangles.push_back(parse_triangles(tri));
    }
    for (pugi::xml_node poly : children_by_tag(mesh, "polylist")) {
        geo.triangles.push_back(parse_polylist(poly));
    }

    return geo;
}

static DaeNode parse_node(const pugi::xml_node &node_elem)
{
    DaeNode node;
    pugi::xml_attribute name_attr = node_elem.attribute("name");
    pugi::xml_attribute id_attr = node_elem.attribute("id");
    if (name_attr) {
        node.name = name_attr.value();
    }
    if (id_attr) {
        node.id = id_attr.value();
    }

    pugi::xml_node matrix_elem = child_by_tag(node_elem, "matrix");
    if (matrix_elem) {
        std::vector<float> mat_vals = parse_float_array_text(matrix_elem.text().get());
        if (mat_vals.size() >= 16) {
            memcpy(node.matrix, mat_vals.data(), 16 * sizeof(float));
        }
    }

    pugi::xml_node inst_geo = child_by_tag(node_elem, "instance_geometry");
    if (inst_geo) {
        node.geometry_url = attr_source_id(inst_geo, "url");

        pugi::xml_node bind_mat = child_by_tag(inst_geo, "bind_material");
        if (bind_mat) {
            pugi::xml_node tech_common = child_by_tag(bind_mat, "technique_common");
            if (tech_common) {
                for (pugi::xml_node inst_mat : children_by_tag(tech_common, "instance_material")) {
                    pugi::xml_attribute symbol = inst_mat.attribute("symbol");
                    pugi::xml_attribute target = inst_mat.attribute("target");
                    if (symbol && target) {
                        const char *target_val = target.value();
                        if (target_val[0] == '#') {
                            target_val++;
                        }
                        node.material_bindings[symbol.value()] = target_val;
                    }
                }
            }
        }
    }

    for (pugi::xml_node child : children_by_tag(node_elem, "node")) {
        node.children.push_back(parse_node(child));
    }

    return node;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name DAE → Mesh Conversion
 * \{ */

static int get_stride(const DaeTriangles &tri)
{
    int max_offset = 0;
    for (const auto &input : tri.inputs) {
        if (input.offset > max_offset) {
            max_offset = input.offset;
        }
    }
    return max_offset + 1;
}

static const DaeInput *find_input(const std::vector<DaeInput> &inputs,
                                   const std::string &semantic,
                                   int set = 0)
{
    for (const auto &input : inputs) {
        if (input.semantic == semantic && input.set == set) {
            return &input;
        }
    }
    return nullptr;
}

static std::vector<float> get_indexed_array(const DaeTriangles &tri,
                                             const DaeGeometry &geo,
                                             const std::string &semantic,
                                             int set,
                                             int components)
{
    const DaeInput *input = find_input(tri.inputs, semantic, set);
    if (!input) {
        return {};
    }

    auto src_it = geo.sources.find(input->source);
    if (src_it == geo.sources.end()) {
        return {};
    }

    const DaeSource &src = src_it->second;
    int stride = get_stride(tri);
    int total_corners = tri.triangle_count * 3;
    int indices_size = int(tri.indices.size());

    std::vector<float> result;
    result.resize(total_corners * components);

    for (int corner = 0; corner < total_corners; corner++) {
        /* BLX - Bounds-check the index access against tri.indices size. */
        int idx_pos = corner * stride + input->offset;
        if (idx_pos < 0 || idx_pos >= indices_size) {
            for (int c = 0; c < components; c++) {
                result[corner * components + c] = 0.0f;
            }
            continue;
        }
        int idx = tri.indices[idx_pos];
        if (idx < 0) {
            for (int c = 0; c < components; c++) {
                result[corner * components + c] = 0.0f;
            }
            continue;
        }
        for (int c = 0; c < components; c++) {
            int src_idx = idx * src.stride + c;
            if (src_idx >= 0 && src_idx < int(src.data.size())) {
                result[corner * components + c] = src.data[src_idx];
            }
            else {
                result[corner * components + c] = 0.0f;
            }
        }
    }

    return result;
}

static void fill_block(VectorBlock &block, const void *data, int count, int elem_size)
{
    block.count = count;
    block.element_size = elem_size;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    block.bytes.assign(bytes, bytes + count * elem_size);
}

static Mesh convert_geometry(const DaeGeometry &geo,
                              const std::map<std::string, int> &mat_name_to_idx,
                              const std::map<std::string, int> &mat_id_to_idx,
                              float unit_meter)
{
    Mesh mesh;
    mesh.mesh_type = MESH_STANDARD;
    mesh.is_dae = true;

    int total_triangles = 0;
    for (const auto &tri : geo.triangles) {
        total_triangles += tri.triangle_count;
    }

    if (total_triangles == 0) {
        mesh.mesh_type = MESH_NULL;
        return mesh;
    }

    int total_corners = total_triangles * 3;

    std::vector<float> dst_verts(total_corners * 3, 0.0f);
    std::vector<float> dst_norms(total_corners * 3, 0.0f);
    std::vector<float> dst_tverts0(total_corners * 2, 0.0f);
    std::vector<float> dst_tverts1(total_corners * 2, 0.0f);
    std::vector<uint8_t> dst_colors(total_corners * 4, 255);
    std::vector<int32_t> dst_indices(total_corners);
    std::vector<uint32_t> dst_primitives;

    bool has_norms = false;
    bool has_tverts0 = false;
    bool has_tverts1 = false;
    bool has_colors = false;

    int corner_offset = 0;
    for (const auto &tri : geo.triangles) {
        int mat_idx = 0;
        const std::string &mat_name = tri.material_name;
        auto name_it = mat_name_to_idx.find(mat_name);
        if (name_it != mat_name_to_idx.end()) {
            mat_idx = name_it->second;
        }
        else {
            auto id_it = mat_id_to_idx.find(mat_name);
            if (id_it != mat_id_to_idx.end()) {
                mat_idx = id_it->second;
            }
        }

        int tri_count = tri.triangle_count;
        int vtx_count = tri_count * 3;

        std::vector<float> src_verts = get_indexed_array(tri, geo, "VERTEX", 0, 3);
        std::vector<float> src_norms = get_indexed_array(tri, geo, "NORMAL", 0, 3);
        std::vector<float> src_tverts0 = get_indexed_array(tri, geo, "TEXCOORD", 0, 2);
        std::vector<float> src_tverts1 = get_indexed_array(tri, geo, "TEXCOORD", 1, 2);
        std::vector<float> src_colors = get_indexed_array(tri, geo, "COLOR", 0, 4);

        int next_offset = corner_offset + vtx_count;

        if (!src_verts.empty()) {
            for (int i = 0; i < vtx_count; i++) {
                dst_verts[(corner_offset + i) * 3 + 0] = src_verts[i * 3 + 0] * unit_meter;
                dst_verts[(corner_offset + i) * 3 + 1] = src_verts[i * 3 + 1] * unit_meter;
                dst_verts[(corner_offset + i) * 3 + 2] = src_verts[i * 3 + 2] * unit_meter;
            }
        }

        if (!src_norms.empty()) {
            has_norms = true;
            for (int i = 0; i < vtx_count; i++) {
                dst_norms[(corner_offset + i) * 3 + 0] = src_norms[i * 3 + 0];
                dst_norms[(corner_offset + i) * 3 + 1] = src_norms[i * 3 + 1];
                dst_norms[(corner_offset + i) * 3 + 2] = src_norms[i * 3 + 2];
            }
        }

        if (!src_tverts0.empty()) {
            has_tverts0 = true;
            for (int i = 0; i < vtx_count; i++) {
                dst_tverts0[(corner_offset + i) * 2 + 0] = src_tverts0[i * 2 + 0];
                dst_tverts0[(corner_offset + i) * 2 + 1] = src_tverts0[i * 2 + 1];
            }
        }

        if (!src_tverts1.empty()) {
            has_tverts1 = true;
            for (int i = 0; i < vtx_count; i++) {
                dst_tverts1[(corner_offset + i) * 2 + 0] = src_tverts1[i * 2 + 0];
                dst_tverts1[(corner_offset + i) * 2 + 1] = src_tverts1[i * 2 + 1];
            }
        }

        if (!src_colors.empty()) {
            has_colors = true;
            for (int i = 0; i < vtx_count; i++) {
                dst_colors[(corner_offset + i) * 4 + 0] = uint8_t(
                    std::clamp(src_colors[i * 4 + 0], 0.0f, 1.0f) * 255.0f);
                dst_colors[(corner_offset + i) * 4 + 1] = uint8_t(
                    std::clamp(src_colors[i * 4 + 1], 0.0f, 1.0f) * 255.0f);
                dst_colors[(corner_offset + i) * 4 + 2] = uint8_t(
                    std::clamp(src_colors[i * 4 + 2], 0.0f, 1.0f) * 255.0f);
                dst_colors[(corner_offset + i) * 4 + 3] = uint8_t(
                    std::clamp(src_colors[i * 4 + 3], 0.0f, 1.0f) * 255.0f);
            }
        }

        uint32_t index_start = uint32_t(corner_offset);
        uint32_t index_count = uint32_t(vtx_count);
        uint32_t raw_info = uint32_t(mat_idx) & 0x0FFFFFFF;
        dst_primitives.push_back(index_start);
        dst_primitives.push_back(index_count);
        dst_primitives.push_back(raw_info);

        corner_offset = next_offset;
    }

    /* Build indices: pre-reversed per triangle (2,1,0, 5,4,3, ...) so that
     * the Python mesh builder's winding reversal produces correct CCW order. */
    for (int tri = 0; tri * 3 + 2 < total_corners; tri++) {
        dst_indices[tri * 3 + 0] = tri * 3 + 2;
        dst_indices[tri * 3 + 1] = tri * 3 + 1;
        dst_indices[tri * 3 + 2] = tri * 3 + 0;
    }

    fill_block(mesh.verts, dst_verts.data(), total_corners, 12);
    if (has_norms) {
        fill_block(mesh.norms, dst_norms.data(), total_corners, 12);
    }
    if (has_tverts0) {
        fill_block(mesh.tverts, dst_tverts0.data(), total_corners, 8);
    }
    if (has_tverts1) {
        fill_block(mesh.tverts2, dst_tverts1.data(), total_corners, 8);
    }
    if (has_colors) {
        fill_block(mesh.colors, dst_colors.data(), total_corners, 4);
    }
    fill_block(mesh.indices, dst_indices.data(), total_corners, 4);

    int prim_count = int(dst_primitives.size()) / 3;
    fill_block(mesh.primitives, dst_primitives.data(), prim_count, 12);

    mesh.verts_per_frame = total_corners;
    mesh.num_frames = 1;
    mesh.num_mat_frames = 1;

    return mesh;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Matrix Decomposition
 * \{ */

static void mat3_to_quat(float quat[4], const float m[3][3])
{
    /* Standard rotation matrix to quaternion (Shepperd's method). */
    float trace = m[0][0] + m[1][1] + m[2][2];
    float qw, qx, qy, qz;

    if (trace > 0.0f) {
        float s = sqrtf(trace + 1.0f) * 2.0f; /* s = 4 * qw */
        qw = 0.25f * s;
        qx = (m[2][1] - m[1][2]) / s;
        qy = (m[0][2] - m[2][0]) / s;
        qz = (m[1][0] - m[0][1]) / s;
    }
    else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = sqrtf(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f; /* s = 4 * qx */
        qw = (m[2][1] - m[1][2]) / s;
        qx = 0.25f * s;
        qy = (m[0][1] + m[1][0]) / s;
        qz = (m[0][2] + m[2][0]) / s;
    }
    else if (m[1][1] > m[2][2]) {
        float s = sqrtf(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f; /* s = 4 * qy */
        qw = (m[0][2] - m[2][0]) / s;
        qx = (m[0][1] + m[1][0]) / s;
        qy = 0.25f * s;
        qz = (m[1][2] + m[2][1]) / s;
    }
    else {
        float s = sqrtf(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f; /* s = 4 * qz */
        qw = (m[1][0] - m[0][1]) / s;
        qx = (m[0][2] + m[2][0]) / s;
        qy = (m[1][2] + m[2][1]) / s;
        qz = 0.25f * s;
    }

    /* Return as (w, x, y, z) — caller will swap to (x, y, z, w) for CDAE convention. */
    quat[0] = qw;
    quat[1] = qx;
    quat[2] = qy;
    quat[3] = qz;
}

static void decompose_matrix(const float m[16],
                              float quat[4],
                              float trans[3],
                              float scale[3])
{
    trans[0] = m[3];
    trans[1] = m[7];
    trans[2] = m[11];

    float mat3[3][3];
    mat3[0][0] = m[0]; mat3[0][1] = m[1]; mat3[0][2] = m[2];
    mat3[1][0] = m[4]; mat3[1][1] = m[5]; mat3[1][2] = m[6];
    mat3[2][0] = m[8]; mat3[2][1] = m[9]; mat3[2][2] = m[10];

    /* Extract scale as column lengths (magnitude only). */
    scale[0] = sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    scale[1] = sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    scale[2] = sqrtf(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    if (scale[0] <= 0.0f) scale[0] = 1.0f;
    if (scale[1] <= 0.0f) scale[1] = 1.0f;
    if (scale[2] <= 0.0f) scale[2] = 1.0f;

    /* Detect negative scale (reflection) via determinant; negate Z if det < 0. */
    float det = mat3[0][0] * (mat3[1][1] * mat3[2][2] - mat3[1][2] * mat3[2][1])
              - mat3[0][1] * (mat3[1][0] * mat3[2][2] - mat3[1][2] * mat3[2][0])
              + mat3[0][2] * (mat3[1][0] * mat3[2][1] - mat3[1][1] * mat3[2][0]);
    if (det < 0.0f) {
        scale[2] = -scale[2];
    }

    /* Divide mat3 by signed scale to get pure rotation matrix. */
    float inv_sx = 1.0f / scale[0];
    float inv_sy = 1.0f / scale[1];
    float inv_sz = 1.0f / scale[2];
    mat3[0][0] *= inv_sx; mat3[1][0] *= inv_sx; mat3[2][0] *= inv_sx;
    mat3[0][1] *= inv_sy; mat3[1][1] *= inv_sy; mat3[2][1] *= inv_sy;
    mat3[0][2] *= inv_sz; mat3[1][2] *= inv_sz; mat3[2][2] *= inv_sz;

    mat3_to_quat(quat, mat3);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Node Hierarchy Flattening
 * \{ */

struct NodeBuildState {
    std::vector<std::string> names;
    std::vector<int32_t> node_raws;
    std::vector<int32_t> object_raws;
    std::vector<float> default_rots;
    std::vector<float> default_trans;
    std::vector<float> default_scales;
    std::map<std::string, int> geometry_name_to_mesh_start;
};

static int process_node(const DaeNode &dae_node,
                         int parent_idx,
                         const std::map<std::string, DaeGeometry> &geometries,
                         NodeBuildState &state)
{
    int node_idx = int(state.node_raws.size()) / 5;

    int name_idx = int(state.names.size());
    state.names.push_back(dae_node.name.empty() ? dae_node.id : dae_node.name);

    float quat[4], trans[3], scale[3];
    decompose_matrix(dae_node.matrix, quat, trans, scale);

    /* Store rotation as float quaternion in CDAE convention: (x, y, z, w).
     * mat3_to_quat returns (w, x, y, z), so we swap the order. */
    state.default_rots.push_back(quat[1]);
    state.default_rots.push_back(quat[2]);
    state.default_rots.push_back(quat[3]);
    state.default_rots.push_back(quat[0]);

    state.default_trans.push_back(trans[0]);
    state.default_trans.push_back(trans[1]);
    state.default_trans.push_back(trans[2]);

    state.default_scales.push_back(scale[0]);
    state.default_scales.push_back(scale[1]);
    state.default_scales.push_back(scale[2]);

    int node_slot = int(state.node_raws.size());
    state.node_raws.resize(node_slot + 5);
    state.node_raws[node_slot + 0] = name_idx;
    state.node_raws[node_slot + 1] = parent_idx;
    state.node_raws[node_slot + 2] = -1;
    state.node_raws[node_slot + 3] = -1;
    state.node_raws[node_slot + 4] = -1;

    int first_obj = -1;
    if (!dae_node.geometry_url.empty()) {
        auto geo_it = geometries.find(dae_node.geometry_url);
        if (geo_it != geometries.end()) {
            const DaeGeometry &geo = geo_it->second;

            auto mesh_start_it = state.geometry_name_to_mesh_start.find(dae_node.geometry_url);
            int mesh_start = (mesh_start_it != state.geometry_name_to_mesh_start.end())
                                 ? mesh_start_it->second
                                 : 0;
            int mesh_count = 0;
            for (const auto &tri : geo.triangles) {
                if (tri.triangle_count > 0) {
                    mesh_count = 1;
                    break;
                }
            }

            if (mesh_count > 0) {
                int obj_name_idx = name_idx;

                int obj_slot = int(state.object_raws.size());
                state.object_raws.resize(obj_slot + 6);
                state.object_raws[obj_slot + 0] = obj_name_idx;
                state.object_raws[obj_slot + 1] = mesh_count;
                state.object_raws[obj_slot + 2] = mesh_start;
                state.object_raws[obj_slot + 3] = node_idx;
                state.object_raws[obj_slot + 4] = -1;
                state.object_raws[obj_slot + 5] = -1;

                first_obj = int(state.object_raws.size()) / 6 - 1;
            }
        }
    }

    state.node_raws[node_slot + 2] = first_obj;

    int first_child = -1;
    int prev_child = -1;
    for (const auto &child : dae_node.children) {
        int child_idx = process_node(child, node_idx, geometries, state);
        if (first_child == -1) {
            first_child = child_idx;
        }
        if (prev_child != -1) {
            state.node_raws[prev_child * 5 + 4] = child_idx;
        }
        prev_child = child_idx;
    }
    state.node_raws[node_slot + 3] = first_child;

    return node_idx;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Main DAE Reader
 * \{ */

std::unique_ptr<ShapeData> dae_read_file(const char *filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::string("Cannot read .dae file: ") + filepath);
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    std::string content = oss.str();
    return dae_read_from_bytes(
        reinterpret_cast<const uint8_t *>(content.data()), content.size());
}

std::unique_ptr<ShapeData> dae_read_from_bytes(const uint8_t *bytes, size_t size)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer(bytes, size);
    if (!result) {
        throw std::runtime_error(std::string("Failed to parse .dae XML: ") + result.description());
    }

    pugi::xml_node root = doc.document_element();
    std::string root_tag = strip_ns(root.name());
    if (root_tag != "COLLADA") {
        throw std::runtime_error("XML root is not <COLLADA>");
    }

    auto shape = std::make_unique<ShapeData>();

    /* Parse <asset> for unit meter. */
    float unit_meter = 1.0f;
    pugi::xml_node asset = child_by_tag(root, "asset");
    if (asset) {
        pugi::xml_node unit = child_by_tag(asset, "unit");
        if (unit) {
            pugi::xml_attribute meter = unit.attribute("meter");
            if (meter) {
                unit_meter = safe_atof(meter.value());
                if (unit_meter <= 0.0f) {
                    unit_meter = 1.0f;
                }
            }
        }
    }
    shape->unit_meter = unit_meter;

    /* Parse <library_materials> and link to <library_effects>. */
    std::map<std::string, int> mat_name_to_idx;
    std::map<std::string, int> mat_id_to_idx;

    /* Parse <library_effects> first: map effect ID → PBR properties. */
    struct EffectPBR {
        float base_color[4] = {0.8f, 0.8f, 0.8f, 1.0f};
        float roughness = 0.5f;
        float metallic = 0.0f;
    };
    std::map<std::string, EffectPBR> effect_pbr_map;

    pugi::xml_node eff_lib = child_by_tag(root, "library_effects");
    if (eff_lib) {
        for (pugi::xml_node eff : children_by_tag(eff_lib, "effect")) {
            pugi::xml_attribute eff_id = eff.attribute("id");
            if (!eff_id) continue;

            EffectPBR pbr;
            pugi::xml_node profile = child_by_tag(eff, "profile_COMMON");
            if (profile) {
                pugi::xml_node tech = child_by_tag(profile, "technique");
                if (tech) {
                    /* Check for lambert or phong shader. */
                    pugi::xml_node shader = child_by_tag(tech, "lambert");
                    if (!shader) shader = child_by_tag(tech, "phong");
                    if (!shader) shader = child_by_tag(tech, "blinn");

                    if (shader) {
                        /* Parse <diffuse><color> → base_color. */
                        pugi::xml_node diffuse = child_by_tag(shader, "diffuse");
                        if (diffuse) {
                            pugi::xml_node color = child_by_tag(diffuse, "color");
                            if (color) {
                                std::vector<float> c = parse_float_array_text(color.text().get());
                                if (c.size() >= 3) {
                                    pbr.base_color[0] = c[0];
                                    pbr.base_color[1] = c[1];
                                    pbr.base_color[2] = c[2];
                                    pbr.base_color[3] = (c.size() >= 4) ? c[3] : 1.0f;
                                }
                            }
                        }

                        /* Parse <reflectivity><float> → metallic. */
                        pugi::xml_node refl = child_by_tag(shader, "reflectivity");
                        if (refl) {
                            pugi::xml_node f = child_by_tag(refl, "float");
                            if (f) {
                                pbr.metallic = safe_atof(f.text().get());
                            }
                        }

                        /* Parse <shininess><float> → roughness (invert). */
                        pugi::xml_node shin = child_by_tag(shader, "shininess");
                        if (shin) {
                            pugi::xml_node f = child_by_tag(shin, "float");
                            if (f) {
                                float sh = safe_atof(f.text().get());
                                /* COLLADA shininess 0-1000 → roughness 1-0. */
                                pbr.roughness = std::clamp(1.0f - sh / 1000.0f, 0.0f, 1.0f);
                            }
                        }

                        /* Parse <transparent><color> → alpha override. */
                        pugi::xml_node trans = child_by_tag(shader, "transparent");
                        if (trans) {
                            pugi::xml_node color = child_by_tag(trans, "color");
                            if (color) {
                                std::vector<float> c = parse_float_array_text(color.text().get());
                                if (c.size() >= 4) {
                                    /* A_ONE mode: alpha = 1 - c[3]. */
                                    pugi::xml_attribute opaque = trans.attribute("opaque");
                                    if (opaque && std::string(opaque.value()) == "A_ONE") {
                                        pbr.base_color[3] = 1.0f - c[3];
                                    } else {
                                        pbr.base_color[3] = c[3];
                                    }
                                }
                            }
                        }
                    }
                }
            }

            effect_pbr_map[eff_id.value()] = pbr;
        }
    }

    pugi::xml_node mat_lib = child_by_tag(root, "library_materials");
    if (mat_lib) {
        for (pugi::xml_node mat : children_by_tag(mat_lib, "material")) {
            Material cdae_mat;
            pugi::xml_attribute name_attr = mat.attribute("name");
            pugi::xml_attribute id_attr = mat.attribute("id");
            if (name_attr) {
                cdae_mat.name = name_attr.value();
            }
            else if (id_attr) {
                cdae_mat.name = id_attr.value();
            }
            cdae_mat.flags = 3;

            /* Link material to effect for PBR properties. */
            pugi::xml_node inst_eff = child_by_tag(mat, "instance_effect");
            if (inst_eff) {
                std::string eff_url = attr_source_id(inst_eff, "url");
                auto eff_it = effect_pbr_map.find(eff_url);
                if (eff_it != effect_pbr_map.end()) {
                    const EffectPBR &pbr = eff_it->second;
                    memcpy(cdae_mat.base_color, pbr.base_color, sizeof(float) * 4);
                    cdae_mat.roughness = pbr.roughness;
                    cdae_mat.metallic = pbr.metallic;
                }
            }

            mat_name_to_idx[cdae_mat.name] = int(shape->materials.size());
            if (id_attr) {
                mat_id_to_idx[id_attr.value()] = int(shape->materials.size());
            }
            shape->materials.push_back(std::move(cdae_mat));
        }
    }

    /* Parse <library_geometries>. */
    std::map<std::string, DaeGeometry> geometries;
    pugi::xml_node geo_lib = child_by_tag(root, "library_geometries");
    if (geo_lib) {
        for (pugi::xml_node geo : children_by_tag(geo_lib, "geometry")) {
            DaeGeometry parsed = parse_geometry(geo);
            geometries[parsed.id] = std::move(parsed);
        }
    }

    /* Parse <library_visual_scenes>. */
    std::vector<DaeNode> dae_nodes;
    pugi::xml_node scene_lib = child_by_tag(root, "library_visual_scenes");
    if (scene_lib) {
        pugi::xml_node visual_scene = child_by_tag(scene_lib, "visual_scene");
        if (visual_scene) {
            for (pugi::xml_node node_elem : children_by_tag(visual_scene, "node")) {
                dae_nodes.push_back(parse_node(node_elem));
            }
        }
    }

    /* Convert geometries to meshes. */
    NodeBuildState state;

    for (auto &geo_pair : geometries) {
        const std::string &geo_id = geo_pair.first;
        const DaeGeometry &geo = geo_pair.second;

        int mesh_start = int(shape->meshes.size());
        state.geometry_name_to_mesh_start[geo_id] = mesh_start;

        bool has_valid_triangles = false;
        for (const auto &tri : geo.triangles) {
            if (tri.triangle_count > 0) {
                has_valid_triangles = true;
                break;
            }
        }

        if (has_valid_triangles) {
            Mesh cmesh = convert_geometry(geo, mat_name_to_idx, mat_id_to_idx, unit_meter);
            shape->meshes.push_back(std::move(cmesh));
        }
    }

    /* Build node/object hierarchy. */
    int prev_sibling = -1;
    for (const auto &dae_node : dae_nodes) {
        int node_idx = process_node(dae_node, -1, geometries, state);
        if (prev_sibling != -1) {
            state.node_raws[prev_sibling * 5 + 4] = node_idx;
        }
        prev_sibling = node_idx;
    }

    /* Fill names. */
    for (const auto &name : state.names) {
        shape->names.push_back(name);
    }

    /* Fill nodes. */
    int total_nodes = int(state.node_raws.size()) / 5;
    for (int i = 0; i < total_nodes; i++) {
        NodeEntry node;
        int name_idx = state.node_raws[i * 5 + 0];
        node.name = (name_idx >= 0 && name_idx < int(shape->names.size()))
                        ? shape->names[name_idx]
                        : "node";
        node.parent_index = state.node_raws[i * 5 + 1];
        node.first_object = state.node_raws[i * 5 + 2];
        node.first_child = state.node_raws[i * 5 + 3];
        node.next_sibling = state.node_raws[i * 5 + 4];

        /* Rotation: float quaternion (x, y, z, w). */
        if (i * 4 + 3 < int(state.default_rots.size())) {
            node.quaternion[0] = state.default_rots[i * 4 + 0];
            node.quaternion[1] = state.default_rots[i * 4 + 1];
            node.quaternion[2] = state.default_rots[i * 4 + 2];
            node.quaternion[3] = state.default_rots[i * 4 + 3];
        }

        /* Translation. */
        if (i * 3 + 2 < int(state.default_trans.size())) {
            node.translation[0] = state.default_trans[i * 3 + 0];
            node.translation[1] = state.default_trans[i * 3 + 1];
            node.translation[2] = state.default_trans[i * 3 + 2];
        }

        /* Scale. */
        if (i * 3 + 2 < int(state.default_scales.size())) {
            node.scale[0] = state.default_scales[i * 3 + 0];
            node.scale[1] = state.default_scales[i * 3 + 1];
            node.scale[2] = state.default_scales[i * 3 + 2];
        }

        shape->nodes.push_back(std::move(node));
    }

    /* Fill objects. */
    int total_objects = int(state.object_raws.size()) / 6;
    for (int i = 0; i < total_objects; i++) {
        ObjectEntry obj;
        obj.name_index = state.object_raws[i * 6 + 0];
        obj.num_meshes = state.object_raws[i * 6 + 1];
        obj.start_mesh = state.object_raws[i * 6 + 2];
        obj.node_index = state.object_raws[i * 6 + 3];
        obj.next_sibling = state.object_raws[i * 6 + 4];
        shape->objects.push_back(std::move(obj));
    }

    return shape;
}

/** \} */

} // namespace cdae
