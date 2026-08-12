import bpy
import os
import numpy as np
from bpy.props import StringProperty, BoolProperty, FloatProperty, CollectionProperty
from bpy_extras.io_utils import ImportHelper, poll_file_object_drop

from ..core.mesh_builder import MeshBuilder


def _create_materials(materials_data):
    """Create Blender materials with Principled BSDF from parsed PBR data."""
    materials = []
    for mat_data in materials_data:
        name = mat_data.get("name", "default")
        mat = bpy.data.materials.get(name)
        if mat is None:
            mat = bpy.data.materials.new(name)
        # Ensure use_nodes is always True
        mat.use_nodes = True

        # Extract PBR values
        base_color = mat_data.get("base_color")
        roughness = float(mat_data.get("roughness", 0.5))
        metallic = float(mat_data.get("metallic", 0.0))

        bc_r = bc_g = bc_b = 0.8
        bc_a = 1.0
        if base_color is not None:
            bc = np.asarray(base_color, dtype=np.float32).flatten()
            if bc.size >= 4:
                bc_r, bc_g, bc_b, bc_a = float(bc[0]), float(bc[1]), float(bc[2]), float(bc[3])

        # Set viewport properties (work regardless of node setup)
        mat.diffuse_color = (bc_r, bc_g, bc_b, bc_a)
        mat.roughness = roughness
        mat.metallic = metallic

        # Set Principled BSDF node inputs
        bsdf = mat.node_tree.nodes.get("Principled BSDF") if mat.use_nodes else None
        if bsdf is not None:
            if "Base Color" in bsdf.inputs:
                bsdf.inputs["Base Color"].default_value = (bc_r, bc_g, bc_b, bc_a)
            if "Roughness" in bsdf.inputs:
                bsdf.inputs["Roughness"].default_value = roughness
            if "Metallic" in bsdf.inputs:
                bsdf.inputs["Metallic"].default_value = metallic

        materials.append(mat)
    return materials


class ImportDAE(bpy.types.Operator, ImportHelper):
    """Import a Collada (.dae) or Cached Collada (.cdae) file"""

    bl_idname = "import_scene.dae_native"
    bl_label = "Import DAE/CDAE"
    bl_options = {"REGISTER", "UNDO"}

    filter_glob: StringProperty(
        default="*.dae;*.cdae",
        options={"HIDDEN"},
    )

    # Files collection and directory for FileHandler drag/drop support
    files: CollectionProperty(
        type=bpy.types.OperatorFileListElement,
        options={"HIDDEN", "SKIP_SAVE"},
    )
    directory: StringProperty(
        subtype='DIR_PATH',
        options={"HIDDEN", "SKIP_SAVE"},
    )

    global_scale: FloatProperty(
        name="Scale",
        description="Manual scale factor",
        default=1.0,
        min=0.001,
        max=1000.0,
    )
    custom_normals: BoolProperty(
        name="Custom Normals",
        description="Import custom/smooth normals",
        default=True,
    )
    import_to_collection: BoolProperty(
        name="Import to Collection",
        description="Create a collection named after the imported file and place objects in it",
        default=True,
    )

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        header, body = layout.panel("DAE_import_general", default_closed=False)
        header.label(text="General")
        if body:
            body.prop(self, "global_scale")

        header, body = layout.panel("DAE_import_mesh", default_closed=False)
        header.label(text="Mesh")
        if body:
            body.prop(self, "custom_normals")

        header, body = layout.panel("DAE_import_pipeline", default_closed=False)
        header.label(text="Pipeline")
        if body:
            body.prop(self, "import_to_collection")

    def invoke(self, context, event):
        # Show popup dialog for drag/drop and menu invocation
        return ImportHelper.invoke_popup(self, context)

    def execute(self, context):
        # Handle multiple files from FileHandler (drag/drop)
        if self.files:
            ret = {"CANCELLED"}
            for file in self.files:
                filepath = os.path.join(self.directory, file.name)
                if self._import_single(context, filepath) == {"FINISHED"}:
                    ret = {"FINISHED"}
            return ret
        else:
            return self._import_single(context, self.filepath)

    def _import_single(self, context, filepath):
        """Import a single DAE or CDAE file."""
        try:
            import cdae_native
        except ImportError:
            self.report({"ERROR"}, "cdae_native C++ module not found. Build the native module first.")
            return {"CANCELLED"}

        is_cdae = filepath.lower().endswith(".cdae")

        # Parse file via C++ native module
        try:
            if is_cdae:
                data = cdae_native.parse_cdae(filepath)
            else:
                data = cdae_native.parse_dae(filepath)
        except Exception as e:
            self.report({"ERROR"}, f"Failed to parse file: {e}")
            return {"CANCELLED"}

        if not data or not data.get("meshes"):
            self.report({"WARNING"}, "No meshes found in file")
            return {"FINISHED"}

        # Create materials (name-only, no textures)
        materials = _create_materials(data.get("materials", []))

        # Build meshes
        builder = MeshBuilder(
            context=context,
            materials=materials,
            global_scale=self.global_scale,
            custom_normals=self.custom_normals,
            is_dae=not is_cdae,
            filepath=filepath,
            use_collection=self.import_to_collection,
        )

        objects = builder.build_meshes(data)
        builder.build_node_hierarchy(data, objects)

        mesh_count = len([o for o in objects.values() if o.type == 'MESH'])
        self.report({"INFO"}, f"Imported {mesh_count} mesh(es), {len(materials)} material(s)")
        return {"FINISHED"}
