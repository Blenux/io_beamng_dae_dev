# SPDX-License-Identifier: GPL-3.0-or-later

"""FileHandler for DAE/CDAE drag-and-drop import support."""

import bpy
from bpy_extras.io_utils import poll_file_object_drop


class IO_FH_dae_native(bpy.types.FileHandler):
    bl_idname = "IO_FH_dae_native"
    bl_label = "DAE/CDAE"
    bl_import_operator = "import_scene.dae_native"
    bl_export_operator = "export_scene.dae_native"
    bl_file_extensions = ".dae;.cdae"

    @classmethod
    def poll_drop(cls, context):
        return poll_file_object_drop(context)
