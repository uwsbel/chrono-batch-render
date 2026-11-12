# SPDX-License-Identifier: BSD-3-Clause
import numpy as np
import pytest

import chrono_batch_render


def cast(scene, origins, directions):
    hits = chrono_batch_render.RayCaster(scene).cast(
        np.asarray(origins, dtype=np.float32),
        np.asarray(directions, dtype=np.float32),
        outputs=chrono_batch_render.RayOutput.DISTANCE,
    )
    hits.done.wait()
    return hits.distance.numpy()


@pytest.mark.parametrize("per_box", [1, 4])
def test_points_hit_as_spheres_at_any_points_per_box(device, per_box):
    scene = chrono_batch_render.Scene(device, points_per_box=per_box)
    chrono_batch_render.Points.define(
        scene, "/Cloud", points=[(3, y, 0) for y in range(10)], widths=[0.5]
    )
    distance = cast(scene, [(0, 0, 0), (0, 4.5, 0), (0, 9, 0)], [(1, 0, 0)] * 3)
    np.testing.assert_allclose(distance[[0, 2]], [2.75, 2.75], atol=1e-4)
    assert np.isinf(distance[1])


def test_points_per_box_must_fit_a_count():
    with pytest.raises(TypeError):
        chrono_batch_render.Scene(points_per_box=-1)


def test_mapped_primvars_write_each_prims_values_in_place(device, scene):
    opacities = chrono_batch_render.Primvar("displayOpacity", np.ones(2, dtype=np.float32))
    for name, y in (("/Near", 0), ("/Far", 10)):
        chrono_batch_render.Points.define(
            scene, name, points=[(3, y, 0), (3, y + 4, 0)], widths=[1.0], primvars=[opacities]
        )
    clouds = chrono_batch_render.Points.select(scene, "/*").selection
    origins = [(0, 0, 0), (0, 4, 0), (0, 10, 0), (0, 14, 0)]
    np.testing.assert_allclose(cast(scene, origins, [(1, 0, 0)] * 4), [2.5] * 4, atol=1e-4)

    written = np.array([[0, 1], [1, 0]], dtype=np.float32)
    with clouds.map_primvar("displayOpacity") as mapping:
        assert mapping.values.shape == [2, 2]
        for prim in range(2):
            device.copy(device.import_tensor(written[prim]), mapping.values[prim]).wait()
    distance = cast(scene, origins, [(1, 0, 0)] * 4)
    assert np.isinf(distance[0]) and np.isinf(distance[3])
    np.testing.assert_allclose(distance[[1, 2]], [2.5, 2.5], atol=1e-4)

    with clouds.map_primvar("displayOpacity", "read") as mapping:
        np.testing.assert_array_equal(mapping.values.numpy(), written)
    with pytest.raises(RuntimeError):
        clouds.map_primvar("displayColor")
