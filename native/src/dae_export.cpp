/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "dae_export.h"
#include "cdae_shape.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/* Custom hash for float array dedup — avoids string key allocations. */
struct FloatArrayHash {
    int stride;
    size_t operator()(const float *p) const noexcept {
        uint64_t h = 0xcbf29ce484222325ULL;
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(p);
        size_t n = static_cast<size_t>(stride) * sizeof(float);
        for (size_t i = 0; i < n; i++) {
            h ^= bytes[i];
            h *= 0x100000001b3ULL;
        }
        return h;
    }
};

struct FloatArrayEq {
    int stride;
    bool operator()(const float *a, const float *b) const noexcept {
        return std::memcmp(a, b, static_cast<size_t>(stride) * sizeof(float)) == 0;
    }
};

#include "pugixml.hpp"

namespace cdae {

static std::string sanitize_id(const std::string &s)
{
    std::string out = s;
    for (auto &c : out) { if (c == '.') c = '_'; }
    return out;
}

/* -------------------------------------------------------------------- */
/** \name Formatting Helpers
 * \{ */

static std::string fmt_float(float v)
{
    /* Trim trailing zeros for compact output. */
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", v);
    return std::string(buf);
}

/* Lower precision for normals (unit vectors, ~0.001% precision). */
static std::string fmt_float_nor(float v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.5g", v);
    return std::string(buf);
}

static std::string fmt_floats(const float *data, int count)
{
    std::string result;
    result.reserve(count * 12);
    char buf[32];
    for (int i = 0; i < count; i++) {
        if (i > 0) result += ' ';
        snprintf(buf, sizeof(buf), "%.6g", data[i]);
        result += buf;
    }
    return result;
}

/* Compact normal formatting using reduced precision. */
static std::string fmt_floats_nor(const float *data, int count)
{
    std::string result;
    result.reserve(count * 10);
    char buf[32];
    for (int i = 0; i < count; i++) {
        if (i > 0) result += ' ';
        snprintf(buf, sizeof(buf), "%.5g", data[i]);
        result += buf;
    }
    return result;
}

static std::string fmt_uints(const uint32_t *data, int count)
{
    std::string result;
    result.reserve(count * 12);
    char buf[16];
    for (int i = 0; i < count; i++) {
        if (i > 0) result += ' ';
        snprintf(buf, sizeof(buf), "%u", data[i]);
        result += buf;
    }
    return result;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Quaternion → Collada Matrix
 * \{ */

static void quat_trans_scale_to_collada_matrix(float out[16],
                                                const float q[4],
                                                const float trans[3],
                                                const float scale[3])
{
    float mat[4][4];
    /* Quaternion to rotation matrix. */
    float xx = q[0] * q[0], yy = q[1] * q[1], zz = q[2] * q[2];
    float xy = q[0] * q[1], xz = q[0] * q[2], yz = q[1] * q[2];
    float wx = q[3] * q[0], wy = q[3] * q[1], wz = q[3] * q[2];

    /* Apply scale to rotation matrix rows. */
    mat[0][0] = (1.0f - 2.0f * (yy + zz)) * scale[0];
    mat[0][1] = 2.0f * (xy + wz) * scale[0];
    mat[0][2] = 2.0f * (xz - wy) * scale[0];
    mat[0][3] = 0.0f;
    mat[1][0] = 2.0f * (xy - wz) * scale[1];
    mat[1][1] = (1.0f - 2.0f * (xx + zz)) * scale[1];
    mat[1][2] = 2.0f * (yz + wx) * scale[1];
    mat[1][3] = 0.0f;
    mat[2][0] = 2.0f * (xz + wy) * scale[2];
    mat[2][1] = 2.0f * (yz - wx) * scale[2];
    mat[2][2] = (1.0f - 2.0f * (xx + yy)) * scale[2];
    mat[2][3] = 0.0f;
    mat[3][0] = trans[0];
    mat[3][1] = trans[1];
    mat[3][2] = trans[2];
    mat[3][3] = 1.0f;

    /* Transpose to Collada column-major order. */
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            out[col * 4 + row] = mat[row][col];
        }
    }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Source Writing
 * \{ */

static const char *pn1[] = {"X"};
static const char *pn2[] = {"X", "Y"};
static const char *pn3[] = {"X", "Y", "Z"};
static const char *pn4[] = {"X", "Y", "Z", "W"};

static const char *uv_params[] = {"S", "T"};
static const char *color_params[] = {"R", "G", "B", "A"};

static const char **get_pn(int s)
{
    switch (s) {
        case 1: return pn1;
        case 2: return pn2;
        case 3: return pn3;
        default: return pn4;
    }
}

static void write_source(pugi::xml_node &parent,
                          const std::string &sid,
                          const float *data,
                          int count,
                          int stride,
                          bool is_normal = false,
                          const char **param_names = nullptr)
{
    pugi::xml_node src = parent.append_child("source");
    src.append_attribute("id") = sid.c_str();

    std::string aid = sid + "_array";
    int total = count * stride;

    pugi::xml_node fa = src.append_child("float_array");
    fa.append_attribute("id") = aid.c_str();
    fa.append_attribute("count") = std::to_string(total).c_str();
    fa.text() = (is_normal ? fmt_floats_nor(data, total) : fmt_floats(data, total)).c_str();

    pugi::xml_node tc = src.append_child("technique_common");
    pugi::xml_node acc = tc.append_child("accessor");
    acc.append_attribute("source") = ("#" + aid).c_str();
    acc.append_attribute("count") = std::to_string(count).c_str();
    acc.append_attribute("stride") = std::to_string(stride).c_str();

    const char **params = param_names ? param_names : get_pn(stride);
    for (int i = 0; i < stride; i++) {
        pugi::xml_node p = acc.append_child("param");
        p.append_attribute("name") = params[i];
        p.append_attribute("type") = "float";
    }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Geometry Writing
 * \{ */

static std::vector<std::string> write_geometry(pugi::xml_node &lib_geoms,
                                                const Mesh &mesh,
                                                int mesh_idx,
                                                const std::string &mesh_name,
                                                const std::vector<Material> &shape_materials)
{
    std::vector<std::string> mat_names;
    std::string gid = sanitize_id(mesh_name) + "-mesh";

    pugi::xml_node geom = lib_geoms.append_child("geometry");
    geom.append_attribute("id") = gid.c_str();
    geom.append_attribute("name") = mesh_name.c_str();
    pugi::xml_node me = geom.append_child("mesh");

    /* Empty meshes: write minimal empty sources and vertices to match original DAE. */
    if (mesh.mesh_type == MESH_NULL) {
        std::string pos_id = gid + "-positions";
        std::string norm_id = gid + "-normals";
        std::string uv0_id = gid + "-map-0";
        std::string uv1_id = gid + "-map-1";
        std::string vid = gid + "-vertices";

        /* Empty positions source. */
        pugi::xml_node pos_src = me.append_child("source");
        pos_src.append_attribute("id") = pos_id.c_str();
        pugi::xml_node pos_arr = pos_src.append_child("float_array");
        pos_arr.append_attribute("id") = (pos_id + "-array").c_str();
        pos_arr.append_attribute("count") = "0";
        pugi::xml_node pos_tc = pos_src.append_child("technique_common");
        pugi::xml_node pos_acc = pos_tc.append_child("accessor");
        pos_acc.append_attribute("source") = ("#" + pos_id + "-array").c_str();
        pos_acc.append_attribute("count") = "0";
        pos_acc.append_attribute("stride") = "3";
        pugi::xml_node px = pos_acc.append_child("param");
        px.append_attribute("name") = "X"; px.append_attribute("type") = "float";
        pugi::xml_node py = pos_acc.append_child("param");
        py.append_attribute("name") = "Y"; py.append_attribute("type") = "float";
        pugi::xml_node pz = pos_acc.append_child("param");
        pz.append_attribute("name") = "Z"; pz.append_attribute("type") = "float";

        /* Empty normals source. */
        pugi::xml_node nor_src = me.append_child("source");
        nor_src.append_attribute("id") = norm_id.c_str();
        pugi::xml_node nor_arr = nor_src.append_child("float_array");
        nor_arr.append_attribute("id") = (norm_id + "-array").c_str();
        nor_arr.append_attribute("count") = "0";
        pugi::xml_node nor_tc = nor_src.append_child("technique_common");
        pugi::xml_node nor_acc = nor_tc.append_child("accessor");
        nor_acc.append_attribute("source") = ("#" + norm_id + "-array").c_str();
        nor_acc.append_attribute("count") = "0";
        nor_acc.append_attribute("stride") = "3";
        pugi::xml_node nx = nor_acc.append_child("param");
        nx.append_attribute("name") = "X"; nx.append_attribute("type") = "float";
        pugi::xml_node ny = nor_acc.append_child("param");
        ny.append_attribute("name") = "Y"; ny.append_attribute("type") = "float";
        pugi::xml_node nz = nor_acc.append_child("param");
        nz.append_attribute("name") = "Z"; nz.append_attribute("type") = "float";

        /* Empty UV0 source. */
        pugi::xml_node uv0_src = me.append_child("source");
        uv0_src.append_attribute("id") = uv0_id.c_str();
        pugi::xml_node uv0_arr = uv0_src.append_child("float_array");
        uv0_arr.append_attribute("id") = (uv0_id + "-array").c_str();
        uv0_arr.append_attribute("count") = "0";
        pugi::xml_node uv0_tc = uv0_src.append_child("technique_common");
        pugi::xml_node uv0_acc = uv0_tc.append_child("accessor");
        uv0_acc.append_attribute("source") = ("#" + uv0_id + "-array").c_str();
        uv0_acc.append_attribute("count") = "0";
        uv0_acc.append_attribute("stride") = "2";
        pugi::xml_node us = uv0_acc.append_child("param");
        us.append_attribute("name") = "S"; us.append_attribute("type") = "float";
        pugi::xml_node ut = uv0_acc.append_child("param");
        ut.append_attribute("name") = "T"; ut.append_attribute("type") = "float";

        /* Empty UV1 source. */
        pugi::xml_node uv1_src = me.append_child("source");
        uv1_src.append_attribute("id") = uv1_id.c_str();
        pugi::xml_node uv1_arr = uv1_src.append_child("float_array");
        uv1_arr.append_attribute("id") = (uv1_id + "-array").c_str();
        uv1_arr.append_attribute("count") = "0";
        pugi::xml_node uv1_tc = uv1_src.append_child("technique_common");
        pugi::xml_node uv1_acc = uv1_tc.append_child("accessor");
        uv1_acc.append_attribute("source") = ("#" + uv1_id + "-array").c_str();
        uv1_acc.append_attribute("count") = "0";
        uv1_acc.append_attribute("stride") = "2";
        pugi::xml_node us2 = uv1_acc.append_child("param");
        us2.append_attribute("name") = "S"; us2.append_attribute("type") = "float";
        pugi::xml_node ut2 = uv1_acc.append_child("param");
        ut2.append_attribute("name") = "T"; ut2.append_attribute("type") = "float";

        /* Vertices element. */
        pugi::xml_node verts = me.append_child("vertices");
        verts.append_attribute("id") = vid.c_str();
        pugi::xml_node vi = verts.append_child("input");
        vi.append_attribute("semantic") = "POSITION";
        vi.append_attribute("source") = ("#" + pos_id).c_str();

        /* If this MESH_NULL has line data (lines-only mesh), write position source
         * with actual vertex data and a <lines> element. */
        if (mesh.line_indices.count > 0 && mesh.line_indices.element_size == 8 &&
            mesh.line_verts.count > 0 && mesh.line_verts.element_size == 12) {
            const float *lv = reinterpret_cast<const float *>(mesh.line_verts.bytes.data());
            int lv_count = mesh.line_verts.count;
            const int32_t *li = reinterpret_cast<const int32_t *>(mesh.line_indices.bytes.data());
            int line_count = mesh.line_indices.count;

            /* Find and update the position source's float_array and accessor. */
            for (pugi::xml_node src = me.first_child(); src; src = src.next_sibling()) {
                if (std::string(src.name()) == "source" &&
                    std::string(src.attribute("id").value()) == pos_id) {
                    for (pugi::xml_node c = src.first_child(); c; c = c.next_sibling()) {
                        if (std::string(c.name()) == "float_array") {
                            c.text() = fmt_floats(lv, lv_count * 3).c_str();
                            c.attribute("count") = std::to_string(lv_count * 3).c_str();
                        } else if (std::string(c.name()) == "technique_common") {
                            for (pugi::xml_node tc = c.first_child(); tc; tc = tc.next_sibling()) {
                                if (std::string(tc.name()) == "accessor") {
                                    tc.attribute("count") = std::to_string(lv_count).c_str();
                                }
                            }
                        }
                    }
                    break;
                }
            }

            /* Write <lines> element. */
            pugi::xml_node lines = me.append_child("lines");
            lines.append_attribute("count") = std::to_string(line_count).c_str();
            pugi::xml_node lin = lines.append_child("input");
            lin.append_attribute("semantic") = "VERTEX";
            lin.append_attribute("source") = ("#" + vid).c_str();
            lin.append_attribute("offset") = "0";
            pugi::xml_node lp = lines.append_child("p");
            lp.text() = fmt_uints(reinterpret_cast<const uint32_t *>(li), line_count * 2).c_str();
        }

        return mat_names;
    }

    std::string pos_id, norm_id, uv0_id, uv1_id, color_id;
    std::vector<std::string> extra_uv_ids; /* Source IDs for extra UV layers */
    std::vector<std::string> color_layer_ids;
    bool has_norms = false, has_uv0 = false, has_uv1 = false, has_colors = false;
    bool has_color_layers = false;

    if (mesh.verts.count > 0 && mesh.verts.element_size == 12) {
        has_norms = (mesh.norms.count > 0 && mesh.norms.element_size == 12);
        has_uv0 = (mesh.tverts.count > 0 && mesh.tverts.element_size == 8);
        has_uv1 = (mesh.tverts2.count > 0 && mesh.tverts2.element_size == 8);
        has_colors = (mesh.colors.count > 0 && mesh.colors.element_size == 4);
        has_color_layers = !mesh.color_layers.empty();
        if (has_color_layers) has_colors = true;

        const float *raw_pos = reinterpret_cast<const float *>(mesh.verts.bytes.data());
        const float *raw_nor = has_norms ? reinterpret_cast<const float *>(mesh.norms.bytes.data()) : nullptr;
        const float *raw_uv0 = has_uv0 ? reinterpret_cast<const float *>(mesh.tverts.bytes.data()) : nullptr;
        const float *raw_uv1 = has_uv1 ? reinterpret_cast<const float *>(mesh.tverts2.bytes.data()) : nullptr;
        const uint8_t *raw_col = has_colors ? mesh.colors.bytes.data() : nullptr;
        int corner_count = mesh.verts.count;

        /* Pre-process UVs (flip V) and colors (uint8→float). */
        std::vector<float> proc_uv0, proc_uv1, proc_color;
        std::vector<std::vector<float>> proc_color_layers;
        std::vector<std::vector<float>> proc_uv_extra; /* Extra UV layers (2+) */
        int num_extra_uvs = static_cast<int>(mesh.tverts_extra.size());
        if (has_uv0) {
            proc_uv0.resize(corner_count * 2);
            for (int i = 0; i < corner_count; i++) {
                proc_uv0[i * 2] = raw_uv0[i * 2];
                proc_uv0[i * 2 + 1] = 1.0f - raw_uv0[i * 2 + 1];
            }
        }
        if (has_uv1) {
            proc_uv1.resize(corner_count * 2);
            for (int i = 0; i < corner_count; i++) {
                proc_uv1[i * 2] = raw_uv1[i * 2];
                proc_uv1[i * 2 + 1] = 1.0f - raw_uv1[i * 2 + 1];
            }
        }
        /* Pre-process extra UV layers (V-flip). */
        for (int ei = 0; ei < num_extra_uvs; ei++) {
            const auto &eb = mesh.tverts_extra[ei];
            if (eb.count > 0 && eb.element_size == 8) {
                const float *raw = reinterpret_cast<const float *>(eb.bytes.data());
                std::vector<float> proc(corner_count * 2);
                for (int i = 0; i < corner_count; i++) {
                    proc[i * 2] = raw[i * 2];
                    proc[i * 2 + 1] = 1.0f - raw[i * 2 + 1];
                }
                proc_uv_extra.push_back(std::move(proc));
            } else {
                proc_uv_extra.emplace_back();
            }
        }
        if (has_colors && !has_color_layers) {
            proc_color.resize(corner_count * 4);
            for (int i = 0; i < corner_count * 4; i++) {
                proc_color[i] = float(raw_col[i]) / 255.0f;
            }
        }
        if (has_color_layers) {
            proc_color_layers.resize(mesh.color_layers.size());
            for (size_t li = 0; li < mesh.color_layers.size(); li++) {
                const auto &lb = mesh.color_layers[li];
                if (lb.count > 0 && lb.element_size == 4) {
                    const uint8_t *ld = lb.bytes.data();
                    proc_color_layers[li].resize(corner_count * 4);
                    for (int i = 0; i < corner_count * 4; i++) {
                        proc_color_layers[li][i] = float(ld[i]) / 255.0f;
                    }
                }
            }
        }

        /* Multi-offset dedup: each attribute stream gets its own index buffer. */
        auto dedup_float = [&](const float *data, int stride) -> std::pair<std::vector<float>, std::vector<uint32_t>> {
            FloatArrayHash hasher{stride};
            FloatArrayEq eq{stride};
            std::unordered_map<const float*, uint32_t, FloatArrayHash, FloatArrayEq> map(
                corner_count, hasher, eq);
            std::vector<float> uniq;
            uniq.reserve(corner_count * stride / 2);
            std::vector<uint32_t> idx(corner_count);
            for (int i = 0; i < corner_count; i++) {
                const float *p = &data[i * stride];
                auto it = map.find(p);
                if (it != map.end()) {
                    idx[i] = it->second;
                } else {
                    uint32_t ni = static_cast<uint32_t>(uniq.size() / stride);
                    map[p] = ni;
                    idx[i] = ni;
                    for (int s = 0; s < stride; s++) uniq.push_back(p[s]);
                }
            }
            return {uniq, idx};
        };

        auto [uniq_pos, pos_idx] = dedup_float(raw_pos, 3);
        std::vector<float> uniq_nor; std::vector<uint32_t> nor_idx;
        if (has_norms) { auto [u, i] = dedup_float(raw_nor, 3); uniq_nor = std::move(u); nor_idx = std::move(i); }

        /* Process line (loose edge) data: map line vertex indices to deduplicated
         * position indices and add line-only vertices to uniq_pos. */
        std::vector<uint32_t> line_dedup_idx; /* mapped line indices (pairs) */
        if (mesh.line_indices.count > 0 && mesh.line_indices.element_size == 8 &&
            mesh.line_verts.count > 0 && mesh.line_verts.element_size == 12) {
            const int32_t *li = reinterpret_cast<const int32_t *>(mesh.line_indices.bytes.data());
            int line_count = mesh.line_indices.count;
            const float *lv = reinterpret_cast<const float *>(mesh.line_verts.bytes.data());
            int lv_count = mesh.line_verts.count;

            /* Build position hash for existing dedup vertices for fast lookup. */
            FloatArrayHash pos_hasher{3};
            FloatArrayEq pos_eq{3};
            std::unordered_map<const float*, uint32_t, FloatArrayHash, FloatArrayEq> pos_map(
                uniq_pos.size() / 3, pos_hasher, pos_eq);
            for (size_t i = 0; i < uniq_pos.size(); i += 3) {
                pos_map[&uniq_pos[i]] = static_cast<uint32_t>(i / 3);
            }

            /* Map each Blender vertex index used by lines to a dedup position index. */
            std::unordered_map<int32_t, uint32_t> bv_to_dedup;
            for (int i = 0; i < line_count * 2; i++) {
                int32_t bvi = li[i];
                if (bv_to_dedup.find(bvi) != bv_to_dedup.end()) continue;
                if (bvi < 0 || bvi >= lv_count) {
                    bv_to_dedup[bvi] = 0;
                    continue;
                }
                const float *p = &lv[bvi * 3];
                auto it = pos_map.find(p);
                if (it != pos_map.end()) {
                    bv_to_dedup[bvi] = it->second;
                } else {
                    /* Line-only vertex: add to uniq_pos. */
                    uint32_t ni = static_cast<uint32_t>(uniq_pos.size() / 3);
                    pos_map[p] = ni;
                    uniq_pos.push_back(p[0]);
                    uniq_pos.push_back(p[1]);
                    uniq_pos.push_back(p[2]);
                    bv_to_dedup[bvi] = ni;
                }
            }

            /* Build deduplicated line index pairs. */
            line_dedup_idx.resize(line_count * 2);
            for (int i = 0; i < line_count * 2; i++) {
                line_dedup_idx[i] = bv_to_dedup[li[i]];
            }
        }

        /* Dedup UV layers. All UV sets share one index offset (offset=2 in the DAE).
         * Combine all UV layers into one stream for dedup, then split back. */
        std::vector<float> uniq_uv0; std::vector<uint32_t> uv0_idx;
        std::vector<float> uniq_uv1; std::vector<uint32_t> uv1_idx;
        std::vector<std::vector<float>> uniq_uv_extra; /* deduped extra UV layers */
        std::vector<uint32_t> uv_idx; /* shared index for all UV sets */

        /* Count total UV layers present. */
        int total_uv_layers = (has_uv0 ? 1 : 0) + (has_uv1 ? 1 : 0);
        for (int ei = 0; ei < num_extra_uvs; ei++) {
            if (!proc_uv_extra[ei].empty()) total_uv_layers++;
        }

        if (total_uv_layers >= 2) {
            /* Combine all UV layers into a single stream for dedup. */
            int combined_stride = total_uv_layers * 2;
            std::vector<float> combined_uv(corner_count * combined_stride);
            int offset = 0;
            if (has_uv0) {
                for (int i = 0; i < corner_count; i++) {
                    combined_uv[i * combined_stride + offset]     = proc_uv0[i * 2];
                    combined_uv[i * combined_stride + offset + 1] = proc_uv0[i * 2 + 1];
                }
                offset += 2;
            }
            if (has_uv1) {
                for (int i = 0; i < corner_count; i++) {
                    combined_uv[i * combined_stride + offset]     = proc_uv1[i * 2];
                    combined_uv[i * combined_stride + offset + 1] = proc_uv1[i * 2 + 1];
                }
                offset += 2;
            }
            for (int ei = 0; ei < num_extra_uvs; ei++) {
                if (!proc_uv_extra[ei].empty()) {
                    for (int i = 0; i < corner_count; i++) {
                        combined_uv[i * combined_stride + offset]     = proc_uv_extra[ei][i * 2];
                        combined_uv[i * combined_stride + offset + 1] = proc_uv_extra[ei][i * 2 + 1];
                    }
                    offset += 2;
                }
            }
            auto [u, i] = dedup_float(combined_uv.data(), combined_stride);
            uv_idx = std::move(i);
            /* Split back into separate UV arrays. */
            int src_off = 0;
            if (has_uv0) {
                uniq_uv0.reserve(u.size() / combined_stride * 2);
                for (size_t j = 0; j < u.size(); j += combined_stride) {
                    uniq_uv0.push_back(u[j + src_off]);
                    uniq_uv0.push_back(u[j + src_off + 1]);
                }
                src_off += 2;
            }
            if (has_uv1) {
                uniq_uv1.reserve(u.size() / combined_stride * 2);
                for (size_t j = 0; j < u.size(); j += combined_stride) {
                    uniq_uv1.push_back(u[j + src_off]);
                    uniq_uv1.push_back(u[j + src_off + 1]);
                }
                src_off += 2;
            }
            for (int ei = 0; ei < num_extra_uvs; ei++) {
                if (!proc_uv_extra[ei].empty()) {
                    std::vector<float> ue;
                    ue.reserve(u.size() / combined_stride * 2);
                    for (size_t j = 0; j < u.size(); j += combined_stride) {
                        ue.push_back(u[j + src_off]);
                        ue.push_back(u[j + src_off + 1]);
                    }
                    uniq_uv_extra.push_back(std::move(ue));
                    src_off += 2;
                }
            }
        } else if (has_uv0) {
            auto [u, i] = dedup_float(proc_uv0.data(), 2);
            uniq_uv0 = std::move(u); uv_idx = std::move(i);
        } else if (has_uv1) {
            auto [u, i] = dedup_float(proc_uv1.data(), 2);
            uniq_uv1 = std::move(u); uv_idx = std::move(i);
        }

        std::vector<float> uniq_color; std::vector<uint32_t> color_idx;
        /* Dedup all color layers together as a combined tuple (they share offset=3). */
        std::vector<std::vector<float>> uniq_color_layers;
        if (has_color_layers) {
            int nlayers = static_cast<int>(proc_color_layers.size());
            int combined_stride = nlayers * 4;
            std::vector<float> combined_colors(corner_count * combined_stride);
            for (int i = 0; i < corner_count; i++) {
                for (int li = 0; li < nlayers; li++) {
                    for (int c = 0; c < 4; c++) {
                        combined_colors[i * combined_stride + li * 4 + c] = proc_color_layers[li][i * 4 + c];
                    }
                }
            }
            auto [u, i] = dedup_float(combined_colors.data(), combined_stride);
            color_idx = std::move(i);
            /* Split back into per-layer arrays. */
            uniq_color_layers.resize(nlayers);
            for (int li = 0; li < nlayers; li++) {
                uniq_color_layers[li].reserve(u.size() / nlayers);
                for (size_t j = 0; j < u.size(); j += combined_stride) {
                    for (int c = 0; c < 4; c++) {
                        uniq_color_layers[li].push_back(u[j + li * 4 + c]);
                    }
                }
            }
        } else if (has_colors) {
            auto [u, i] = dedup_float(proc_color.data(), 4); uniq_color = std::move(u); color_idx = std::move(i);
        }

        /* Write sources using deduplicated data. */
        pos_id = gid + "-positions";
        write_source(me, pos_id, uniq_pos.data(), uniq_pos.size() / 3, 3);

        if (has_norms) {
            norm_id = gid + "-normals";
            write_source(me, norm_id, uniq_nor.data(), uniq_nor.size() / 3, 3, true);
        }
        if (has_uv0) {
            uv0_id = gid + "-map-0";
            write_source(me, uv0_id, uniq_uv0.data(), uniq_uv0.size() / 2, 2, false, uv_params);
        }
        if (has_uv1) {
            uv1_id = gid + "-map-1";
            write_source(me, uv1_id, uniq_uv1.data(), uniq_uv1.size() / 2, 2, false, uv_params);
        }
        /* Write extra UV sources (map-2, map-3, ...). */
        for (size_t ei = 0; ei < uniq_uv_extra.size(); ei++) {
            std::string uv_id = gid + "-map-" + std::to_string(ei + 2);
            write_source(me, uv_id, uniq_uv_extra[ei].data(),
                         uniq_uv_extra[ei].size() / 2, 2, false, uv_params);
            extra_uv_ids.push_back(uv_id);
        }
        if (has_colors && !has_color_layers) {
            color_id = gid + "-colors";
            write_source(me, color_id, uniq_color.data(), uniq_color.size() / 4, 4, false, color_params);
        }
        if (has_color_layers) {
            color_layer_ids.resize(uniq_color_layers.size());
            for (size_t li = 0; li < uniq_color_layers.size(); li++) {
                std::string lname;
                if (li < mesh.color_layer_names.size() && !mesh.color_layer_names[li].empty()) {
                    lname = mesh.color_layer_names[li];
                } else {
                    lname = "Col";
                    if (li > 0) lname += std::to_string(li);
                }
                color_layer_ids[li] = gid + "-colors-" + lname;
                write_source(me, color_layer_ids[li],
                             uniq_color_layers[li].data(),
                             uniq_color_layers[li].size() / 4, 4, false, color_params);
            }
        }

        /* <vertices> element. */
        std::string vid = gid + "-vertices";
        pugi::xml_node verts = me.append_child("vertices");
        verts.append_attribute("id") = vid.c_str();
        pugi::xml_node vi = verts.append_child("input");
        vi.append_attribute("semantic") = "POSITION";
        vi.append_attribute("source") = ("#" + pos_id).c_str();

        /* <triangles> per draw region. */
        if (mesh.primitives.count > 0 && mesh.indices.count > 0) {
            const uint32_t *prims = reinterpret_cast<const uint32_t *>(mesh.primitives.bytes.data());
            const uint32_t *idx = reinterpret_cast<const uint32_t *>(mesh.indices.bytes.data());

            /* Offset scheme matches Blender's Collada exporter:
             * VERTEX=0, NORMAL=1, TEXCOORD=2 (all UV sets share), COLOR=3 (all color layers share). */
            bool has_uv = has_uv0 || has_uv1 || !uniq_uv_extra.empty();
            int nor_off = 1;
            int uv_off = has_uv ? 2 : -1;
            int col_off = has_colors ? (has_uv ? 3 : 2) : -1;
            int nstreams = 1 + (has_norms ? 1 : 0) + (has_uv ? 1 : 0) + (has_colors ? 1 : 0);

            for (int i = 0; i < mesh.primitives.count; i++) {
                uint32_t istart = prims[i * 3];
                uint32_t icount = prims[i * 3 + 1];
                uint32_t info = prims[i * 3 + 2];
                uint32_t mi = info & 0x0FFFFFFF;

                std::string mn;
                if (mi < shape_materials.size()) {
                    mn = sanitize_id(shape_materials[mi].name) + "-material";
                } else {
                    mn = "mat_" + std::to_string(mi);
                }
                bool found = false;
                for (const auto &n : mat_names) {
                    if (n == mn) { found = true; break; }
                }
                if (!found) mat_names.push_back(mn);

                pugi::xml_node tris = me.append_child("triangles");
                tris.append_attribute("material") = mn.c_str();
                tris.append_attribute("count") = std::to_string(icount / 3).c_str();

                auto add_input = [&](const char *sem, const std::string &ref, int off, const char *set = nullptr) {
                    pugi::xml_node inp = tris.append_child("input");
                    inp.append_attribute("semantic") = sem;
                    inp.append_attribute("source") = ref.c_str();
                    inp.append_attribute("offset") = std::to_string(off).c_str();
                    if (set) inp.append_attribute("set") = set;
                };

                add_input("VERTEX", "#" + vid, 0);
                if (has_norms) add_input("NORMAL", "#" + norm_id, nor_off);
                if (has_uv0) add_input("TEXCOORD", "#" + uv0_id, uv_off, "0");
                if (has_uv1) add_input("TEXCOORD", "#" + uv1_id, uv_off, "1");
                for (size_t ei = 0; ei < extra_uv_ids.size(); ei++) {
                    std::string set_str = std::to_string(ei + 2);
                    add_input("TEXCOORD", "#" + extra_uv_ids[ei], uv_off, set_str.c_str());
                }
                if (has_color_layers) {
                    for (size_t li = 0; li < color_layer_ids.size(); li++) {
                        std::string set_str = std::to_string(li);
                        add_input("COLOR", "#" + color_layer_ids[li], col_off,
                                  set_str.c_str());
                    }
                } else if (has_colors) {
                    add_input("COLOR", "#" + color_id, col_off);
                }

                /* Build interleaved <p> with per-stream indices; reverse winding (2,1,0)→(0,1,2). */
                int tc = icount / 3;
                std::vector<uint32_t> p_idx(icount * nstreams);
                for (int t = 0; t < tc; t++) {
                    uint32_t b = istart + t * 3;
                    /* CDAE order: (b, b+1, b+2) → DAE order: (b+2, b+1, b) */
                    uint32_t corners[3] = {idx[b + 2], idx[b + 1], idx[b]};
                    for (int c = 0; c < 3; c++) {
                        uint32_t ci = corners[c];
                        int base = (t * 3 + c) * nstreams;
                        p_idx[base + 0] = pos_idx[ci];
                        if (has_norms) p_idx[base + nor_off] = nor_idx[ci];
                        if (has_uv) p_idx[base + uv_off] = uv_idx[ci];
                        if (has_colors) p_idx[base + col_off] = color_idx[ci];
                    }
                }
                pugi::xml_node p = tris.append_child("p");
                p.text() = fmt_uints(p_idx.data(), icount * nstreams).c_str();
            }
        }

        /* Write <lines> element for loose edges (from <lines> round-trip). */
        if (!line_dedup_idx.empty()) {
            int line_count = static_cast<int>(line_dedup_idx.size()) / 2;
            pugi::xml_node lines = me.append_child("lines");
            lines.append_attribute("count") = std::to_string(line_count).c_str();
            pugi::xml_node li = lines.append_child("input");
            li.append_attribute("semantic") = "VERTEX";
            li.append_attribute("source") = ("#" + vid).c_str();
            li.append_attribute("offset") = "0";
            pugi::xml_node lp = lines.append_child("p");
            lp.text() = fmt_uints(line_dedup_idx.data(), line_count * 2).c_str();
        }

        return mat_names;
    }

    return mat_names;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Node Tree Writing
 * \{ */

static std::string get_mesh_name(const ShapeData &shape, int mesh_idx)
{
    if (!shape.objects.empty()) {
        for (const auto &obj : shape.objects) {
            int start = obj.start_mesh;
            int num = obj.num_meshes;
            if (mesh_idx >= start && mesh_idx < start + num) {
                int ni = obj.name_index;
                if (ni >= 0 && ni < int(shape.names.size())) {
                    return shape.names[ni];
                }
            }
        }
    }
    return "mesh_" + std::to_string(mesh_idx);
}

static std::string get_geometry_name(const ShapeData &shape, int mesh_idx)
{
    if (mesh_idx >= 0 && mesh_idx < int(shape.meshes.size())) {
        if (!shape.meshes[mesh_idx].geometry_name.empty()) {
            return shape.meshes[mesh_idx].geometry_name;
        }
    }
    return get_mesh_name(shape, mesh_idx);
}

static void write_node_recursive(pugi::xml_node &parent,
                                  const ShapeData &shape,
                                  int node_idx,
                                  const std::vector<std::vector<std::string>> &mesh_mat_names)
{
    const NodeEntry &node = shape.nodes[node_idx];

    pugi::xml_node xml_node = parent.append_child("node");
    xml_node.append_attribute("id") = sanitize_id(node.name).c_str();
    xml_node.append_attribute("name") = node.name.c_str();
    xml_node.append_attribute("type") = "NODE";

    /* Write <matrix sid="transform"> with rotation, translation, and scale. */
    float q[4] = {node.quaternion[0], node.quaternion[1], node.quaternion[2], node.quaternion[3]};
    float trans[3] = {node.translation[0], node.translation[1], node.translation[2]};
    float scale[3] = {node.scale[0], node.scale[1], node.scale[2]};
    float mat[16];
    quat_trans_scale_to_collada_matrix(mat, q, trans, scale);
    pugi::xml_node mx = xml_node.append_child("matrix");
    mx.append_attribute("sid") = "transform";
    mx.text() = fmt_floats(mat, 16).c_str();

    /* Process objects (follow first_object → next_sibling chain). */
    int oi = node.first_object;
    while (oi >= 0 && oi < int(shape.objects.size())) {
        const ObjectEntry &obj = shape.objects[oi];

        std::string obj_name = (obj.name_index >= 0 && obj.name_index < int(shape.names.size()))
                                    ? shape.names[obj.name_index]
                                    : "object";

        pugi::xml_node xml_obj;
        if (obj_name != node.name) {
            xml_obj = xml_node.append_child("node");
            xml_obj.append_attribute("id") = sanitize_id(obj_name).c_str();
            xml_obj.append_attribute("name") = obj_name.c_str();
            xml_obj.append_attribute("type") = "NODE";
        }
        else {
            xml_obj = xml_node;
        }

        for (int m = 0; m < obj.num_meshes; m++) {
            int mi = obj.start_mesh + m;
            std::string geo_name = get_geometry_name(shape, mi);
            std::string mesh_id = sanitize_id(geo_name) + "-mesh";
            pugi::xml_node ig = xml_obj.append_child("instance_geometry");
            ig.append_attribute("url") = ("#" + mesh_id).c_str();
            ig.append_attribute("name") = geo_name.c_str();

            pugi::xml_node bm = ig.append_child("bind_material");
            pugi::xml_node tc = bm.append_child("technique_common");

            if (mi < int(mesh_mat_names.size())) {
                for (const auto &mn : mesh_mat_names[mi]) {
                    pugi::xml_node imat = tc.append_child("instance_material");
                    imat.append_attribute("symbol") = mn.c_str();
                    imat.append_attribute("target") = ("#" + mn).c_str();
                    /* Add bind_vertex_input for each UV layer using preserved names.
                     * Only write BVI for meshes that actually have UV data. */
                    const Mesh *mesh_ptr = (mi >= 0 && mi < int(shape.meshes.size()))
                                               ? &shape.meshes[mi] : nullptr;
                    if (mesh_ptr && mesh_ptr->mesh_type != MESH_NULL) {
                        bool has_any_uv = (mesh_ptr->tverts.count > 0) ||
                                          (mesh_ptr->tverts2.count > 0) ||
                                          !mesh_ptr->tverts_extra.empty();
                        if (has_any_uv) {
                            if (!mesh_ptr->tvert_names.empty()) {
                                for (size_t ui = 0; ui < mesh_ptr->tvert_names.size(); ui++) {
                                    pugi::xml_node bvi = imat.append_child("bind_vertex_input");
                                    bvi.append_attribute("semantic") = mesh_ptr->tvert_names[ui].c_str();
                                    bvi.append_attribute("input_semantic") = "TEXCOORD";
                                    bvi.append_attribute("input_set") = std::to_string(ui).c_str();
                                }
                            } else {
                                /* Fallback: single UVMap entry. */
                                pugi::xml_node bvi = imat.append_child("bind_vertex_input");
                                bvi.append_attribute("semantic") = "UVMap";
                                bvi.append_attribute("input_semantic") = "TEXCOORD";
                                bvi.append_attribute("input_set") = "0";
                            }
                        }
                    }
                }
            }
        }

        oi = obj.next_sibling;
    }

    /* Process child nodes. */
    int ci = node.first_child;
    while (ci >= 0 && ci < int(shape.nodes.size())) {
        write_node_recursive(xml_node, shape, ci, mesh_mat_names);
        ci = shape.nodes[ci].next_sibling;
    }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Main DAE Writer
 * \{ */

bool dae_write_file(const ShapeData &shape, const char *filepath,
                    const std::string &authoring_tool)
{
    pugi::xml_document doc;

    pugi::xml_node collada = doc.append_child("COLLADA");
    /* Attribute order matches Blender DAE output. */
    collada.append_attribute("xmlns") = "http://www.collada.org/2005/11/COLLADASchema";
    collada.append_attribute("version") = "1.4.1";
    collada.append_attribute("xmlns:xsi") = "http://www.w3.org/2001/XMLSchema-instance";

    /* <asset>. */
    {
        pugi::xml_node asset = collada.append_child("asset");
        pugi::xml_node contrib = asset.append_child("contributor");
        contrib.append_child("author").text() = "Blender User";
        contrib.append_child("authoring_tool").text() = authoring_tool.c_str();
        time_t now = std::time(nullptr);
        struct tm tm_buf;
#if defined(_WIN32)
        gmtime_s(&tm_buf, &now);
#else
        gmtime_r(&now, &tm_buf);
#endif
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_buf);
        asset.append_child("created").text() = ts;
        asset.append_child("modified").text() = ts;
        pugi::xml_node unit = asset.append_child("unit");
        unit.append_attribute("name") = "meter";
        unit.append_attribute("meter") = "1";
        asset.append_child("up_axis").text() = "Z_UP";
    }

    /* Library order matches Blender DAE output. */
    pugi::xml_node lib_effs = collada.append_child("library_effects");
    pugi::xml_node lib_imgs = collada.append_child("library_images");
    pugi::xml_node lib_mats = collada.append_child("library_materials");
    pugi::xml_node lib_geoms = collada.append_child("library_geometries");
    pugi::xml_node lib_scenes = collada.append_child("library_visual_scenes");

    pugi::xml_node scene = collada.append_child("scene");
    pugi::xml_node ivs = scene.append_child("instance_visual_scene");
    ivs.append_attribute("url") = "#Scene";

    pugi::xml_node vs = lib_scenes.append_child("visual_scene");
    vs.append_attribute("id") = "Scene";
    vs.append_attribute("name") = "Scene";

    /* Materials with PBR effects (diffuse color, reflectivity, shininess). */
    for (size_t i = 0; i < shape.materials.size(); i++) {
        std::string mid = sanitize_id(shape.materials[i].name) + "-material";
        std::string eid = sanitize_id(shape.materials[i].name) + "-effect";
        pugi::xml_node mat = lib_mats.append_child("material");
        mat.append_attribute("id") = mid.c_str();
        mat.append_attribute("name") = shape.materials[i].name.c_str();
        pugi::xml_node ie = mat.append_child("instance_effect");
        ie.append_attribute("url") = ("#" + eid).c_str();
        pugi::xml_node eff = lib_effs.append_child("effect");
        eff.append_attribute("id") = eid.c_str();
        pugi::xml_node profile = eff.append_child("profile_COMMON");
        pugi::xml_node tech = profile.append_child("technique");
        tech.append_attribute("sid") = "common";
        pugi::xml_node lambert = tech.append_child("lambert");

        /* Emission. */
        pugi::xml_node emission = lambert.append_child("emission");
        pugi::xml_node em_color = emission.append_child("color");
        em_color.append_attribute("sid") = "emission";
        em_color.text() = "0 0 0 1";

        /* Diffuse → base_color. */
        const auto &m = shape.materials[i];
        pugi::xml_node diffuse = lambert.append_child("diffuse");
        pugi::xml_node diff_color = diffuse.append_child("color");
        diff_color.append_attribute("sid") = "diffuse";
        {
            /* When alpha < 1.0, write diffuse alpha=1.0 and use <transparent> A_ONE
             * to match the original DAE structure (importer converts transparent A_ONE
             * to base_color[3] = 1 - transparent.alpha). */
            float diff_alpha = (m.base_color[3] < 1.0f) ? 1.0f : m.base_color[3];
            std::string cs = fmt_float(m.base_color[0]) + " " +
                             fmt_float(m.base_color[1]) + " " +
                             fmt_float(m.base_color[2]) + " " +
                             fmt_float(diff_alpha);
            diff_color.text() = cs.c_str();
        }

        /* Transparent → alpha (A_ONE mode: alpha = 1 - base_color[3]). */
        if (m.base_color[3] < 1.0f) {
            pugi::xml_node trans = lambert.append_child("transparent");
            trans.append_attribute("opaque") = "A_ONE";
            pugi::xml_node trans_color = trans.append_child("color");
            trans_color.append_attribute("sid") = "transparent";
            float trans_alpha = 1.0f - m.base_color[3];
            std::string ts = "0 0 0 " + fmt_float(trans_alpha);
            trans_color.text() = ts.c_str();
        }

        /* Reflectivity → metallic (only if original had it). */
        if (m.has_reflectivity) {
            pugi::xml_node refl = lambert.append_child("reflectivity");
            pugi::xml_node refl_f = refl.append_child("float");
            refl_f.append_attribute("sid") = "specular";
            refl_f.text() = fmt_float(m.metallic).c_str();
        }

        /* Shininess → roughness (invert: roughness 0-1 → shininess 1000-0).
         * Only write if original had shininess. */
        if (m.has_shininess) {
            pugi::xml_node shin = lambert.append_child("shininess");
            pugi::xml_node shin_f = shin.append_child("float");
            shin_f.append_attribute("sid") = "shininess";
            shin_f.text() = fmt_float(std::clamp(1.0f - m.roughness, 0.0f, 1.0f) * 1000.0f).c_str();
        }

        /* Index of refraction. */
        pugi::xml_node ior = lambert.append_child("index_of_refraction");
        pugi::xml_node ior_f = ior.append_child("float");
        ior_f.append_attribute("sid") = "ior";
        ior_f.text() = fmt_float(m.ior).c_str();
    }

    /* Geometries. */
    std::vector<std::vector<std::string>> mesh_mat_names;
    for (size_t i = 0; i < shape.meshes.size(); i++) {
        std::string mname = get_geometry_name(shape, int(i));
        std::vector<std::string> mn = write_geometry(lib_geoms, shape.meshes[i], int(i), mname, shape.materials);
        mesh_mat_names.push_back(std::move(mn));
    }

    /* Node tree — find root nodes (parent == -1) and recurse. */
    for (size_t i = 0; i < shape.nodes.size(); i++) {
        if (shape.nodes[i].parent_index == -1) {
            write_node_recursive(vs, shape, int(i), mesh_mat_names);
        }
    }

    /* Streaming writer: replaces " />" with "/>" during serialization and writes directly
     * to file, avoiding building a large string in memory. */
    unsigned int flags = pugi::format_default;

    class FileStreamWriter : public pugi::xml_writer {
    public:
        std::ofstream &ofs;
        std::string pending; /* small buffer to detect " />" across write calls */
        bool first_write = true;
        bool need_encoding_fix = true;

        explicit FileStreamWriter(std::ofstream &o) : ofs(o) { pending.reserve(8); }

        void write(const void *data, size_t size) override {
            const char *p = static_cast<const char *>(data);

            /* Fix XML declaration encoding on first write. */
            if (first_write) {
                first_write = false;
                if (size >= 21 && std::strncmp(p, "<?xml version=\"1.0\"?>", 21) == 0) {
                    static const char decl[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
                    ofs.write(decl, sizeof(decl) - 1);
                    p += 21;
                    size -= 21;
                }
            }

            /* Append to pending buffer, then flush, keeping last 2 chars for cross-boundary detection. */
            pending.append(p, size);

            /* Replace " />" with "/>" in the pending buffer. */
            size_t pos = 0;
            while ((pos = pending.find(" />", pos)) != std::string::npos) {
                pending.replace(pos, 3, "/>");
                pos += 2;
            }

            /* Write all but last 2 chars (in case " /" spans a boundary). */
            if (pending.size() > 2) {
                size_t write_len = pending.size() - 2;
                ofs.write(pending.data(), static_cast<std::streamsize>(write_len));
                pending = pending.substr(write_len);
            }
        }

        void flush() {
            if (!pending.empty()) {
                ofs.write(pending.data(), static_cast<std::streamsize>(pending.size()));
                pending.clear();
            }
        }
    };

    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) {
        throw std::runtime_error(std::string("Failed to open .dae file for writing: ") + filepath);
    }

    FileStreamWriter writer(ofs);
    doc.save(writer, "  ", flags, pugi::encoding_utf8);
    writer.flush();

    if (!ofs) {
        throw std::runtime_error(std::string("Failed to write .dae file: ") + filepath);
    }

    return true;
}

/** \} */

} // namespace cdae
