# SPDX-License-Identifier: BSD-3-Clause
import math
import threading

import numpy as np
import pytest

import chrono_batch_render

OBJ = """o Quad
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
f 1 2 3 4
o Tri
v 0 0 1
v 2 0 1
v 0 2 1
f -3 -2 -1
"""


def test_obj_files_import_turned_z_up(scene, tmp_path):
    path = tmp_path / "shapes.obj"
    path.write_text(OBJ)
    root = scene.import_file("/Shapes", path, synchronous=True).wait()
    assert root.path == "/Shapes"
    half = math.sqrt(0.5)
    np.testing.assert_allclose(
        chrono_batch_render.Xform.select(scene, "/Shapes").orient[0], (half, 0, 0, half)
    )
    meshes = chrono_batch_render.Mesh.select(scene, "/Shapes/*")
    assert len(meshes) == 2

    scaled = scene.import_file("/Scaled", path, synchronous=True, meters_per_unit=0.01).wait()
    np.testing.assert_allclose(
        chrono_batch_render.Xform.select(scene, scaled.path).scale[0], (0.01,) * 3
    )
    with pytest.raises(RuntimeError):
        scene.import_file("/Missing", tmp_path / "missing.obj", synchronous=True).wait()


def test_python_readers_build_plans_and_report_progress(scene):
    reported = []

    def read(context):
        plan = chrono_batch_render.ImportPlan()
        plan.define("/Boxes", "Xform")
        for i in range(3):
            context.report("boxes", i + 1, 3)
            plan.define(f"/Boxes/Box{i}", "Cube", {"size": 2.0})
        assert plan.prim_count == 4
        return plan

    root = scene.import_from(
        "/Boxes", read, progress=lambda p: reported.append(p.done), synchronous=True
    ).wait()
    assert root.path == "/Boxes"
    np.testing.assert_array_equal(
        chrono_batch_render.Cube.select(scene, "/Boxes/*").size, [2, 2, 2]
    )
    assert reported

    def broken(context):
        raise ValueError("bad data")

    with pytest.raises((ValueError, RuntimeError), match="bad data"):
        scene.import_from("/Broken", broken, synchronous=True).wait()
    assert scene.find("/Broken") is None


def test_cancelled_imports_leave_nothing(scene):
    started = threading.Event()

    def wait_for_cancel(context):
        started.set()
        while not context.cancelled:
            pass
        plan = chrono_batch_render.ImportPlan()
        plan.define("/Waiting", "Xform")
        return plan

    job = scene.import_from("/Waiting", wait_for_cancel)
    assert started.wait(5)
    job.cancel()
    with pytest.raises(chrono_batch_render.CancelledError):
        job.wait()
    assert scene.find("/Waiting") is None


def test_python_importers_register_by_extension(scene, tmp_path):
    class Points(chrono_batch_render.Importer):
        def name(self):
            return "points"

        def extensions(self):
            return [".pts"]

        def read(self, path, source, context):
            plan = chrono_batch_render.ImportPlan()
            plan.define(path, "Xform")
            for i, line in enumerate(source.read_entry().decode().split()):
                plan.define(f"{path}/P{i}", "Sphere", {"radius": float(line)})
            return plan

    assert ".obj" in chrono_batch_render.importer_extensions()
    chrono_batch_render.register_importer(Points())
    try:
        assert chrono_batch_render.find_importer(".pts") is not None
        file = tmp_path / "radii.pts"
        file.write_text("1.5 2.5")
        scene.import_file("/Points", file, synchronous=True).wait()
        np.testing.assert_array_equal(
            chrono_batch_render.Sphere.select(scene, "/Points/*").radius, [1.5, 2.5]
        )
    finally:
        chrono_batch_render.unregister_importer(".pts")
    assert chrono_batch_render.find_importer(".pts") is None


def test_python_importers_offer_options_and_share_extensions(scene):
    class Shift(chrono_batch_render.Importer):
        def __init__(self, name):
            super().__init__()
            self._name = name

        def name(self):
            return self._name

        def extensions(self):
            return [".shift"]

        def options(self):
            return [
                chrono_batch_render.ImportOption("shift", 2.0, label="Shift"),
                chrono_batch_render.ImportOption("axis", "x", choices=["x", "y"]),
            ]

        def read(self, path, source, context):
            plan = chrono_batch_render.ImportPlan()
            plan.define(path, "Xform", {"translate": (context.option("shift"), 0, 0)})
            return plan

    chrono_batch_render.register_importer(Shift("first"))
    chrono_batch_render.register_importer(Shift("second"))
    try:
        names = [importer.name() for importer in chrono_batch_render.find_importers(".shift")]
        assert names == ["second", "first"]
        option = chrono_batch_render.find_importer(".shift").options()[1]
        assert (option.name, option.value, option.choices) == ("axis", "x", ["x", "y"])
        source = chrono_batch_render.ImportSource("thing.shift")
        root = scene.import_file("/Moved", source, synchronous=True, options={"shift": 5}).wait()
        assert root.path == "/Moved"
        np.testing.assert_allclose(
            chrono_batch_render.Xform.select(scene, "/Moved").translate, [[5, 0, 0]]
        )
        with pytest.raises(RuntimeError):
            scene.import_file("/Bad", source, synchronous=True, options={"spin": True})
        chrono_batch_render.unregister_importer(".shift", "second")
        assert chrono_batch_render.find_importer(".shift").name() == "first"
    finally:
        chrono_batch_render.unregister_importer(".shift")
    assert chrono_batch_render.find_importers(".shift") == []
