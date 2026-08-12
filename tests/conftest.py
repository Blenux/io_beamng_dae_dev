"""Pytest configuration: ensure cdae_native module is importable.

Adds the io_beamng_dae/wheels directory to sys.path so the native module
can be found when running tests outside of Blender.
"""

import os
import sys

_wheels_dir = os.path.join(os.path.dirname(__file__), "..", "io_beamng_dae", "wheels")
if os.path.isdir(_wheels_dir):
    for whl in os.listdir(_wheels_dir):
        if whl.endswith(".whl"):
            path = os.path.join(_wheels_dir, whl)
            if path not in sys.path:
                sys.path.insert(0, path)
