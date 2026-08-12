import bpy


class ImportDAEPanel(bpy.types.Panel):
    bl_label = "Import DAE/CDAE"
    bl_idname = "DAE_PT_import"
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOL_PROPS'
    bl_parent_id = "FILE_PT_operator"

    @classmethod
    def poll(cls, context):
        sfile = context.space_data
        return sfile and sfile.active_operator and sfile.active_operator.bl_idname == "import_scene.dae_native"

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        op = context.space_data.active_operator

        layout.prop(op, "global_scale")

        header, body = layout.panel("DAE_import_mesh", default_closed=False)
        header.label(text="Mesh")
        if body:
            body.prop(op, "custom_normals")

        header, body = layout.panel("DAE_import_pipeline", default_closed=False)
        header.label(text="Pipeline")
        if body:
            body.prop(op, "import_to_collection")
