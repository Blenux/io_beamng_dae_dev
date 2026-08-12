/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pybind11 module: cdae_native
 * Exposes DAE XML and CDAE binary parsing/writing to Python via numpy arrays. */

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <stdexcept>

#include "cdae_shape.h"
#include "dae_import.h"
#include "dae_export.h"
#include "cdae_binary_import.h"
#include "cdae_binary_export.h"

namespace py = pybind11;
using namespace cdae;

/* -------------------------------------------------------------------- */
/** \name CDAE binary raw tree → NodeEntry/ObjectEntry conversion
 * \{ */

static void convert_raw_tree(ShapeData &shape)
{
    if (shape.raw_nodes.bytes.empty() || shape.raw_objects.bytes.empty()) {
        return;
    }

    /* BLX - Validate that raw_nodes bytes are large enough for the declared count. */
    int node_count = shape.raw_nodes.count;
    if (int(shape.raw_nodes.bytes.size()) < node_count * 20) {
        node_count = int(shape.raw_nodes.bytes.size()) / 20;
    }
    const int32_t *node_ptr = reinterpret_cast<const int32_t *>(shape.raw_nodes.bytes.data());

    shape.nodes.clear();
    for (int i = 0; i < node_count; i++) {
        NodeEntry node;
        int32_t name_idx = node_ptr[i * 5 + 0];
        node.name = (name_idx >= 0 && name_idx < int(shape.names.size()))
                        ? shape.names[name_idx]
                        : "";
        node.parent_index = node_ptr[i * 5 + 1];
        node.first_object = node_ptr[i * 5 + 2];
        node.first_child = node_ptr[i * 5 + 3];
        node.next_sibling = node_ptr[i * 5 + 4];

        /* Default rotation: int16 quaternion (x, y, z, w). CDAE w is negated relative to Blender w. */
        if (!shape.default_rotations.bytes.empty() && i < shape.default_rotations.count) {
            const int16_t *rot = reinterpret_cast<const int16_t *>(shape.default_rotations.bytes.data());
            node.quaternion[0] = float(rot[i * 4 + 0]) / 32767.0f;
            node.quaternion[1] = float(rot[i * 4 + 1]) / 32767.0f;
            node.quaternion[2] = float(rot[i * 4 + 2]) / 32767.0f;
            node.quaternion[3] = -float(rot[i * 4 + 3]) / 32767.0f;
        }

        /* Default translation: float3. */
        if (!shape.default_translations.bytes.empty() && i < shape.default_translations.count) {
            const float *trans = reinterpret_cast<const float *>(shape.default_translations.bytes.data());
            node.translation[0] = trans[i * 3 + 0];
            node.translation[1] = trans[i * 3 + 1];
            node.translation[2] = trans[i * 3 + 2];
        }

        /* Default scale: node_aligned_scales (Vec3F), fallback to node_uniform_scales (float). */
        if (!shape.node_aligned_scales.bytes.empty() && i < shape.node_aligned_scales.count) {
            const float *scales = reinterpret_cast<const float *>(shape.node_aligned_scales.bytes.data());
            node.scale[0] = scales[i * 3 + 0];
            node.scale[1] = scales[i * 3 + 1];
            node.scale[2] = scales[i * 3 + 2];
        } else if (!shape.node_uniform_scales.bytes.empty() && i < shape.node_uniform_scales.count) {
            const float *us = reinterpret_cast<const float *>(shape.node_uniform_scales.bytes.data());
            node.scale[0] = us[i];
            node.scale[1] = us[i];
            node.scale[2] = us[i];
        }

        shape.nodes.push_back(std::move(node));
    }

    /* BLX - Validate that raw_objects bytes are large enough for the declared count. */
    int obj_count = shape.raw_objects.count;
    if (int(shape.raw_objects.bytes.size()) < obj_count * 24) {
        obj_count = int(shape.raw_objects.bytes.size()) / 24;
    }
    const int32_t *obj_ptr = reinterpret_cast<const int32_t *>(shape.raw_objects.bytes.data());

    shape.objects.clear();
    for (int i = 0; i < obj_count; i++) {
        ObjectEntry obj;
        obj.name_index = obj_ptr[i * 6 + 0];
        obj.num_meshes = obj_ptr[i * 6 + 1];
        obj.start_mesh = obj_ptr[i * 6 + 2];
        obj.node_index = obj_ptr[i * 6 + 3];
        obj.next_sibling = obj_ptr[i * 6 + 4];
        shape.objects.push_back(std::move(obj));
    }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name NodeEntry/ObjectEntry → CDAE binary raw tree conversion
 * \{ */

static void build_raw_tree(ShapeData &shape)
{
    int n_nodes = int(shape.nodes.size());
    int n_objs = int(shape.objects.size());

    /* Nodes: 5 x int32 per node. */
    if (n_nodes > 0) {
        shape.raw_nodes.element_size = 20;
        shape.raw_nodes.count = n_nodes;
        shape.raw_nodes.bytes.resize(n_nodes * 20);
        int32_t *ptr = reinterpret_cast<int32_t *>(shape.raw_nodes.bytes.data());
        for (int i = 0; i < n_nodes; i++) {
            /* Find name index. */
            int name_idx = -1;
            for (int j = 0; j < int(shape.names.size()); j++) {
                if (shape.names[j] == shape.nodes[i].name) {
                    name_idx = j;
                    break;
                }
            }
            ptr[i * 5 + 0] = name_idx;
            ptr[i * 5 + 1] = shape.nodes[i].parent_index;
            ptr[i * 5 + 2] = shape.nodes[i].first_object;
            ptr[i * 5 + 3] = shape.nodes[i].first_child;
            ptr[i * 5 + 4] = shape.nodes[i].next_sibling;
        }
    }

    /* Objects: 6 x int32 per object. */
    if (n_objs > 0) {
        shape.raw_objects.element_size = 24;
        shape.raw_objects.count = n_objs;
        shape.raw_objects.bytes.resize(n_objs * 24);
        int32_t *ptr = reinterpret_cast<int32_t *>(shape.raw_objects.bytes.data());
        for (int i = 0; i < n_objs; i++) {
            ptr[i * 6 + 0] = shape.objects[i].name_index;
            ptr[i * 6 + 1] = shape.objects[i].num_meshes;
            ptr[i * 6 + 2] = shape.objects[i].start_mesh;
            ptr[i * 6 + 3] = shape.objects[i].node_index;
            ptr[i * 6 + 4] = shape.objects[i].next_sibling;
            ptr[i * 6 + 5] = -1; /* first_decal */
        }
    }

    /* Default rotations: 4 x int16 per node. CDAE w is negated relative to Blender w. */
    if (n_nodes > 0) {
        shape.default_rotations.element_size = 8;
        shape.default_rotations.count = n_nodes;
        shape.default_rotations.bytes.resize(n_nodes * 8);
        int16_t *rot = reinterpret_cast<int16_t *>(shape.default_rotations.bytes.data());
        for (int i = 0; i < n_nodes; i++) {
            rot[i * 4 + 0] = int16_t(shape.nodes[i].quaternion[0] * 32767.0f);
            rot[i * 4 + 1] = int16_t(shape.nodes[i].quaternion[1] * 32767.0f);
            rot[i * 4 + 2] = int16_t(shape.nodes[i].quaternion[2] * 32767.0f);
            rot[i * 4 + 3] = int16_t(-shape.nodes[i].quaternion[3] * 32767.0f);
        }
    }

    /* Default translations: 3 x float per node. */
    if (n_nodes > 0) {
        shape.default_translations.element_size = 12;
        shape.default_translations.count = n_nodes;
        shape.default_translations.bytes.resize(n_nodes * 12);
        float *trans = reinterpret_cast<float *>(shape.default_translations.bytes.data());
        for (int i = 0; i < n_nodes; i++) {
            trans[i * 3 + 0] = shape.nodes[i].translation[0];
            trans[i * 3 + 1] = shape.nodes[i].translation[1];
            trans[i * 3 + 2] = shape.nodes[i].translation[2];
        }
    }

    /* Default aligned scales: 3 x float per node. */
    if (n_nodes > 0) {
        shape.node_aligned_scales.element_size = 12;
        shape.node_aligned_scales.count = n_nodes;
        shape.node_aligned_scales.bytes.resize(n_nodes * 12);
        float *scales = reinterpret_cast<float *>(shape.node_aligned_scales.bytes.data());
        for (int i = 0; i < n_nodes; i++) {
            scales[i * 3 + 0] = shape.nodes[i].scale[0];
            scales[i * 3 + 1] = shape.nodes[i].scale[1];
            scales[i * 3 + 2] = shape.nodes[i].scale[2];
        }
    }

    /* Subshape data: 1 subshape, root at node 0. */
    if (n_nodes > 0) {
        shape.sub_shape_first_node.element_size = 4;
        shape.sub_shape_first_node.count = 1;
        shape.sub_shape_first_node.bytes.resize(4);
        int32_t v = 0;
        memcpy(shape.sub_shape_first_node.bytes.data(), &v, 4);

        shape.sub_shape_num_nodes.element_size = 4;
        shape.sub_shape_num_nodes.count = 1;
        shape.sub_shape_num_nodes.bytes.resize(4);
        v = n_nodes;
        memcpy(shape.sub_shape_num_nodes.bytes.data(), &v, 4);
    }

    if (n_objs > 0) {
        shape.sub_shape_first_object.element_size = 4;
        shape.sub_shape_first_object.count = 1;
        shape.sub_shape_first_object.bytes.resize(4);
        int32_t v = 0;
        memcpy(shape.sub_shape_first_object.bytes.data(), &v, 4);

        shape.sub_shape_num_objects.element_size = 4;
        shape.sub_shape_num_objects.count = 1;
        shape.sub_shape_num_objects.bytes.resize(4);
        v = n_objs;
        memcpy(shape.sub_shape_num_objects.bytes.data(), &v, 4);
    }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name ShapeData → Python dict conversion
 * \{ */

static py::dict mesh_to_dict(const Mesh &mesh)
{
    py::dict result;

    result["is_dae"] = mesh.is_dae;
    result["mesh_type"] = uint32_t(mesh.mesh_type);
    result["verts_per_frame"] = mesh.verts_per_frame;

    if (mesh.mesh_type == MESH_NULL) {
        result["is_null"] = true;
        return result;
    }
    result["is_null"] = false;

    if (mesh.verts.count > 0) {
        py::array_t<float> verts({mesh.verts.count, 3});
        memcpy(verts.request().ptr, mesh.verts.bytes.data(), mesh.verts.bytes.size());
        result["vertices"] = verts;
    }

    if (mesh.norms.count > 0) {
        py::array_t<float> norms({mesh.norms.count, 3});
        memcpy(norms.request().ptr, mesh.norms.bytes.data(), mesh.norms.bytes.size());
        result["normals"] = norms;
    }

    if (mesh.tverts.count > 0) {
        py::array_t<float> uv0({mesh.tverts.count, 2});
        memcpy(uv0.request().ptr, mesh.tverts.bytes.data(), mesh.tverts.bytes.size());
        result["uv0"] = uv0;
    }

    if (mesh.tverts2.count > 0) {
        py::array_t<float> uv1({mesh.tverts2.count, 2});
        memcpy(uv1.request().ptr, mesh.tverts2.bytes.data(), mesh.tverts2.bytes.size());
        result["uv1"] = uv1;
    }

    if (mesh.colors.count > 0) {
        py::array_t<uint8_t> colors({mesh.colors.count, 4});
        memcpy(colors.request().ptr, mesh.colors.bytes.data(), mesh.colors.bytes.size());
        result["colors"] = colors;
    }

    if (mesh.tangents.count > 0) {
        py::array_t<float> tangents({mesh.tangents.count, 4});
        memcpy(tangents.request().ptr, mesh.tangents.bytes.data(), mesh.tangents.bytes.size());
        result["tangents"] = tangents;
    }

    if (mesh.indices.count > 0) {
        py::array_t<uint32_t> indices(mesh.indices.count);
        memcpy(indices.request().ptr, mesh.indices.bytes.data(), mesh.indices.bytes.size());
        result["indices"] = indices;
    }

    if (mesh.primitives.count > 0) {
        py::array_t<uint32_t> prims({mesh.primitives.count, 3});
        memcpy(prims.request().ptr, mesh.primitives.bytes.data(), mesh.primitives.bytes.size());
        result["primitives"] = prims;
    }

    /* CDAE binary: encoded norms (uint8 packed normals). */
    if (mesh.encoded_norms.count > 0) {
        py::array_t<uint8_t> enc(mesh.encoded_norms.count);
        memcpy(enc.request().ptr, mesh.encoded_norms.bytes.data(), mesh.encoded_norms.bytes.size());
        result["encoded_norms"] = enc;
    }

    return result;
}

static py::dict node_to_dict(const NodeEntry &node)
{
    py::dict result;
    result["name"] = node.name;
    result["parent_index"] = node.parent_index;
    result["first_object"] = node.first_object;
    result["first_child"] = node.first_child;
    result["next_sibling"] = node.next_sibling;

    py::array_t<float> quat({4});
    auto q = quat.mutable_data();
    q[0] = node.quaternion[0];
    q[1] = node.quaternion[1];
    q[2] = node.quaternion[2];
    q[3] = node.quaternion[3];
    result["quaternion"] = quat;

    py::array_t<float> trans({3});
    auto t = trans.mutable_data();
    t[0] = node.translation[0];
    t[1] = node.translation[1];
    t[2] = node.translation[2];
    result["translation"] = trans;

    py::array_t<float> scale({3});
    auto s = scale.mutable_data();
    s[0] = node.scale[0];
    s[1] = node.scale[1];
    s[2] = node.scale[2];
    result["scale"] = scale;

    return result;
}

static py::dict object_to_dict(const ObjectEntry &obj, const std::vector<std::string> &names)
{
    py::dict result;
    result["name_index"] = obj.name_index;
    result["name"] = (obj.name_index >= 0 && obj.name_index < int(names.size()))
                         ? names[obj.name_index]
                         : "";
    result["num_meshes"] = obj.num_meshes;
    result["start_mesh"] = obj.start_mesh;
    result["node_index"] = obj.node_index;
    result["next_sibling"] = obj.next_sibling;
    return result;
}

static py::dict shape_to_dict(const ShapeData &shape)
{
    py::dict result;

    result["unit_meter"] = shape.unit_meter;

    /* Shape info (CDAE binary). */
    result["radius"] = shape.radius;
    result["tube_radius"] = shape.tube_radius;
    py::array_t<float> center({3});
    memcpy(center.mutable_data(), shape.center, sizeof(float) * 3);
    result["center"] = center;
    py::array_t<float> bounds({6});
    memcpy(bounds.mutable_data(), shape.bounds, sizeof(float) * 6);
    result["bounds"] = bounds;

    /* Meshes. */
    py::list meshes;
    for (const auto &mesh : shape.meshes) {
        meshes.append(mesh_to_dict(mesh));
    }
    result["meshes"] = meshes;

    /* Materials. */
    py::list materials;
    for (const auto &mat : shape.materials) {
        py::dict m;
        m["name"] = mat.name;
        m["flags"] = uint32_t(mat.flags);
        /* PBR properties. */
        py::array_t<float> bc({4});
        auto bc_ptr = bc.mutable_data();
        bc_ptr[0] = mat.base_color[0];
        bc_ptr[1] = mat.base_color[1];
        bc_ptr[2] = mat.base_color[2];
        bc_ptr[3] = mat.base_color[3];
        m["base_color"] = bc;
        m["roughness"] = mat.roughness;
        m["metallic"] = mat.metallic;
        materials.append(m);
    }
    result["materials"] = materials;

    /* Nodes. */
    py::list nodes;
    for (const auto &node : shape.nodes) {
        nodes.append(node_to_dict(node));
    }
    result["nodes"] = nodes;

    /* Objects. */
    py::list objects;
    for (const auto &obj : shape.objects) {
        objects.append(object_to_dict(obj, shape.names));
    }
    result["objects"] = objects;

    /* Names. */
    py::list names;
    for (const auto &name : shape.names) {
        names.append(name);
    }
    result["names"] = names;

    return result;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Python dict → ShapeData (for export)
 * \{ */

static VectorBlock numpy_to_block(py::handle data, int elem_size)
{
    VectorBlock block;
    if (data.is_none()) {
        return block;
    }

    py::array array = py::cast<py::array>(data);
    auto buf = array.request();

    block.element_size = elem_size;
    block.count = int(buf.size * array.itemsize() / elem_size);
    block.bytes.resize(buf.size * array.itemsize());
    memcpy(block.bytes.data(), buf.ptr, block.bytes.size());

    return block;
}

static std::unique_ptr<ShapeData> dict_to_shape(py::dict data)
{
    auto shape = std::make_unique<ShapeData>();

    if (data.contains("unit_meter")) {
        shape->unit_meter = py::cast<float>(data["unit_meter"]);
    }

    /* Shape info (CDAE binary). */
    if (data.contains("radius")) {
        shape->radius = py::cast<float>(data["radius"]);
    }
    if (data.contains("tube_radius")) {
        shape->tube_radius = py::cast<float>(data["tube_radius"]);
    }
    if (data.contains("center") && !data["center"].is_none()) {
        py::array_t<float> center = data["center"].cast<py::array_t<float>>();
        auto c = center.data();
        shape->center[0] = c[0];
        shape->center[1] = c[1];
        shape->center[2] = c[2];
    }
    if (data.contains("bounds") && !data["bounds"].is_none()) {
        py::array_t<float> bounds = data["bounds"].cast<py::array_t<float>>();
        auto b = bounds.data();
        for (int i = 0; i < 6; i++) shape->bounds[i] = b[i];
    }

    /* Meshes. */
    py::list meshes = data["meshes"].cast<py::list>();
    for (auto item : meshes) {
        py::dict m = item.cast<py::dict>();
        Mesh mesh;
        mesh.is_dae = true;
        mesh.mesh_type = m.contains("is_null") && m["is_null"].cast<bool>()
                             ? MESH_NULL
                             : MESH_STANDARD;

        if (mesh.mesh_type != MESH_NULL) {
            if (m.contains("vertices") && !m["vertices"].is_none()) {
                mesh.verts = numpy_to_block(m["vertices"], 12);
            }
            if (m.contains("normals") && !m["normals"].is_none()) {
                mesh.norms = numpy_to_block(m["normals"], 12);
            }
            if (m.contains("uv0") && !m["uv0"].is_none()) {
                mesh.tverts = numpy_to_block(m["uv0"], 8);
            }
            if (m.contains("uv1") && !m["uv1"].is_none()) {
                mesh.tverts2 = numpy_to_block(m["uv1"], 8);
            }
            if (m.contains("colors") && !m["colors"].is_none()) {
                mesh.colors = numpy_to_block(m["colors"], 4);
            }
            if (m.contains("tangents") && !m["tangents"].is_none()) {
                mesh.tangents = numpy_to_block(m["tangents"], 16);
            }
            if (m.contains("indices") && !m["indices"].is_none()) {
                mesh.indices = numpy_to_block(m["indices"], 4);
            }
            if (m.contains("primitives") && !m["primitives"].is_none()) {
                mesh.primitives = numpy_to_block(m["primitives"], 12);
            }
            if (mesh.verts.count > 0) {
                mesh.verts_per_frame = mesh.verts.count;
            }
        }

        shape->meshes.push_back(std::move(mesh));
    }

    /* Materials. */
    py::list materials = data["materials"].cast<py::list>();
    for (auto item : materials) {
        py::dict m = item.cast<py::dict>();
        Material mat;
        mat.name = py::cast<std::string>(m["name"]);
        if (m.contains("flags")) {
            mat.flags = py::cast<uint32_t>(m["flags"]);
        }
        /* PBR properties. */
        if (m.contains("base_color") && !m["base_color"].is_none()) {
            py::array_t<float> bc = m["base_color"].cast<py::array_t<float>>();
            auto bc_ptr = bc.data();
            mat.base_color[0] = bc_ptr[0];
            mat.base_color[1] = bc_ptr[1];
            mat.base_color[2] = bc_ptr[2];
            mat.base_color[3] = bc_ptr[3];
        }
        if (m.contains("roughness")) {
            mat.roughness = py::cast<float>(m["roughness"]);
        }
        if (m.contains("metallic")) {
            mat.metallic = py::cast<float>(m["metallic"]);
        }
        shape->materials.push_back(std::move(mat));
    }

    /* Nodes. */
    if (data.contains("nodes") && !data["nodes"].is_none()) {
        py::list nodes = data["nodes"].cast<py::list>();
        for (auto item : nodes) {
            py::dict n = item.cast<py::dict>();
            NodeEntry node;
            node.name = py::cast<std::string>(n["name"]);
            node.parent_index = py::cast<int>(n["parent_index"]);
            node.first_object = py::cast<int>(n["first_object"]);
            node.first_child = py::cast<int>(n["first_child"]);
            node.next_sibling = py::cast<int>(n["next_sibling"]);

            if (n.contains("quaternion") && !n["quaternion"].is_none()) {
                py::array_t<float> quat = n["quaternion"].cast<py::array_t<float>>();
                auto q = quat.data();
                node.quaternion[0] = q[0];
                node.quaternion[1] = q[1];
                node.quaternion[2] = q[2];
                node.quaternion[3] = q[3];
            }
            if (n.contains("translation") && !n["translation"].is_none()) {
                py::array_t<float> trans = n["translation"].cast<py::array_t<float>>();
                auto t = trans.data();
                node.translation[0] = t[0];
                node.translation[1] = t[1];
                node.translation[2] = t[2];
            }
            if (n.contains("scale") && !n["scale"].is_none()) {
                py::array_t<float> scale = n["scale"].cast<py::array_t<float>>();
                auto s = scale.data();
                node.scale[0] = s[0];
                node.scale[1] = s[1];
                node.scale[2] = s[2];
            }

            shape->nodes.push_back(std::move(node));
        }
    }

    /* Objects. */
    if (data.contains("objects") && !data["objects"].is_none()) {
        py::list objects = data["objects"].cast<py::list>();
        for (auto item : objects) {
            py::dict o = item.cast<py::dict>();
            ObjectEntry obj;
            obj.name_index = py::cast<int>(o["name_index"]);
            obj.num_meshes = py::cast<int>(o["num_meshes"]);
            obj.start_mesh = py::cast<int>(o["start_mesh"]);
            obj.node_index = py::cast<int>(o["node_index"]);
            obj.next_sibling = py::cast<int>(o["next_sibling"]);
            shape->objects.push_back(std::move(obj));
        }
    }

    /* Names. */
    if (data.contains("names") && !data["names"].is_none()) {
        py::list names = data["names"].cast<py::list>();
        for (auto item : names) {
            shape->names.push_back(py::cast<std::string>(item));
        }
    }

    return shape;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Module Definition
 * \{ */

PYBIND11_MODULE(cdae_native, m) {
    m.doc() = "C++ DAE (Collada) XML and CDAE binary parser/writer for Blender addons";

    /* --- DAE XML --- */

    m.def("parse_dae", [](const std::string &filepath) {
        auto shape = dae_read_file(filepath.c_str());
        return shape_to_dict(*shape);
    }, py::arg("filepath"),
       "Parse a .dae file and return mesh/material/node data as Python dicts with numpy arrays");

    m.def("parse_dae_bytes", [](py::bytes data) {
        std::string s = data;
        auto shape = dae_read_from_bytes(
            reinterpret_cast<const uint8_t *>(s.data()), s.size());
        return shape_to_dict(*shape);
    }, py::arg("data"),
       "Parse .dae from in-memory bytes and return mesh/material/node data");

    m.def("write_dae", [](const std::string &filepath, py::dict data) {
        auto shape = dict_to_shape(data);
        return dae_write_file(*shape, filepath.c_str());
    }, py::arg("filepath"), py::arg("data"),
       "Write mesh/material/node data to a .dae file");

    /* --- CDAE Binary --- */

    m.def("parse_cdae", [](const std::string &filepath) {
        auto shape = parse_cdae(filepath.c_str());
        convert_raw_tree(*shape);
        return shape_to_dict(*shape);
    }, py::arg("filepath"),
       "Parse a .cdae binary file and return mesh/material/node data as Python dicts with numpy arrays");

    m.def("parse_cdae_bytes", [](py::bytes data) {
        std::string s = data;
        auto shape = parse_cdae_bytes(
            reinterpret_cast<const uint8_t *>(s.data()), s.size());
        convert_raw_tree(*shape);
        return shape_to_dict(*shape);
    }, py::arg("data"),
       "Parse .cdae binary from in-memory bytes and return mesh/material/node data");

    m.def("write_cdae", [](const std::string &filepath, py::dict data, bool compress) {
        auto shape = dict_to_shape(data);
        build_raw_tree(*shape);
        return write_cdae(filepath.c_str(), *shape, compress);
    }, py::arg("filepath"), py::arg("data"), py::arg("compress") = false,
       "Write mesh/material/node data to a .cdae binary file");

    /* Version info. */
    m.attr("__version__") = "2.0.0";
}

/** \} */
