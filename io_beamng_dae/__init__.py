import bpy

from .operators.import_dae import ImportDAE
from .operators.export_dae import ExportDAE
from .ui.filehandler import IO_FH_dae_native
from .ui.import_panel import ImportDAEPanel
from .ui.export_panel import ExportDAEPanel


def menu_import(self, context):
    self.layout.operator(ImportDAE.bl_idname, text="BeamNG Collada (.dae, .cdae)")


def menu_export(self, context):
    self.layout.operator(ExportDAE.bl_idname, text="BeamNG Collada (.dae, .cdae)")


classes = (
    ImportDAE,
    ExportDAE,
    IO_FH_dae_native,
    ImportDAEPanel,
    ExportDAEPanel,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)

    bpy.types.TOPBAR_MT_file_import.append(menu_import)
    bpy.types.TOPBAR_MT_file_export.append(menu_export)


def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(menu_import)
    bpy.types.TOPBAR_MT_file_export.remove(menu_export)

    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
