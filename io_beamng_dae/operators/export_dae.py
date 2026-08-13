import bpy
import os
from bpy.props import StringProperty, BoolProperty, FloatProperty, EnumProperty
from bpy_extras.io_utils import ExportHelper

from ..core.mesh_extractor import MeshExtractor


class ExportDAE(bpy.types.Operator, ExportHelper):
    """Export meshes to Collada (.dae) or Cached Collada (.cdae)"""

    bl_idname = "export_scene.dae_native"
    bl_label = "Export DAE/CDAE"
    bl_options = {"REGISTER"}

    filename_ext = ".dae"

    filter_glob: StringProperty(
        default="*.dae;*.cdae",
        options={"HIDDEN"},
    )

    export_format: EnumProperty(
        name="Format",
        description="Select export format",
        items=[
            ("DAE", "DAE", "Collada XML format (.dae)"),
            ("CDAE", "CDAE", "Cached Collada binary format (.cdae)"),
        ],
        default="DAE",
    )
    selected: BoolProperty(
        name="Selection Only",
        description="Export only selected objects",
        default=False,
    )
    apply_modifiers: BoolProperty(
        name="Apply Modifiers",
        description="Apply modifiers before export",
        default=True,
    )
    custom_normals: BoolProperty(
        name="Custom Normals",
        description="Export custom normals and sharp edges",
        default=True,
    )
    active_uv_only: BoolProperty(
        name="Active UV Only",
        description="Export only the active UV layer",
        default=False,
    )
    global_scale: FloatProperty(
        name="Scale",
        description="Global scale factor for export",
        default=1.0,
        min=0.001,
        max=1000.0,
    )

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        header, body = layout.panel("DAE_export_general", default_closed=False)
        header.label(text="General")
        if body:
            body.prop(self, "export_format")
            body.prop(self, "selected")

        header, body = layout.panel("DAE_export_mesh", default_closed=False)
        header.label(text="Mesh")
        if body:
            body.prop(self, "apply_modifiers")
            body.prop(self, "custom_normals")
            body.prop(self, "active_uv_only")

        header, body = layout.panel("DAE_export_transform", default_closed=False)
        header.label(text="Transform")
        if body:
            body.prop(self, "global_scale")

    def check(self, context):
        # Update filename_ext and filter when format changes
        new_ext = ".cdae" if self.export_format == "CDAE" else ".dae"
        if new_ext != self.filename_ext:
            self.filename_ext = new_ext
            self.filter_glob = "*.cdae" if self.export_format == "CDAE" else "*.dae"
            # Fix filepath extension
            filepath = self.filepath
            base, ext = os.path.splitext(filepath)
            if ext.lower() not in (".dae", ".cdae") or ext.lower() != new_ext:
                self.filepath = base + new_ext
            return True
        return False

    def execute(self, context):
        try:
            import cdae_native
        except ImportError:
            self.report({"ERROR"}, "cdae_native C++ module not found. Build the native module first.")
            return {"CANCELLED"}

        is_cdae = self.export_format == "CDAE"
        filepath = self.filepath

        # Collect objects to export
        if self.selected:
            objects = [obj for obj in context.selected_objects if obj.type == 'MESH']
        else:
            objects = [obj for obj in context.scene.objects if obj.type == 'MESH']

        if not objects:
            self.report({"WARNING"}, "No mesh objects to export")
            return {"CANCELLED"}

        # Find dae_material_order from collections
        dae_material_order = None
        for col in bpy.data.collections:
            if "dae_material_order" in col:
                dae_material_order = col["dae_material_order"]
                break
        if dae_material_order is None and "dae_material_order" in context.scene.collection:
            dae_material_order = context.scene.collection["dae_material_order"]

        extractor = MeshExtractor(
            global_scale=self.global_scale,
            apply_modifiers=self.apply_modifiers,
            custom_normals=self.custom_normals,
            active_uv_only=self.active_uv_only,
            dae_material_order=dae_material_order,
        )

        data = extractor.extract(objects)

        if not data.get("meshes"):
            self.report({"WARNING"}, "No valid mesh data extracted")
            return {"CANCELLED"}

        try:
            if is_cdae:
                cdae_native.write_cdae(filepath, data, True)
            else:
                # Pass Blender version info for <authoring_tool> element.
                bl_info_str = f"Blender {bpy.app.version_string} commit date:{bpy.app.build_commit_date.decode() if bpy.app.build_commit_date else 'unknown'}, commit time:{bpy.app.build_commit_time.decode() if bpy.app.build_commit_time else 'unknown'}, hash:{bpy.app.build_hash.decode() if bpy.app.build_hash else 'unknown'}"
                cdae_native.write_dae(filepath, data, authoring_tool=bl_info_str)
        except Exception as e:
            self.report({"ERROR"}, f"Failed to write file: {e}")
            return {"CANCELLED"}

        mesh_count = len(data.get("meshes", []))
        mat_count = len(data.get("materials", []))
        fmt = ".cdae" if is_cdae else ".dae"
        self.report({"INFO"}, f"Exported {mesh_count} mesh(es), {mat_count} material(s) to {fmt}")
        return {"FINISHED"}
