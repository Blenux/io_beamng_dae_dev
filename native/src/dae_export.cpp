/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "dae_export.h"
#include "cdae_shape.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pugixml.hpp"

namespace cdae {

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
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            result += ' ';
        }
        result += fmt_float(data[i]);
    }
    return result;
}

/* Compact normal formatting using reduced precision. */
static std::string fmt_floats_nor(const float *data, int count)
{
    std::string result;
    result.reserve(count * 10);
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            result += ' ';
        }
        result += fmt_float_nor(data[i]);
    }
    return result;
}

static std::string fmt_uints(const uint32_t *data, int count)
{
    std::string result;
    result.reserve(count * 12);
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            result += ' ';
        }
        result += std::to_string(data[i]);
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
                          bool is_normal = false)
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

    const char **params = get_pn(stride);
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
                                                const std::string &mesh_name)
{
    std::vector<std::string> mat_names;
    std::string gid = "mesh_" + std::to_string(mesh_idx);

    pugi::xml_node geom = lib_geoms.append_child("geometry");
    geom.append_attribute("id") = gid.c_str();
    geom.append_attribute("name") = mesh_name.c_str();
    pugi::xml_node me = geom.append_child("mesh");

    std::string pos_id, norm_id, uv0_id, uv1_id, color_id;
    bool has_norms = false, has_uv0 = false, has_uv1 = false, has_colors = false;

    if (mesh.verts.count > 0 && mesh.verts.element_size == 12) {
        has_norms = (mesh.norms.count > 0 && mesh.norms.element_size == 12);
        has_uv0 = (mesh.tverts.count > 0 && mesh.tverts.element_size == 8);
        has_uv1 = (mesh.tverts2.count > 0 && mesh.tverts2.element_size == 8);
        has_colors = (mesh.colors.count > 0 && mesh.colors.element_size == 4);

        const float *raw_pos = reinterpret_cast<const float *>(mesh.verts.bytes.data());
        const float *raw_nor = has_norms ? reinterpret_cast<const float *>(mesh.norms.bytes.data()) : nullptr;
        const float *raw_uv0 = has_uv0 ? reinterpret_cast<const float *>(mesh.tverts.bytes.data()) : nullptr;
        const float *raw_uv1 = has_uv1 ? reinterpret_cast<const float *>(mesh.tverts2.bytes.data()) : nullptr;
        const uint8_t *raw_col = has_colors ? mesh.colors.bytes.data() : nullptr;
        int corner_count = mesh.verts.count;

        /* Pre-process UVs (flip V) and colors (uint8→float). */
        std::vector<float> proc_uv0, proc_uv1, proc_color;
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
        if (has_colors) {
            proc_color.resize(corner_count * 4);
            for (int i = 0; i < corner_count * 4; i++) {
                proc_color[i] = float(raw_col[i]) / 255.0f;
            }
        }

        /* Multi-offset dedup: each attribute stream gets its own index buffer. */
        auto dedup_float = [&](const float *data, int stride) -> std::pair<std::vector<float>, std::vector<uint32_t>> {
            /* BLX - Use unordered_map for O(1) average lookup vs O(log n) for std::map. */
            std::unordered_map<std::string, uint32_t> map;
            map.reserve(corner_count);
            std::vector<float> uniq;
            std::vector<uint32_t> idx(corner_count);
            for (int i = 0; i < corner_count; i++) {
                std::string key(reinterpret_cast<const char *>(&data[i * stride]), stride * sizeof(float));
                auto it = map.find(key);
                if (it != map.end()) {
                    idx[i] = it->second;
                } else {
                    uint32_t ni = static_cast<uint32_t>(map.size());
                    map[key] = ni;
                    idx[i] = ni;
                    for (int s = 0; s < stride; s++) uniq.push_back(data[i * stride + s]);
                }
            }
            return {uniq, idx};
        };

        auto [uniq_pos, pos_idx] = dedup_float(raw_pos, 3);
        std::vector<float> uniq_nor; std::vector<uint32_t> nor_idx;
        if (has_norms) { auto [u, i] = dedup_float(raw_nor, 3); uniq_nor = std::move(u); nor_idx = std::move(i); }

        std::vector<float> uniq_uv0; std::vector<uint32_t> uv0_idx;
        if (has_uv0) { auto [u, i] = dedup_float(proc_uv0.data(), 2); uniq_uv0 = std::move(u); uv0_idx = std::move(i); }

        std::vector<float> uniq_uv1; std::vector<uint32_t> uv1_idx;
        if (has_uv1) { auto [u, i] = dedup_float(proc_uv1.data(), 2); uniq_uv1 = std::move(u); uv1_idx = std::move(i); }

        std::vector<float> uniq_color; std::vector<uint32_t> color_idx;
        if (has_colors) { auto [u, i] = dedup_float(proc_color.data(), 4); uniq_color = std::move(u); color_idx = std::move(i); }

        /* Write sources using deduplicated data. */
        pos_id = gid + "_position";
        write_source(me, pos_id, uniq_pos.data(), uniq_pos.size() / 3, 3);

        if (has_norms) {
            norm_id = gid + "_normals";
            write_source(me, norm_id, uniq_nor.data(), uniq_nor.size() / 3, 3, true);
        }
        if (has_uv0) {
            uv0_id = gid + "_uv0s";
            write_source(me, uv0_id, uniq_uv0.data(), uniq_uv0.size() / 2, 2);
        }
        if (has_uv1) {
            uv1_id = gid + "_uv1s";
            write_source(me, uv1_id, uniq_uv1.data(), uniq_uv1.size() / 2, 2);
        }
        if (has_colors) {
            color_id = gid + "_colors";
            write_source(me, color_id, uniq_color.data(), uniq_color.size() / 4, 4);
        }

        /* <vertices> element. */
        std::string vid = gid + "_vertices";
        pugi::xml_node verts = me.append_child("vertices");
        verts.append_attribute("id") = vid.c_str();
        pugi::xml_node vi = verts.append_child("input");
        vi.append_attribute("semantic") = "POSITION";
        vi.append_attribute("source") = ("#" + pos_id).c_str();

        /* <triangles> per draw region. */
        if (mesh.primitives.count > 0 && mesh.indices.count > 0) {
            const uint32_t *prims = reinterpret_cast<const uint32_t *>(mesh.primitives.bytes.data());
            const uint32_t *idx = reinterpret_cast<const uint32_t *>(mesh.indices.bytes.data());

            /* Assign offsets: each attribute stream gets its own offset. */
            int next_offset = 1;
            int nor_off = has_norms ? next_offset++ : -1;
            int uv0_off = has_uv0 ? next_offset++ : -1;
            int uv1_off = has_uv1 ? next_offset++ : -1;
            int col_off = has_colors ? next_offset++ : -1;
            int nstreams = next_offset; /* total index streams per corner */

            for (int i = 0; i < mesh.primitives.count; i++) {
                uint32_t istart = prims[i * 3];
                uint32_t icount = prims[i * 3 + 1];
                uint32_t info = prims[i * 3 + 2];
                uint32_t mi = info & 0x0FFFFFFF;

                std::string mn = "mat_" + std::to_string(mi);
                bool found = false;
                for (const auto &n : mat_names) {
                    if (n == mn) { found = true; break; }
                }
                if (!found) mat_names.push_back(mn);

                pugi::xml_node tris = me.append_child("triangles");
                tris.append_attribute("count") = std::to_string(icount / 3).c_str();
                tris.append_attribute("material") = mn.c_str();

                auto add_input = [&](const char *sem, const std::string &ref, int off, const char *set = nullptr) {
                    pugi::xml_node inp = tris.append_child("input");
                    inp.append_attribute("semantic") = sem;
                    inp.append_attribute("source") = ref.c_str();
                    inp.append_attribute("offset") = std::to_string(off).c_str();
                    if (set) inp.append_attribute("set") = set;
                };

                add_input("VERTEX", "#" + vid, 0);
                if (has_norms) add_input("NORMAL", "#" + norm_id, nor_off);
                if (has_uv0) add_input("TEXCOORD", "#" + uv0_id, uv0_off, "0");
                if (has_uv1) add_input("TEXCOORD", "#" + uv1_id, uv1_off, "1");
                if (has_colors) add_input("COLOR", "#" + color_id, col_off);

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
                        if (has_uv0) p_idx[base + uv0_off] = uv0_idx[ci];
                        if (has_uv1) p_idx[base + uv1_off] = uv1_idx[ci];
                        if (has_colors) p_idx[base + col_off] = color_idx[ci];
                    }
                }
                pugi::xml_node p = tris.append_child("p");
                p.text() = fmt_uints(p_idx.data(), icount * nstreams).c_str();
            }
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

static void write_node_recursive(pugi::xml_node &parent,
                                  const ShapeData &shape,
                                  int node_idx,
                                  const std::vector<std::vector<std::string>> &mesh_mat_names)
{
    const NodeEntry &node = shape.nodes[node_idx];

    pugi::xml_node xml_node = parent.append_child("node");
    xml_node.append_attribute("id") = node.name.c_str();
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
            xml_obj.append_attribute("id") = obj_name.c_str();
            xml_obj.append_attribute("name") = obj_name.c_str();
            xml_obj.append_attribute("type") = "NODE";
        }
        else {
            xml_obj = xml_node;
        }

        for (int m = 0; m < obj.num_meshes; m++) {
            int mi = obj.start_mesh + m;
            pugi::xml_node ig = xml_obj.append_child("instance_geometry");
            ig.append_attribute("url") = ("#mesh_" + std::to_string(mi)).c_str();

            pugi::xml_node bm = ig.append_child("bind_material");
            pugi::xml_node tc = bm.append_child("technique_common");

            if (mi < int(mesh_mat_names.size())) {
                for (const auto &mn : mesh_mat_names[mi]) {
                    pugi::xml_node imat = tc.append_child("instance_material");
                    imat.append_attribute("symbol") = mn.c_str();
                    imat.append_attribute("target") = ("#" + mn).c_str();
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

bool dae_write_file(const ShapeData &shape, const char *filepath)
{
    pugi::xml_document doc;

    pugi::xml_node collada = doc.append_child("COLLADA");
    collada.append_attribute("version") = "1.4.1";
    collada.append_attribute("xmlns") = "http://www.collada.org/2005/11/COLLADASchema";

    /* <asset>. */
    {
        pugi::xml_node asset = collada.append_child("asset");
        pugi::xml_node contrib = asset.append_child("contributor");
        contrib.append_child("authoring_tool").text() = "BeamNGer";
        time_t now = std::time(nullptr);
        struct tm tm_buf;
        /* BLX - Use thread-safe gmtime_r (POSIX) or gmtime_s (Windows). */
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

    pugi::xml_node lib_geoms = collada.append_child("library_geometries");
    pugi::xml_node lib_mats = collada.append_child("library_materials");
    pugi::xml_node lib_effs = collada.append_child("library_effects");
    pugi::xml_node lib_scenes = collada.append_child("library_visual_scenes");

    pugi::xml_node scene = collada.append_child("scene");
    pugi::xml_node ivs = scene.append_child("instance_visual_scene");
    ivs.append_attribute("url") = "#Scene";

    pugi::xml_node vs = lib_scenes.append_child("visual_scene");
    vs.append_attribute("id") = "Scene";
    vs.append_attribute("name") = "Scene";

    /* Materials with PBR effects (diffuse color, reflectivity, shininess). */
    for (size_t i = 0; i < shape.materials.size(); i++) {
        std::string mid = "mat_" + std::to_string(i);
        std::string eid = mid + "_fx";
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
            std::string cs = fmt_float(m.base_color[0]) + " " +
                             fmt_float(m.base_color[1]) + " " +
                             fmt_float(m.base_color[2]) + " " +
                             fmt_float(m.base_color[3]);
            diff_color.text() = cs.c_str();
        }

        /* Reflectivity → metallic. */
        pugi::xml_node refl = lambert.append_child("reflectivity");
        pugi::xml_node refl_f = refl.append_child("float");
        refl_f.append_attribute("sid") = "specular";
        refl_f.text() = fmt_float(m.metallic).c_str();

        /* Shininess → roughness (invert: roughness 0-1 → shininess 1000-0). */
        pugi::xml_node shin = lambert.append_child("shininess");
        pugi::xml_node shin_f = shin.append_child("float");
        shin_f.append_attribute("sid") = "shininess";
        shin_f.text() = fmt_float(std::clamp(1.0f - m.roughness, 0.0f, 1.0f) * 1000.0f).c_str();

        /* Index of refraction. */
        pugi::xml_node ior = lambert.append_child("index_of_refraction");
        pugi::xml_node ior_f = ior.append_child("float");
        ior_f.append_attribute("sid") = "ior";
        ior_f.text() = "1.45";
    }

    /* Geometries. */
    std::vector<std::vector<std::string>> mesh_mat_names;
    for (size_t i = 0; i < shape.meshes.size(); i++) {
        std::string mname = get_mesh_name(shape, int(i));
        std::vector<std::string> mn = write_geometry(lib_geoms, shape.meshes[i], int(i), mname);
        mesh_mat_names.push_back(std::move(mn));
    }

    /* Node tree — find root nodes (parent == -1) and recurse. */
    for (size_t i = 0; i < shape.nodes.size(); i++) {
        if (shape.nodes[i].parent_index == -1) {
            write_node_recursive(vs, shape, int(i), mesh_mat_names);
        }
    }

    /* Save file. */
    /* Use format_raw (no indentation) for compact file size. */
    unsigned int flags = pugi::format_raw;
    if (!doc.save_file(filepath, "  ", flags)) {
        throw std::runtime_error(std::string("Failed to write .dae file: ") + filepath);
    }

    return true;
}

/** \} */

} // namespace cdae
