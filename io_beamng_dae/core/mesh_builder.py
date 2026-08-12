"""
Slim mesh builder — creates Blender mesh objects from parsed DAE/CDAE data.
Vectorized with numpy foreach_get/foreach_set for speed.
Handles: vertices, normals, UVs, colors, materials, sharp edges/faces, node transforms.
"""

import bpy
import os
import numpy as np
from array import array
from mathutils import Matrix, Quaternion, Vector


class MeshBuilder:
    def __init__(self, context, materials, global_scale=1.0,
                 custom_normals=True, is_dae=True, filepath="",
                 use_collection=True):
        self.context = context
        self.materials = materials
        self.global_scale = global_scale
        self.custom_normals = custom_normals
        self.is_dae = is_dae
        self.filepath = filepath
        self.use_collection = use_collection

    def build_meshes(self, data):
        """Build Blender mesh objects from parsed data."""
        objects = {}
        meshes_data = data.get("meshes", [])
        objects_data = data.get("objects", [])
        names = data.get("names", [])

        # BLX - Use a named collection when enabled, otherwise the scene's active collection
        col = self._get_import_collection() if self.use_collection else self.context.collection

        for mesh_idx, mesh_dict in enumerate(meshes_data):
            if mesh_dict.get("is_null", False):
                continue

            obj_name = self._get_mesh_name(mesh_idx, objects_data, names)
            mesh = self._create_mesh(obj_name, mesh_dict)

            if mesh is None:
                continue

            obj = bpy.data.objects.new(obj_name, mesh)
            col.objects.link(obj)
            self._assign_materials(mesh, mesh_dict)
            objects[mesh_idx] = obj

        return objects

    def build_node_hierarchy(self, data, mesh_objects):
        """Build node hierarchy and apply transforms."""
        nodes = data.get("nodes", [])
        objects_data = data.get("objects", [])

        if not nodes:
            return

        mesh_to_node = {}
        for obj_entry in objects_data:
            node_idx = obj_entry.get("node_index", -1)
            start = obj_entry.get("start_mesh", -1)
            count = obj_entry.get("num_meshes", 0)
            for m in range(count):
                mi = start + m
                if mi not in mesh_to_node:
                    mesh_to_node[mi] = node_idx

        world_matrices = self._compute_world_matrices(nodes)

        for mesh_idx, obj in mesh_objects.items():
            node_idx = mesh_to_node.get(mesh_idx, -1)
            if node_idx >= 0 and node_idx < len(world_matrices):
                obj.matrix_world = world_matrices[node_idx]

        for node_idx, node in enumerate(nodes):
            parent_idx = node.get("parent_index", -1)
            if parent_idx < 0:
                continue
            for obj_entry in objects_data:
                if obj_entry.get("node_index") != node_idx:
                    continue
                start = obj_entry.get("start_mesh", -1)
                count = obj_entry.get("num_meshes", 0)
                for m in range(count):
                    mi = start + m
                    if mi in mesh_objects:
                        child_obj = mesh_objects[mi]
                        parent_obj = self._find_parent_object(parent_idx, objects_data, mesh_objects)
                        if parent_obj and parent_obj != child_obj:
                            child_obj.parent = parent_obj
                            child_obj.matrix_parent_inverse = parent_obj.matrix_world.inverted()

    def _create_mesh(self, name, mesh_dict):
        """Create a Blender Mesh from parsed mesh data — fully vectorized."""
        vertices = mesh_dict.get("vertices")
        if vertices is None:
            return None

        verts_np = np.asarray(vertices, dtype=np.float32)
        vert_count = verts_np.shape[0]
        if vert_count == 0:
            return None

        indices = mesh_dict.get("indices")
        primitives = mesh_dict.get("primitives")
        normals = mesh_dict.get("normals")
        uv0 = mesh_dict.get("uv0")
        uv1 = mesh_dict.get("uv1")
        colors = mesh_dict.get("colors")

        # Build triangle list from primitives or indices
        if primitives is not None and indices is not None:
            prims_np = np.asarray(primitives, dtype=np.uint32)
            idx_np = np.asarray(indices, dtype=np.uint32)
            # BLX - Vectorized: build (v0, v1, v2, mat) per triangle, reversed winding (2,1,0)
            # Extract primitive fields as arrays
            istarts = prims_np[:, 0].astype(np.int64)
            icounts = prims_np[:, 1].astype(np.int64)
            infos = prims_np[:, 2]
            mats = (infos & 0x0FFFFFFF).astype(np.int32)

            # Build per-primitive triangle ranges
            tri_counts = (icounts // 3).astype(np.int64)  # triangles per primitive
            total_tris = int(tri_counts.sum())
            if total_tris == 0:
                return None

            # Build flat index arrays for all triangles
            # For each primitive p with n triangles: indices istart+0, istart+1, ..., istart+3*n-1
            idx_counts = tri_counts * 3  # indices per primitive
            tri_offsets = np.repeat(istarts, idx_counts)  # one offset per index
            tri_local = np.concatenate([np.arange(n, dtype=np.int64) for n in idx_counts])
            flat_idx = idx_np[tri_offsets + tri_local].astype(np.int32)  # (total_tris*3,)

            # Reshape to (total_tris, 3) and reverse winding
            tris_np = flat_idx.reshape(-1, 3)[:, [2, 1, 0]]  # (total_tris, 3)
            tri_mats_np = np.repeat(mats, tri_counts)

            # Filter out-of-bounds and degenerate triangles
            v0 = tris_np[:, 0]
            v1 = tris_np[:, 1]
            v2 = tris_np[:, 2]
            valid = (v0 < vert_count) & (v1 < vert_count) & (v2 < vert_count) & \
                    (v0 != v1) & (v1 != v2) & (v2 != v0)
            tris_np = tris_np[valid]
            tri_mats_np = tri_mats_np[valid]

        elif indices is not None:
            idx_np = np.asarray(indices, dtype=np.uint32)
            # BLX - Vectorized: reshape indices to triangles, reverse winding
            n_tris = len(idx_np) // 3
            if n_tris == 0:
                return None
            flat = idx_np[:n_tris * 3].astype(np.int32).reshape(-1, 3)
            tris_np = flat[:, [2, 1, 0]]  # (N, 3) reversed winding
            tri_mats_np = np.zeros(n_tris, dtype=np.int32)

            # Filter out-of-bounds and degenerate triangles
            v0 = tris_np[:, 0]
            v1 = tris_np[:, 1]
            v2 = tris_np[:, 2]
            valid = (v0 < vert_count) & (v1 < vert_count) & (v2 < vert_count) & \
                    (v0 != v1) & (v1 != v2) & (v2 != v0)
            tris_np = tris_np[valid]
            tri_mats_np = tri_mats_np[valid]
        else:
            # BLX - Sequential indices: triangles can't be degenerate by index
            tris = [(i+2, i+1, i) for i in range(0, vert_count - 2, 3)]
            tri_mats = [0] * len(tris)

            if not tris:
                return None

            tris_np = np.array(tris, dtype=np.int32)  # (N, 3) — already reversed winding
            tri_mats_np = np.array(tri_mats, dtype=np.int32)

        if tris_np.shape[0] == 0:
            return None

        # Per-loop original vertex indices (for UV/normal lookup)
        corner_orig_vi = tris_np.reshape(-1)  # (N*3,)
        loop_count = corner_orig_vi.shape[0]

        # Deduplicate vertices by position only; sharp edges derived from normal divergence
        has_normals = normals is not None and self.custom_normals

        if has_normals:
            norms_np = np.asarray(normals, dtype=np.float32)

        # Position-only dedup for both normals and no-normals cases
        pos_quant = np.round(verts_np * 100000).astype(np.int64)
        corner_pos = pos_quant[corner_orig_vi]
        keys_view = corner_pos.view(np.dtype((np.void, corner_pos.dtype.itemsize * corner_pos.shape[1])))
        _, unique_idx, corner_to_dedup = np.unique(keys_view, return_index=True, return_inverse=True)
        corner_to_dedup = corner_to_dedup.astype(np.int32)
        dedup_count = len(unique_idx)

        dedup_orig_vi = corner_orig_vi[unique_idx]
        dedup_positions = verts_np[dedup_orig_vi] * self.global_scale

        # Build face vertex indices (dedup indices, 3 per face)
        face_verts = corner_to_dedup.reshape(-1, 3)  # (N, 3)

        # Skip degenerate faces after dedup
        d0 = face_verts[:, 0]
        d1 = face_verts[:, 1]
        d2 = face_verts[:, 2]
        non_degen = (d0 != d1) & (d1 != d2) & (d0 != d2)
        face_verts = face_verts[non_degen]
        tri_mats_np = tri_mats_np[non_degen]
        corner_to_dedup = corner_to_dedup.reshape(-1, 3)[non_degen].reshape(-1)
        corner_orig_vi = corner_orig_vi.reshape(-1, 3)[non_degen].reshape(-1)
        tri_count = face_verts.shape[0]
        loop_count = tri_count * 3

        if tri_count == 0:
            return None

        # Create mesh with from_pydata
        faces_list = face_verts.tolist()
        verts_list = dedup_positions.tolist()
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(verts_list, [], faces_list)
        mesh.update()

        # Material indices set later in _assign_materials
        mesh_dict["_tri_mats"] = tri_mats_np

        # UV0 layer
        if uv0 is not None:
            uv0_np = np.asarray(uv0, dtype=np.float32)
            uv_layer = mesh.uv_layers.new(name="UV0")
            uv_data = np.zeros((loop_count, 2), dtype=np.float32)
            valid = corner_orig_vi < uv0_np.shape[0]
            uv_data[valid, 0] = uv0_np[corner_orig_vi[valid], 0]
            if self.is_dae:
                uv_data[valid, 1] = uv0_np[corner_orig_vi[valid], 1]
            else:
                uv_data[valid, 1] = 1.0 - uv0_np[corner_orig_vi[valid], 1]
            uv_layer.data.foreach_set("uv", uv_data.reshape(-1))
            mesh.uv_layers.active = uv_layer

        # UV1 layer
        if uv1 is not None:
            uv1_np = np.asarray(uv1, dtype=np.float32)
            uv1_layer = mesh.uv_layers.new(name="UV1")
            uv1_data = np.zeros((loop_count, 2), dtype=np.float32)
            valid = corner_orig_vi < uv1_np.shape[0]
            uv1_data[valid, 0] = uv1_np[corner_orig_vi[valid], 0]
            if self.is_dae:
                uv1_data[valid, 1] = uv1_np[corner_orig_vi[valid], 1]
            else:
                uv1_data[valid, 1] = 1.0 - uv1_np[corner_orig_vi[valid], 1]
            uv1_layer.data.foreach_set("uv", uv1_data.reshape(-1))

        # Vertex colors (RGBA uint8)
        if colors is not None:
            colors_np = np.asarray(colors, dtype=np.uint8)
            color_layer = mesh.color_attributes.new(name="Color", type="BYTE_COLOR", domain="CORNER")
            col_data = np.zeros((loop_count, 4), dtype=np.float32)
            valid = corner_orig_vi < colors_np.shape[0]
            col_data[valid] = colors_np[corner_orig_vi[valid]].astype(np.float32) / 255.0
            color_layer.data.foreach_set("color", col_data.reshape(-1))
            mesh.color_attributes.active_color = color_layer

        # Normals + sharp edge derivation from normal divergence
        if has_normals:
            # Per-corner normals from original vertex indices (not dedup)
            loop_normal_flat = norms_np[corner_orig_vi]  # (loop_count, 3)

            # Derive sharp_face: if all 3 corner normals per face are nearly identical → flat
            face_corner_norms = loop_normal_flat.reshape(-1, 3, 3)  # (tri_count, 3, 3)
            n0 = face_corner_norms[:, 0, :]
            n1 = face_corner_norms[:, 1, :]
            n2 = face_corner_norms[:, 2, :]
            dot1 = np.sum(n0 * n1, axis=1)
            dot2 = np.sum(n0 * n2, axis=1)
            is_flat = (dot1 > 0.99999) & (dot2 > 0.99999)
            sharp_faces = is_flat.astype(np.int8)

            # Set all faces smooth; sharp_edge/sharp_face control shading
            mesh.polygons.foreach_set("use_smooth", array('b', [1] * tri_count))

            # Set sharp_face before normals_split_custom_set
            if "sharp_face" not in mesh.attributes:
                sharp_attr = mesh.attributes.new(name="sharp_face", type='BOOLEAN', domain='FACE')
            else:
                sharp_attr = mesh.attributes["sharp_face"]
            sharp_attr.data.foreach_set("value", array('b', sharp_faces.tolist()))

            mesh.update()

            # Set custom corner normals via normals_split_custom_set
            try:
                mesh.normals_split_custom_set(loop_normal_flat.tolist())
            except Exception:
                pass

        else:
            mesh.polygons.foreach_set("use_smooth", array('b', [1] * tri_count))

        mesh.update()
        return mesh

    def _assign_materials(self, mesh, mesh_dict):
        """Assign only used materials to mesh and remap face indices to local slots."""
        if not self.materials:
            return

        primitives = mesh_dict.get("primitives")
        if primitives is None:
            # No primitives — assign all materials
            for mat in self.materials:
                if mat is not None:
                    mesh.materials.append(mat)
                else:
                    mesh.materials.append(None)
            return

        prims_np = np.asarray(primitives, dtype=np.uint32)
        used_indices = set()
        for p in range(prims_np.shape[0]):
            info = int(prims_np[p, 2])
            mat_idx = info & 0x0FFFFFFF
            used_indices.add(mat_idx)

        # Only append used materials, build global→local slot remap
        global_to_local = {}
        local_slot = 0
        for g_idx in sorted(used_indices):
            if g_idx < len(self.materials) and self.materials[g_idx] is not None:
                mesh.materials.append(self.materials[g_idx])
                global_to_local[g_idx] = local_slot
                local_slot += 1

        # Remap face material indices from global to local slot
        tri_mats = mesh_dict.get("_tri_mats")
        if tri_mats is not None:
            remapped = np.zeros(len(tri_mats), dtype=np.int32)
            for g_idx, l_idx in global_to_local.items():
                remapped[tri_mats == g_idx] = l_idx
            mesh.polygons.foreach_set("material_index", array('i', remapped.tolist()))
            mesh.update()

    def _compute_world_matrices(self, nodes):
        """Compute world transforms per node by composing parent transforms."""
        node_count = len(nodes)
        world_matrices = [None] * node_count

        local_rot = []
        local_trans = []
        local_scale = []

        for node in nodes:
            q = node.get("quaternion", [0, 0, 0, 1])
            t = node.get("translation", [0, 0, 0])
            s = node.get("scale", [1, 1, 1])
            local_rot.append(Quaternion((q[3], q[0], q[1], q[2])))
            local_trans.append(Vector((t[0], t[1], t[2])))
            local_scale.append(Vector((s[0], s[1], s[2])))

        resolved = [False] * node_count
        for _ in range(node_count):
            any_new = False
            for i in range(node_count):
                if resolved[i]:
                    continue
                parent_idx = nodes[i].get("parent_index", -1)
                if parent_idx < 0:
                    mat = Matrix.LocRotScale(local_trans[i], local_rot[i], local_scale[i])
                    world_matrices[i] = mat
                    resolved[i] = True
                    any_new = True
                elif parent_idx < node_count and resolved[parent_idx]:
                    local_mat = Matrix.LocRotScale(local_trans[i], local_rot[i], local_scale[i])
                    world_matrices[i] = world_matrices[parent_idx] @ local_mat
                    resolved[i] = True
                    any_new = True
            if not any_new:
                break

        for i in range(node_count):
            if world_matrices[i] is None:
                world_matrices[i] = Matrix.Identity(4)

        return world_matrices

    def _get_import_collection(self):
        """Create or reuse a collection named after the imported file."""
        if self.filepath:
            col_name = os.path.splitext(os.path.basename(self.filepath))[0]
        else:
            col_name = "DAEImport"
        if not col_name:
            col_name = "DAEImport"
        col = bpy.data.collections.get(col_name)
        if col is None:
            col = bpy.data.collections.new(col_name)
            self.context.scene.collection.children.link(col)
        return col

    def _get_mesh_name(self, mesh_idx, objects_data, names):
        """Get mesh name from objects data."""
        for obj in objects_data:
            start = obj.get("start_mesh", -1)
            count = obj.get("num_meshes", 0)
            if mesh_idx >= start and mesh_idx < start + count:
                name_idx = obj.get("name_index", -1)
                if name_idx >= 0 and name_idx < len(names):
                    return names[name_idx]
        return f"mesh_{mesh_idx}"

    def _find_parent_object(self, node_idx, objects_data, mesh_objects):
        """Find the first mesh object belonging to a node."""
        for obj_entry in objects_data:
            if obj_entry.get("node_index") != node_idx:
                continue
            start = obj_entry.get("start_mesh", -1)
            count = obj_entry.get("num_meshes", 0)
            for m in range(count):
                mi = start + m
                if mi in mesh_objects:
                    return mesh_objects[mi]
        return None
