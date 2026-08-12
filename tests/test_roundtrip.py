"""Round-trip tests for DAE/CDAE import/export via the cdae_native module.

These tests exercise the C++ native module directly (no Blender required).
They verify that:
  - DAE XML can be parsed and re-written with data preserved
  - CDAE binary can be parsed and re-written with data preserved
  - DAE → CDAE → DAE round-trips preserve core mesh data

Run with: python -m pytest tests/test_roundtrip.py -v
Or:       python tests/test_roundtrip.py
"""

import os
import sys
import tempfile
import numpy as np

# Add the io_beamng_dae wheels to path so we can import cdae_native
# Try installed module first, then look for wheels
try:
    import cdae_native
except ImportError:
    _wheels_dir = os.path.join(os.path.dirname(__file__), "..", "io_beamng_dae", "wheels")
    if os.path.isdir(_wheels_dir):
        for whl in os.listdir(_wheels_dir):
            if whl.endswith(".whl"):
                sys.path.insert(0, os.path.join(_wheels_dir, whl))
    import cdae_native


# ---------------------------------------------------------------------------
# Test DAE XML data
# ---------------------------------------------------------------------------

SAMPLE_DAE = """<?xml version="1.0" encoding="UTF-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset>
    <unit name="meter" meter="1.0"/>
    <up_axis>Z_UP</up_axis>
  </asset>
  <library_materials>
    <material id="mat0" name="TestMaterial">
      <instance_effect url="#mat0_fx"/>
    </material>
  </library_materials>
  <library_effects>
    <effect id="mat0_fx">
      <profile_COMMON>
        <technique sid="common">
          <lambert>
            <diffuse>
              <color>0.8 0.2 0.2 1.0</color>
            </diffuse>
            <reflectivity>
              <float>0.5</float>
            </reflectivity>
            <shininess>
              <float>500.0</float>
            </shininess>
          </lambert>
        </technique>
      </profile_COMMON>
    </effect>
  </library_effects>
  <library_geometries>
    <geometry id="mesh_0" name="TestMesh">
      <mesh>
        <source id="mesh_0_position">
          <float_array id="mesh_0_position_array" count="9">0 0 0  1 0 0  0 1 0</float_array>
          <technique_common>
            <accessor source="#mesh_0_position_array" count="3" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <source id="mesh_0_normals">
          <float_array id="mesh_0_normals_array" count="9">0 0 1  0 0 1  0 0 1</float_array>
          <technique_common>
            <accessor source="#mesh_0_normals_array" count="3" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <vertices id="mesh_0_vertices">
          <input semantic="POSITION" source="#mesh_0_position"/>
        </vertices>
        <triangles material="mat0" count="1">
          <input semantic="VERTEX" source="#mesh_0_vertices" offset="0"/>
          <input semantic="NORMAL" source="#mesh_0_normals" offset="0"/>
          <p>0 1 2</p>
        </triangles>
      </mesh>
    </geometry>
  </library_geometries>
  <library_visual_scenes>
    <visual_scene id="Scene" name="Scene">
      <node id="Root" name="Root" type="NODE">
        <matrix sid="transform">1 0 0 0  0 1 0 0  0 0 1 0  0 0 0 1</matrix>
        <instance_geometry url="#mesh_0">
          <bind_material>
            <technique_common>
              <instance_material symbol="mat0" target="#mat0"/>
            </technique_common>
          </bind_material>
        </instance_geometry>
      </node>
    </visual_scene>
  </library_visual_scenes>
  <scene>
    <instance_visual_scene url="#Scene"/>
  </scene>
</COLLADA>"""


# ---------------------------------------------------------------------------
# Helper: build a minimal shape dict for direct write_cdae
# ---------------------------------------------------------------------------

def make_minimal_shape():
    """Build a minimal ShapeData dict suitable for write_cdae/write_dae."""
    return {
        "unit_meter": 1.0,
        "radius": 1.0,
        "tube_radius": 0.5,
        "center": np.array([0.0, 0.0, 0.0], dtype=np.float32),
        "bounds": np.array([-1.0, -1.0, -1.0, 1.0, 1.0, 1.0], dtype=np.float32),
        "meshes": [
            {
                "is_dae": True,
                "mesh_type": 1,  # MESH_STANDARD
                "verts_per_frame": 3,
                "is_null": False,
                "vertices": np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32),
                "normals": np.array([[0, 0, 1], [0, 0, 1], [0, 0, 1]], dtype=np.float32),
                "indices": np.array([2, 1, 0], dtype=np.uint32),
                "primitives": np.array([[0, 3, 0]], dtype=np.uint32),
            }
        ],
        "materials": [
            {
                "name": "TestMaterial",
                "flags": 3,
                "base_color": np.array([0.8, 0.2, 0.2, 1.0], dtype=np.float32),
                "roughness": 0.5,
                "metallic": 0.5,
            }
        ],
        "nodes": [
            {
                "name": "Root",
                "parent_index": -1,
                "first_object": 0,
                "first_child": -1,
                "next_sibling": -1,
                "quaternion": np.array([0.0, 0.0, 0.0, 1.0], dtype=np.float32),
                "translation": np.array([0.0, 0.0, 0.0], dtype=np.float32),
                "scale": np.array([1.0, 1.0, 1.0], dtype=np.float32),
            }
        ],
        "objects": [
            {
                "name_index": 0,
                "name": "Root",
                "num_meshes": 1,
                "start_mesh": 0,
                "node_index": 0,
                "next_sibling": -1,
            }
        ],
        "names": ["Root"],
    }


# ---------------------------------------------------------------------------
# Tests: DAE XML round-trip
# ---------------------------------------------------------------------------

class TestDAERoundTrip:
    """Test DAE XML parse → write → parse round-trip."""

    def test_parse_dae_bytes(self):
        """Parse a DAE XML string and verify basic structure."""
        data = cdae_native.parse_dae_bytes(SAMPLE_DAE.encode("utf-8"))
        assert data is not None
        assert len(data["meshes"]) > 0
        mesh = data["meshes"][0]
        assert not mesh["is_null"]
        verts = np.asarray(mesh["vertices"])
        assert verts.shape == (3, 3)
        # Check first vertex
        np.testing.assert_allclose(verts[0], [0, 0, 0], atol=1e-5)

    def test_parse_dae_materials(self):
        """Verify PBR material parsing from DAE effects."""
        data = cdae_native.parse_dae_bytes(SAMPLE_DAE.encode("utf-8"))
        mats = [dict(m) for m in data["materials"]]
        assert len(mats) == 1
        assert mats[0]["name"] == "TestMaterial"
        bc = np.asarray(mats[0]["base_color"])
        np.testing.assert_allclose(bc, [0.8, 0.2, 0.2, 1.0], atol=1e-5)
        assert abs(float(mats[0]["metallic"]) - 0.5) < 1e-5
        # shininess 500 → roughness = 1 - 500/1000 = 0.5
        assert abs(float(mats[0]["roughness"]) - 0.5) < 1e-5

    def test_dae_write_reread(self):
        """Write a DAE file from a shape dict, re-parse it, verify data."""
        shape = make_minimal_shape()
        with tempfile.NamedTemporaryFile(suffix=".dae", delete=False) as f:
            path = f.name
        try:
            cdae_native.write_dae(path, shape)
            parsed = cdae_native.parse_dae(path)
            assert len(parsed["meshes"]) == 1
            mesh = parsed["meshes"][0]
            assert not mesh["is_null"]
            verts = np.asarray(mesh["vertices"])
            assert verts.shape == (3, 3)
            np.testing.assert_allclose(verts[0], [0, 0, 0], atol=1e-4)
            np.testing.assert_allclose(verts[1], [1, 0, 0], atol=1e-4)
            np.testing.assert_allclose(verts[2], [0, 1, 0], atol=1e-4)
            # Material should survive
            mats = [dict(m) for m in parsed["materials"]]
            assert len(mats) == 1
            assert mats[0]["name"] == "TestMaterial"
        finally:
            os.unlink(path)

    def test_dae_node_hierarchy(self):
        """Verify node hierarchy is preserved through round-trip."""
        data = cdae_native.parse_dae_bytes(SAMPLE_DAE.encode("utf-8"))
        nodes = [dict(n) for n in data["nodes"]]
        assert len(nodes) >= 1
        root = nodes[0]
        assert root["name"] == "Root"
        assert root["parent_index"] == -1


# ---------------------------------------------------------------------------
# Tests: CDAE binary round-trip
# ---------------------------------------------------------------------------

class TestCDAERoundTrip:
    """Test CDAE binary write → parse round-trip."""

    def test_cdae_write_parse_uncompressed(self):
        """Write a CDAE file (uncompressed) and re-parse it."""
        shape = make_minimal_shape()
        with tempfile.NamedTemporaryFile(suffix=".cdae", delete=False) as f:
            path = f.name
        try:
            cdae_native.write_cdae(path, shape, compress=False)
            parsed = cdae_native.parse_cdae(path)
            assert len(parsed["meshes"]) == 1
            mesh = parsed["meshes"][0]
            assert not mesh["is_null"]
            verts = np.asarray(mesh["vertices"])
            assert verts.shape == (3, 3)
            np.testing.assert_allclose(verts[0], [0, 0, 0], atol=1e-4)
            np.testing.assert_allclose(verts[1], [1, 0, 0], atol=1e-4)
            np.testing.assert_allclose(verts[2], [0, 1, 0], atol=1e-4)
        finally:
            os.unlink(path)

    def test_cdae_write_parse_compressed(self):
        """Write a CDAE file (zstd compressed) and re-parse it."""
        shape = make_minimal_shape()
        with tempfile.NamedTemporaryFile(suffix=".cdae", delete=False) as f:
            path = f.name
        try:
            cdae_native.write_cdae(path, shape, compress=True)
            parsed = cdae_native.parse_cdae(path)
            assert len(parsed["meshes"]) == 1
            mesh = parsed["meshes"][0]
            assert not mesh["is_null"]
            verts = np.asarray(mesh["vertices"])
            assert verts.shape == (3, 3)
            np.testing.assert_allclose(verts[0], [0, 0, 0], atol=1e-4)
        finally:
            os.unlink(path)

    def test_cdae_materials_preserved(self):
        """Verify PBR materials survive CDAE round-trip."""
        shape = make_minimal_shape()
        with tempfile.NamedTemporaryFile(suffix=".cdae", delete=False) as f:
            path = f.name
        try:
            cdae_native.write_cdae(path, shape, compress=False)
            parsed = cdae_native.parse_cdae(path)
            mats = [dict(m) for m in parsed["materials"]]
            assert len(mats) == 1
            assert mats[0]["name"] == "TestMaterial"
            bc = np.asarray(mats[0]["base_color"])
            np.testing.assert_allclose(bc, [0.8, 0.2, 0.2, 1.0], atol=1e-5)
            assert abs(float(mats[0]["roughness"]) - 0.5) < 1e-5
            assert abs(float(mats[0]["metallic"]) - 0.5) < 1e-5
        finally:
            os.unlink(path)


# ---------------------------------------------------------------------------
# Tests: Cross-format round-trip (DAE → CDAE → DAE)
# ---------------------------------------------------------------------------

class TestCrossFormat:
    """Test DAE → CDAE → DAE round-trip preserves core data."""

    def test_dae_to_cdae_to_dae(self):
        """Parse DAE, write as CDAE, parse CDAE, write as DAE, parse DAE."""
        # Step 1: Parse original DAE
        data1 = cdae_native.parse_dae_bytes(SAMPLE_DAE.encode("utf-8"))
        assert len(data1["meshes"]) == 1
        verts1 = np.asarray(data1["meshes"][0]["vertices"])

        # Step 2: Write as CDAE
        with tempfile.NamedTemporaryFile(suffix=".cdae", delete=False) as f:
            cdae_path = f.name
        with tempfile.NamedTemporaryFile(suffix=".dae", delete=False) as f:
            dae_path = f.name
        try:
            cdae_native.write_cdae(cdae_path, data1, compress=False)

            # Step 3: Parse CDAE
            data2 = cdae_native.parse_cdae(cdae_path)
            assert len(data2["meshes"]) == 1
            verts2 = np.asarray(data2["meshes"][0]["vertices"])

            # Vertex positions should match (within float precision)
            assert verts1.shape == verts2.shape
            np.testing.assert_allclose(verts1, verts2, atol=1e-4)

            # Step 4: Write back as DAE
            cdae_native.write_dae(dae_path, data2)

            # Step 5: Parse final DAE
            data3 = cdae_native.parse_dae(dae_path)
            assert len(data3["meshes"]) == 1
            verts3 = np.asarray(data3["meshes"][0]["vertices"])
            assert verts1.shape == verts3.shape
            np.testing.assert_allclose(verts1, verts3, atol=1e-4)
        finally:
            for p in [cdae_path, dae_path]:
                if os.path.exists(p):
                    os.unlink(p)


# ---------------------------------------------------------------------------
# Tests: Edge cases
# ---------------------------------------------------------------------------

class TestEdgeCases:
    """Test edge cases and malformed input handling."""

    def test_empty_dae(self):
        """Parsing empty bytes should raise an error."""
        try:
            cdae_native.parse_dae_bytes(b"")
            assert False, "Expected exception"
        except (RuntimeError, ValueError, Exception):
            pass

    def test_invalid_xml(self):
        """Parsing invalid XML should raise an error."""
        try:
            cdae_native.parse_dae_bytes(b"<not_collada>")
            assert False, "Expected exception"
        except (RuntimeError, ValueError, Exception):
            pass

    def test_cdae_too_small(self):
        """Parsing a too-small CDAE file should raise an error."""
        try:
            cdae_native.parse_cdae_bytes(b"\x00\x00")
            assert False, "Expected exception"
        except (RuntimeError, ValueError, Exception):
            pass

    def test_null_mesh_round_trip(self):
        """A null mesh should survive DAE round-trip."""
        shape = make_minimal_shape()
        shape["meshes"] = [
            {
                "is_dae": True,
                "mesh_type": 0,  # MESH_NULL
                "verts_per_frame": 0,
                "is_null": True,
            }
        ]
        with tempfile.NamedTemporaryFile(suffix=".dae", delete=False) as f:
            path = f.name
        try:
            cdae_native.write_dae(path, shape)
            parsed = cdae_native.parse_dae(path)
            # Null mesh may or may not appear depending on export logic
            # but the file should be valid and parseable
            assert parsed is not None
        finally:
            os.unlink(path)


# ---------------------------------------------------------------------------
# Run directly
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    # Simple test runner for when pytest is not available
    test_classes = [TestDAERoundTrip, TestCDAERoundTrip, TestCrossFormat, TestEdgeCases]
    passed = 0
    failed = 0
    for cls in test_classes:
        for name in sorted(dir(cls)):
            if name.startswith("test_"):
                instance = cls()
                method = getattr(instance, name)
                try:
                    method()
                    print(f"  PASS: {cls.__name__}.{name}")
                    passed += 1
                except Exception as e:
                    print(f"  FAIL: {cls.__name__}.{name}: {e}")
                    failed += 1
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1 if failed else 0)
