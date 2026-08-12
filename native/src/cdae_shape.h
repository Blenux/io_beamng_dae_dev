/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cdae {

/* Raw binary data block. */
struct VectorBlock {
    int32_t count = 0;
    int32_t element_size = 0;
    std::vector<uint8_t> bytes;
};

enum MeshType : uint32_t {
    MESH_NULL = 4,
    MESH_STANDARD = 0,
    MESH_SKIN = 1,
    MESH_SORTED = 3,
};

/* Material definition. */
struct Material {
    std::string name;
    uint32_t flags = 3; /* S_WRAP | T_WRAP by default */
    uint32_t reflectance_map = 0;
    uint32_t bump_map = 0;
    uint32_t detail_map = 0;
    float detail_scale = 0.0f;
    float reflection_amount = 0.0f;

    /* PBR properties for Principled BSDF round-trip. */
    float base_color[4] = {0.8f, 0.8f, 0.8f, 1.0f}; /* RGBA */
    float roughness = 0.5f;
    float metallic = 0.0f;
};

/* Mesh data (supports both DAE XML and CDAE binary). */
struct Mesh {
    uint32_t mesh_type = MESH_NULL;
    bool is_dae = true;
    int32_t num_frames = 1;
    int32_t num_mat_frames = 1;
    int32_t verts_per_frame = 0;
    int32_t parent_mesh = -1;
    uint32_t mesh_flags = 0;

    VectorBlock verts;
    VectorBlock norms;
    VectorBlock tverts;       /* UV layer 0 */
    VectorBlock tverts2;      /* UV layer 1 */
    VectorBlock colors;
    VectorBlock tangents;
    VectorBlock indices;
    VectorBlock primitives;
    VectorBlock encoded_norms; /* CDAE binary: packed uint8 normals */

    /* SkinMesh extras — parsed for round-trip but unused for import. */
    VectorBlock skin_initial_verts;
    VectorBlock skin_initial_norms;
    VectorBlock skin_initial_transforms;
    VectorBlock skin_vertex_index;
    VectorBlock skin_bone_index;
    VectorBlock skin_weight;
    VectorBlock skin_node_index;

    float center[3] = {0, 0, 0};
    float bounds[6] = {0, 0, 0, 0, 0, 0};
    float radius = 0.0f;
};

/* Node hierarchy entry (for DAE XML parsed node tree). */
struct NodeEntry {
    std::string name;
    int parent_index = -1;
    int first_object = -1;
    int first_child = -1;
    int next_sibling = -1;
    float quaternion[4] = {0, 0, 0, 1}; /* x, y, z, w */
    float translation[3] = {0, 0, 0};
    float scale[3] = {1, 1, 1};
};

/* Object entry (links nodes to meshes). */
struct ObjectEntry {
    int name_index = -1;
    int num_meshes = 0;
    int start_mesh = -1;
    int node_index = -1;
    int next_sibling = -1;
};

/* Aggregated shape data (supports both DAE XML and CDAE binary). */
struct ShapeData {
    /* Shape info */
    float smallest_visible_size = 0.0f;
    int32_t smallest_visible_dl = 0;
    float radius = 0.0f;
    float tube_radius = 0.0f;
    float center[3] = {0, 0, 0};
    float bounds[6] = {0, 0, 0, 0, 0, 0};

    /* DAE XML parsed node/object tree */
    std::vector<NodeEntry> nodes;
    std::vector<ObjectEntry> objects;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<std::string> names;

    float unit_meter = 1.0f;

    /* CDAE binary raw vector blocks (used by binary import/export).
     * These store the raw Torque-style scene tree data. */
    VectorBlock raw_nodes;
    VectorBlock raw_objects;
    VectorBlock sub_shape_first_node;
    VectorBlock sub_shape_first_object;
    VectorBlock sub_shape_num_nodes;
    VectorBlock sub_shape_num_objects;
    VectorBlock default_rotations;
    VectorBlock default_translations;
    VectorBlock node_rotations;
    VectorBlock node_translations;
    VectorBlock node_uniform_scales;
    VectorBlock node_aligned_scales;
    VectorBlock node_arbitrary_scale_factors;
    VectorBlock node_arbitrary_scale_rots;
    VectorBlock ground_translations;
    VectorBlock ground_rotations;
    VectorBlock object_states;
    VectorBlock triggers;
    VectorBlock details;

    /* Sequences — opaque blob for round-trip export. */
    std::vector<uint8_t> sequences_raw;
};

} // namespace cdae
