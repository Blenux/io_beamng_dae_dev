"""
Slim mesh extractor — extracts Blender mesh data into dicts for cdae_native.
Vectorized with numpy foreach_get for speed.
Handles: vertices, normals, UVs, colors, materials, sharp face/edge, node transforms.
"""

import bpy
import numpy as np
from array import array


class MeshExtractor:
    def __init__(self, global_scale=1.0, apply_modifiers=True,
                 custom_normals=True, active_uv_only=False, dae_material_order=None):
        self.global_scale = global_scale
        self.apply_modifiers = apply_modifiers
        self.custom_normals = custom_normals
        self.active_uv_only = active_uv_only
        self.dae_material_order = dae_material_order

    def extract(self, objects):
        """Extract mesh data from Blender objects into a dict for cdae_native."""
        meshes = []
        materials = []

        # Pre-populate materials from stored DAE order to preserve unused materials
        if self.dae_material_order:
            for mat_name in self.dae_material_order:
                mat = bpy.data.materials.get(mat_name)
                if mat is None:
                    mat = bpy.data.materials.new(mat_name)
                mat_entry = {"name": mat.name, "flags": 3}
                bc_r, bc_g, bc_b, bc_a = 0.8, 0.8, 0.8, 1.0
                roughness = 0.5
                metallic = 0.0
                has_bsdf = False
                if mat.use_nodes:
                    bsdf = mat.node_tree.nodes.get("Principled BSDF")
                    if bsdf is not None:
                        has_bsdf = True
                        bc = bsdf.inputs.get("Base Color")
                        if bc is not None:
                            dv = bc.default_value
                            bc_r, bc_g, bc_b, bc_a = float(dv[0]), float(dv[1]), float(dv[2]), float(dv[3])
                        rough = bsdf.inputs.get("Roughness")
                        if rough is not None:
                            roughness = float(rough.default_value)
                        metal = bsdf.inputs.get("Metallic")
                        if metal is not None:
                            metallic = float(metal.default_value)
                if not has_bsdf:
                    dc = mat.diffuse_color
                    bc_r, bc_g, bc_b, bc_a = float(dc[0]), float(dc[1]), float(dc[2]), float(dc[3])
                    roughness = float(mat.roughness)
                    metallic = float(mat.metallic)
                mat_entry["base_color"] = np.array([bc_r, bc_g, bc_b, bc_a], dtype=np.float32)
                mat_entry["roughness"] = roughness
                mat_entry["metallic"] = metallic
                # IOR and roundtrip flags from custom properties
                mat_entry["ior"] = float(mat.get("dae_ior", 1.45))
                mat_entry["has_shininess"] = bool(mat.get("dae_has_shininess", False))
                mat_entry["has_reflectivity"] = bool(mat.get("dae_has_reflectivity", False))
                materials.append(mat_entry)

        node_entries = []
        object_entries = []
        names = []

        for obj in objects:
            if obj.type != 'MESH':
                continue

            mesh, eval_obj = self._get_mesh(obj)
            if mesh is None:
                continue

            # Build local-to-global material index mapping
            local_to_global_mat = {}

            # Dict for O(1) material name → global index lookup
            mat_name_to_global = {m["name"]: i for i, m in enumerate(materials)}

            # Collect used material slot indices from face material_index
            poly_count = len(mesh.polygons)
            if poly_count > 0:
                face_mat_slots = np.empty(poly_count, dtype=np.int32)
                mesh.polygons.foreach_get("material_index", face_mat_slots)
                used_slots = set(face_mat_slots.tolist())
            else:
                used_slots = set()

            for slot_idx, mat in enumerate(obj.data.materials):
                if mat is None:
                    continue
                if slot_idx not in used_slots:
                    continue
                # Find existing global index or add new material
                global_idx = mat_name_to_global.get(mat.name)
                if global_idx is None:
                    mat_entry = {"name": mat.name, "flags": 3}
                    # Extract PBR from Principled BSDF, with viewport fallbacks
                    bc_r, bc_g, bc_b, bc_a = 0.8, 0.8, 0.8, 1.0
                    roughness = 0.5
                    metallic = 0.0
                    has_bsdf = False
                    if mat.use_nodes:
                        bsdf = mat.node_tree.nodes.get("Principled BSDF")
                        if bsdf is not None:
                            has_bsdf = True
                            bc = bsdf.inputs.get("Base Color")
                            if bc is not None:
                                dv = bc.default_value
                                bc_r, bc_g, bc_b, bc_a = float(dv[0]), float(dv[1]), float(dv[2]), float(dv[3])
                            rough = bsdf.inputs.get("Roughness")
                            if rough is not None:
                                roughness = float(rough.default_value)
                            metal = bsdf.inputs.get("Metallic")
                            if metal is not None:
                                metallic = float(metal.default_value)
                    # Fallback to viewport properties when BSDF not available
                    if not has_bsdf:
                        dc = mat.diffuse_color
                        bc_r, bc_g, bc_b, bc_a = float(dc[0]), float(dc[1]), float(dc[2]), float(dc[3])
                        roughness = float(mat.roughness)
                        metallic = float(mat.metallic)
                    mat_entry["base_color"] = np.array([bc_r, bc_g, bc_b, bc_a], dtype=np.float32)
                    mat_entry["roughness"] = roughness
                    mat_entry["metallic"] = metallic
                    # IOR and roundtrip flags from custom properties
                    mat_entry["ior"] = float(mat.get("dae_ior", 1.45))
                    mat_entry["has_shininess"] = bool(mat.get("dae_has_shininess", False))
                    mat_entry["has_reflectivity"] = bool(mat.get("dae_has_reflectivity", False))
                    materials.append(mat_entry)
                    global_idx = len(materials) - 1
                    mat_name_to_global[mat.name] = global_idx
                # Map local slot index → global material list index
                local_to_global_mat[slot_idx] = global_idx

            mesh_data = self._extract_mesh(mesh, obj, local_to_global_mat)

            if mesh_data is None:
                if eval_obj is not None:
                    eval_obj.to_mesh_clear()
                continue

            geo_name = obj.get("dae_geometry_name", "")
            if geo_name:
                mesh_data["geometry_name"] = geo_name

            tvert_names = obj.get("dae_tvert_names", "")
            if tvert_names:
                mesh_data["tvert_names"] = list(tvert_names)

            mesh_idx = len(meshes)
            meshes.append(mesh_data)

            name_idx = len(names)
            names.append(obj.name)

            # Use matrix_world for full world-space transform
            world_mat = obj.matrix_world
            loc, rot_quat, scale = world_mat.decompose()
            node_entry = {
                "name": obj.name,
                "parent_index": -1,
                "first_object": len(object_entries),
                "first_child": -1,
                "next_sibling": -1,
                "quaternion": [rot_quat.x, rot_quat.y, rot_quat.z, rot_quat.w],
                "translation": [loc.x, loc.y, loc.z],
                "scale": [scale.x, scale.y, scale.z],
            }

            node_idx = len(node_entries)
            node_entries.append(node_entry)

            object_entries.append({
                "name_index": name_idx,
                "num_meshes": 1,
                "start_mesh": mesh_idx,
                "node_index": node_idx,
                "next_sibling": -1,
            })

            if eval_obj is not None:
                eval_obj.to_mesh_clear()

        return {
            "meshes": meshes,
            "materials": materials,
            "nodes": node_entries,
            "objects": object_entries,
            "names": names,
            "unit_meter": 1.0,
        }

    def _get_mesh(self, obj):
        """Get the mesh data to export (with or without modifiers).
        Returns (mesh, eval_obj) where eval_obj is None if no modifiers applied."""
        if self.apply_modifiers:
            depsgraph = bpy.context.evaluated_depsgraph_get()
            eval_obj = obj.evaluated_get(depsgraph)
            return eval_obj.to_mesh(), eval_obj
        else:
            return obj.data, None

    def _extract_mesh(self, mesh, obj, local_to_global_mat):
        """Extract mesh data into dict format for cdae_native — vectorized."""
        if mesh.polygons is None or len(mesh.polygons) == 0:
            # Lines-only mesh: extract loose edges and vertex positions
            result = {"is_null": True, "is_dae": True, "verts_per_frame": 0}
            if len(mesh.vertices) > 0 and len(mesh.edges) > 0:
                vert_positions = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
                mesh.vertices.foreach_get("co", vert_positions)
                vert_positions = vert_positions.reshape(-1, 3)
                loose_edges = self._extract_loose_edges(mesh)
                if loose_edges is not None:
                    result["line_indices"] = loose_edges
                    result["line_verts"] = np.ascontiguousarray(vert_positions, dtype=np.float32)
            return result

        # corner_normals is auto-computed in Blender 4.1+

        loop_count = len(mesh.loops)
        vert_count = len(mesh.vertices)
        poly_count = len(mesh.polygons)

        # Fast array extraction via foreach_get
        vert_positions = np.empty(vert_count * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", vert_positions)
        vert_positions = vert_positions.reshape(-1, 3)

        corner_verts = np.empty(loop_count, dtype=np.int32)
        mesh.loops.foreach_get("vertex_index", corner_verts)

        face_offsets = np.empty(poly_count, dtype=np.int32)
        mesh.polygons.foreach_get("loop_start", face_offsets)

        face_lengths = np.empty(poly_count, dtype=np.int32)
        mesh.polygons.foreach_get("loop_total", face_lengths)

        face_mat_indices = np.empty(poly_count, dtype=np.int32)
        mesh.polygons.foreach_get("material_index", face_mat_indices)

        # Remap local material slot indices to global list indices
        max_slot = int(face_mat_indices.max()) if poly_count > 0 else 0
        remap = np.zeros(max_slot + 1, dtype=np.int32)
        for local_idx, global_idx in local_to_global_mat.items():
            if local_idx <= max_slot:
                remap[local_idx] = global_idx
        face_mat_indices = remap[face_mat_indices]

        # Sharp face detection: sharp_face attribute + use_smooth flag
        sharp_faces = np.zeros(poly_count, dtype=bool)

        sharp_attr = mesh.attributes.get("sharp_face") if mesh.attributes else None
        if sharp_attr is not None and sharp_attr.domain == 'FACE':
            sharp_vals = array('b', [0] * poly_count)
            sharp_attr.data.foreach_get("value", sharp_vals)
            sharp_faces |= np.array(sharp_vals, dtype=bool)

        smooth_vals = array('b', [0] * poly_count)
        mesh.polygons.foreach_get("use_smooth", smooth_vals)
        is_smooth = np.array(smooth_vals, dtype=bool)
        sharp_faces |= (~is_smooth)

        # Extract sharp_edge attribute for round-trip fidelity
        edge_count = len(mesh.edges)
        sharp_edges = np.zeros(edge_count, dtype=bool)
        se_attr = mesh.attributes.get("sharp_edge") if mesh.attributes else None
        if se_attr is not None and se_attr.domain == 'EDGE':
            se_vals = array('b', [0] * edge_count)
            se_attr.data.foreach_get("value", se_vals)
            sharp_edges |= np.array(se_vals, dtype=bool)

        # UV layers — vectorized foreach_get
        uv0_data = None
        uv1_data = None
        uv_extra_data = []  # Extra UV layers (2+)
        uv_layer_names = []  # Names of all UV layers in order

        if mesh.uv_layers:
            if self.active_uv_only:
                active_uv = mesh.uv_layers.active
                if active_uv:
                    uv0_data = np.empty(loop_count * 2, dtype=np.float32)
                    active_uv.data.foreach_get("uv", uv0_data)
                    uv0_data = uv0_data.reshape(-1, 2)
                    uv_layer_names.append(active_uv.name)
            else:
                uv0_layer = mesh.uv_layers.get("UV0")
                if uv0_layer is None:
                    uv0_layer = mesh.uv_layers.active
                if uv0_layer:
                    uv0_data = np.empty(loop_count * 2, dtype=np.float32)
                    uv0_layer.data.foreach_get("uv", uv0_data)
                    uv0_data = uv0_data.reshape(-1, 2)
                    uv_layer_names.append(uv0_layer.name)

                extra_uv_layers = []
                for layer in mesh.uv_layers:
                    if layer != uv0_layer:
                        extra_uv_layers.append(layer)

                if extra_uv_layers:
                    first_extra = extra_uv_layers[0]
                    uv1_data = np.empty(loop_count * 2, dtype=np.float32)
                    first_extra.data.foreach_get("uv", uv1_data)
                    uv1_data = uv1_data.reshape(-1, 2)
                    uv_layer_names.append(first_extra.name)

                    for layer in extra_uv_layers[1:]:
                        ed = np.empty(loop_count * 2, dtype=np.float32)
                        layer.data.foreach_get("uv", ed)
                        uv_extra_data.append(ed.reshape(-1, 2))
                        uv_layer_names.append(layer.name)

        # Corner normals — vectorized via foreach_get on collection
        corner_normals = None
        if self.custom_normals:
            try:
                cn_collection = mesh.corner_normals
                if cn_collection and len(cn_collection) > 0:
                    corner_normals = np.empty(loop_count * 3, dtype=np.float32)
                    cn_collection.foreach_get("vector", corner_normals)
                    corner_normals = corner_normals.reshape(-1, 3)
            except Exception:
                corner_normals = None

        # Face normals (for sharp faces)
        face_normals = np.empty(poly_count * 3, dtype=np.float32)
        mesh.polygons.foreach_get("normal", face_normals)
        face_normals = face_normals.reshape(-1, 3)

        # Vertex colors — extract all color attribute layers.
        color_layers_list = []
        if mesh.color_attributes:
            for layer in mesh.color_attributes:
                if layer.domain == 'CORNER':
                    cd = np.empty(loop_count * 4, dtype=np.float32)
                    layer.data.foreach_get("color", cd)
                    cd = (cd.reshape(-1, 4) * 255).astype(np.uint8)
                    color_layers_list.append((layer.name, cd, 'CORNER'))
                elif layer.domain == 'POINT':
                    cd = np.empty(vert_count * 4, dtype=np.float32)
                    layer.data.foreach_get("color", cd)
                    cd = (cd.reshape(-1, 4) * 255).astype(np.uint8)
                    color_layers_list.append((layer.name, cd, 'POINT'))
                else:
                    continue

        # Keep single color_data for backwards compat (first layer)
        color_data = color_layers_list[0][1] if color_layers_list else None

        # Build triangulated corner data — vectorized where possible
        has_nor = corner_normals is not None
        has_uv0 = uv0_data is not None
        has_uv1 = uv1_data is not None
        has_col = color_data is not None

        # Fan triangulation: build triangle → (loop0, loop1, loop2) index arrays
        # Vectorized: for each face with n verts, generate (n-2) triangles
        # Triangle t in face fi: (start, start+t, start+t+1)
        tri_counts_per_face = np.maximum(face_lengths - 2, 0)
        total_tris = int(tri_counts_per_face.sum())

        if total_tris == 0:
            return {"is_null": True, "is_dae": True, "verts_per_frame": 0}

        # Repeat face index for each triangle in that face
        tri_face_idx = np.repeat(np.arange(poly_count, dtype=np.int32), tri_counts_per_face)

        # For each triangle t in a face: local index t goes from 1 to n-2
        # Build local triangle indices per face
        local_t = np.concatenate([np.arange(1, n - 1, dtype=np.int32) for n in face_lengths if n >= 3])

        # Compute loop indices: start, start+t, start+t+1
        face_starts = face_offsets[tri_face_idx]
        tri_loops_np = np.stack([
            face_starts,
            face_starts + local_t,
            face_starts + local_t + 1
        ], axis=1).astype(np.int32)  # (N, 3)
        tri_count = tri_loops_np.shape[0]

        # Reverse winding: CDAE stores (2,1,0) → use (l2, l1, l0)
        reversed_loops = tri_loops_np[:, [2, 1, 0]]  # (N, 3) — CDAE order

        # Flatten to corner-level (N*3,)
        corner_loop_indices = reversed_loops.reshape(-1)  # (N*3,) — loop indices in CDAE order
        corner_face_indices = np.repeat(tri_face_idx, 3)  # (N*3,)

        # Get vertex positions per corner
        corner_vi = corner_verts[corner_loop_indices]  # (N*3,)
        corner_pos = vert_positions[corner_vi] * self.global_scale  # (N*3, 3)

        # Material per corner
        corner_mat = face_mat_indices[corner_face_indices]  # (N*3,)

        # Normals per corner
        if has_nor:
            # For sharp faces, use face normal; for smooth, use corner normal
            corner_nor = corner_normals[corner_loop_indices]  # (N*3, 3)
            # Override with face normals where sharp
            sharp_per_corner = sharp_faces[corner_face_indices]
            if np.any(sharp_per_corner):
                face_nor_per_corner = face_normals[corner_face_indices]
                corner_nor = np.where(sharp_per_corner[:, None], face_nor_per_corner, corner_nor)
        else:
            corner_nor = None

        # UV per corner (V-flip)
        if has_uv0:
            corner_uv0 = uv0_data[corner_loop_indices].copy()
            corner_uv0[:, 1] = 1.0 - corner_uv0[:, 1]
        else:
            corner_uv0 = None

        if has_uv1:
            corner_uv1 = uv1_data[corner_loop_indices].copy()
            corner_uv1[:, 1] = 1.0 - corner_uv1[:, 1]
        else:
            corner_uv1 = None

        # Extra UV layers per corner (V-flip)
        corner_uv_extra = []
        for uv_data in uv_extra_data:
            cu = uv_data[corner_loop_indices].copy()
            cu[:, 1] = 1.0 - cu[:, 1]
            corner_uv_extra.append(cu)

        # Colors per corner (first layer for backwards compat)
        if has_col:
            first_domain = color_layers_list[0][2]
            if first_domain == 'CORNER':
                corner_col = color_data[corner_loop_indices]
            else:
                corner_col = color_data[corner_vi]
        else:
            corner_col = None

        # Group by material into contiguous primitive regions
        # Sort triangles by material so each region is contiguous in the index buffer
        tri_mats = corner_mat.reshape(-1, 3)[:, 0]  # (N,) — material per triangle
        sort_order = np.argsort(tri_mats, kind='stable')
        sorted_tri_mats = tri_mats[sort_order]

        # Reorder all corner data by sort_order
        corner_pos = corner_pos.reshape(-1, 3, 3)[sort_order].reshape(-1, 3)
        if corner_nor is not None:
            corner_nor = corner_nor.reshape(-1, 3, 3)[sort_order].reshape(-1, 3)
        if corner_uv0 is not None:
            corner_uv0 = corner_uv0.reshape(-1, 3, 2)[sort_order].reshape(-1, 2)
        if corner_uv1 is not None:
            corner_uv1 = corner_uv1.reshape(-1, 3, 2)[sort_order].reshape(-1, 2)
        for ci in range(len(corner_uv_extra)):
            corner_uv_extra[ci] = corner_uv_extra[ci].reshape(-1, 3, 2)[sort_order].reshape(-1, 2)
        if corner_col is not None:
            corner_col = corner_col.reshape(-1, 3, 4)[sort_order].reshape(-1, 4)

        # Build primitive regions from sorted triangles
        primitives_buf = []
        if tri_count > 0:
            region_start = 0
            current_mat = sorted_tri_mats[0]
            for i in range(1, tri_count):
                if sorted_tri_mats[i] != current_mat:
                    primitives_buf.extend([region_start * 3, (i - region_start) * 3, int(current_mat)])
                    region_start = i
                    current_mat = sorted_tri_mats[i]
            # Last region
            primitives_buf.extend([region_start * 3, (tri_count - region_start) * 3, int(current_mat)])

        # Build result dict
        result = {
            "is_null": False,
            "is_dae": True,
            "verts_per_frame": tri_count * 3,
        }

        result["vertices"] = np.ascontiguousarray(corner_pos, dtype=np.float32)

        if corner_nor is not None:
            result["normals"] = np.ascontiguousarray(corner_nor, dtype=np.float32)

        if corner_uv0 is not None:
            result["uv0"] = np.ascontiguousarray(corner_uv0, dtype=np.float32)

        if corner_uv1 is not None:
            result["uv1"] = np.ascontiguousarray(corner_uv1, dtype=np.float32)

        if corner_uv_extra:
            result["uv_extra"] = [np.ascontiguousarray(cu, dtype=np.float32) for cu in corner_uv_extra]

        if corner_col is not None:
            result["colors"] = np.ascontiguousarray(corner_col, dtype=np.uint8)

        # Export all color layers for multi-color-layer DAE support.
        if color_layers_list:
            result["color_layer_names"] = [name for name, _, _ in color_layers_list]
            result["color_layers"] = []
            for name, cd, domain in color_layers_list:
                if domain == 'CORNER':
                    layer_corner = cd[corner_loop_indices]
                else:
                    layer_corner = cd[corner_vi]
                layer_corner = layer_corner.reshape(-1, 3, 4)[sort_order].reshape(-1, 4)
                result["color_layers"].append(np.ascontiguousarray(layer_corner, dtype=np.uint8))

        # Indices: sequential 0..N*3-1
        result["indices"] = np.arange(tri_count * 3, dtype=np.uint32)

        if primitives_buf:
            result["primitives"] = np.array(primitives_buf, dtype=np.uint32).reshape(-1, 3)

        # Extract loose edges (edges not part of any face) for <lines> export
        loose_edges = self._extract_loose_edges(mesh)
        if loose_edges is not None:
            result["line_indices"] = loose_edges
            result["line_verts"] = np.ascontiguousarray(vert_positions, dtype=np.float32)

        return result

    def _extract_loose_edges(self, mesh):
        """Find loose edges (edges not part of any face) and return as (N, 2) int32 array.

        Returns None if no loose edges exist.
        """
        if len(mesh.edges) == 0:
            return None

        # Get all edge vertex pairs
        edge_verts = np.empty(len(mesh.edges) * 2, dtype=np.int32)
        mesh.edges.foreach_get("vertices", edge_verts)
        edge_verts = edge_verts.reshape(-1, 2)

        if len(mesh.polygons) == 0:
            # All edges are loose (no faces)
            return edge_verts

        # Get face edge indices to identify which edges belong to faces
        # In Blender 5.x, we can check if an edge is loose via the 'sharp_edge' or
        # by checking if it's used by any polygon. The simplest way is to use
        # bmesh to find loose edges.
        import bmesh
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bm.edges.ensure_lookup_table()

        loose_edge_pairs = []
        for edge in bm.edges:
            if len(edge.link_faces) == 0:
                loose_edge_pairs.append([edge.verts[0].index, edge.verts[1].index])

        bm.free()

        if not loose_edge_pairs:
            return None

        return np.array(loose_edge_pairs, dtype=np.int32)
