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


def test_linear_curves_hit_as_tubes(device):
    scene = chrono_batch_render.Scene(device)
    chrono_batch_render.BasisCurves.define(
        scene,
        "/Line",
        points=[(3, 0, 0), (3, 10, 0)],
        curve_vertex_counts=[2],
        widths=[1.0],
        type="linear",
    )
    distance = cast(scene, [(0, 5, 0), (0, 12, 0)], [(1, 0, 0)] * 2)
    np.testing.assert_allclose(distance[0], 2.5, atol=1e-4)
    assert np.isinf(distance[1])


@pytest.mark.parametrize("tolerance, steps, expected", [(0.05, 16, 4.4), (0.0, 1, 2.9)])
def test_curve_tolerance_and_steps_set_how_closely_cubics_follow(
    device, tolerance, steps, expected
):
    scene = chrono_batch_render.Scene(device, curve_tolerance=tolerance, max_curve_steps=steps)
    chrono_batch_render.BasisCurves.define(
        scene,
        "/Bulge",
        points=[(3, -2, 0), (5, -2, 0), (5, 2, 0), (3, 2, 0)],
        curve_vertex_counts=[4],
        widths=[0.2],
    )
    distance = cast(scene, [(0, 0, 0)], [(1, 0, 0)])
    np.testing.assert_allclose(distance[0], expected, atol=0.02)


def rider_position(scene):
    poses = scene.select(["/Rider"]).read_world_poses(chrono_batch_render.DType.FLOAT64)
    return poses.numpy()[0, :3]


def test_riders_follow_paths_at_their_speed(scene):
    chrono_batch_render.BasisCurves.define(
        scene,
        "/Track",
        points=[(0, 0, 0), (10, 0, 0)],
        curve_vertex_counts=[2],
        type="linear",
    )
    rider = chrono_batch_render.Xform.define(scene, "/Rider")
    chrono_batch_render.FollowAPI.apply(rider, follow_path=["/Track"], follow_path_speed=2.0)
    np.testing.assert_allclose(rider_position(scene), [0, 0, 0], atol=1e-5)
    scene.advance(1.0)
    np.testing.assert_allclose(rider_position(scene), [2, 0, 0], atol=1e-5)

    follow = chrono_batch_render.FollowAPI.select(scene, "/Rider")
    follow.follow_path_position = 5.0
    np.testing.assert_allclose(rider_position(scene), [5, 0, 0], atol=1e-5)
    follow.follow_path_mode = "parameter"
    follow.follow_path_position = 0.25
    follow.follow_path_speed = 0.0
    np.testing.assert_allclose(rider_position(scene), [2.5, 0, 0], atol=1e-5)
