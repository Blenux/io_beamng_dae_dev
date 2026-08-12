import bpy


class ExportDAEPanel(bpy.types.Panel):
    bl_label = "Export DAE/CDAE"
    bl_idname = "DAE_PT_export"
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOL_PROPS'
    bl_parent_id = "FILE_PT_operator"

    @classmethod
    def poll(cls, context):
        sfile = context.space_data
        if not sfile or not sfile.active_operator:
            return False
        return sfile.active_operator.bl_idname == "export_scene.dae_native"

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        op = context.space_data.active_operator

        layout.prop(op, "export_format")
        layout.prop(op, "selected")

        header, body = layout.panel("DAE_export_mesh", default_closed=False)
        header.label(text="Mesh")
        if body:
            body.prop(op, "apply_modifiers")
            body.prop(op, "custom_normals")
            body.prop(op, "active_uv_only")

        header, body = layout.panel("DAE_export_transform", default_closed=False)
        header.label(text="Transform")
        if body:
            body.prop(op, "global_scale")
