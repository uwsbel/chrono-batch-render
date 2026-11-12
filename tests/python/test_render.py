# SPDX-License-Identifier: BSD-3-Clause
import numpy as np
import pytest

import chrono_batch_render

WIDTH, HEIGHT = 32, 24
PRODUCT = "/Render/View"


def two_worlds(scene, mode="Minimal"):
    for world in range(2):
        chrono_batch_render.World.define(scene, f"/env_{world}")
        chrono_batch_render.Camera.define(
            scene,
            f"/env_{world}/Cam",
            translate=(0, -5, 0),
            orient=chrono_batch_render.look_at((0, -5, 0), (0, 0, 0)),
        )
    chrono_batch_render.Sphere.define(scene, "/env_0/Ball", radius=1)
    far = chrono_batch_render.Sphere.define(scene, "/env_1/Ball", radius=1, translate=(0, 0, 50))
    chrono_batch_render.Scope.define(scene, "/Render")
    chrono_batch_render.RenderVar.define(scene, "/Render/Color", source_name="LdrColor")
    chrono_batch_render.RenderVar.define(scene, "/Render/Depth", source_name="DistanceToCamera")
    chrono_batch_render.RenderProduct.define(
        scene,
        PRODUCT,
        camera=["/env_0/Cam", "/env_1/Cam"],
        resolution=(WIDTH, HEIGHT),
        ordered_vars=["/Render/Color", "/Render/Depth"],
        render_mode=mode,
    )
    return far


def render(renderer, steps=1):
    for _ in range(steps):
        output = renderer.step([PRODUCT])
    output.wait()
    [frame] = output[PRODUCT].frames
    return frame


@pytest.mark.parametrize("mode", ["Minimal", "RealTime"])
def test_each_world_sees_only_its_own_prims(scene, mode):
    far = two_worlds(scene, mode)
    renderer = chrono_batch_render.Renderer(scene)
    frame = render(renderer, steps=2)
    depth = frame.source("DistanceToCamera").numpy()
    color = frame.source("LdrColor").numpy()
    assert depth.shape == (2, HEIGHT, WIDTH)
    assert color.shape == (2, HEIGHT, WIDTH, 4)
    assert color.dtype == np.uint8
    assert depth[0, HEIGHT // 2, WIDTH // 2] == pytest.approx(4, abs=0.02)
    assert color[0, HEIGHT // 2, WIDTH // 2, 3] == 255
    assert np.isinf(depth[0, 0, 0])
    assert np.isinf(depth[1, HEIGHT // 2, WIDTH // 2])

    far.translate = (0, 0, 0)
    depth = render(renderer).source("DistanceToCamera").numpy()
    assert depth[1, HEIGHT // 2, WIDTH // 2] == pytest.approx(4, abs=0.02)


def test_steps_keep_time_and_frames_write(scene, tmp_path):
    two_worlds(scene)
    renderer = chrono_batch_render.Renderer(scene)
    scene.advance(0.25)
    renderer.step([PRODUCT])
    scene.advance(0.25)
    output = renderer.step([PRODUCT])
    output.wait()
    assert scene.time == pytest.approx(0.5)
    assert PRODUCT in output
    assert len(output) == 1
    [frame] = output[PRODUCT].frames
    assert sorted(frame.render_vars) == ["/Render/Color", "/Render/Depth"]
    assert frame.render_var("/Render/Depth").source_name == "DistanceToCamera"
    written = chrono_batch_render.write_frame(tmp_path, frame)
    assert written and all(path.exists() for path in written)

    scene.time = 3
    assert scene.time == pytest.approx(3)
    with pytest.raises(RuntimeError):
        renderer.step(["/Render/Nothing"])


def test_only_the_scene_moves_time(scene):
    two_worlds(scene)
    renderer = chrono_batch_render.Renderer(scene)
    with pytest.raises(TypeError):
        renderer.step([PRODUCT], 1 / 30)
    scene.advance(0.25)
    scene.advance(0.25)
    output = renderer.step([PRODUCT])
    output.wait()
    assert output.simulation_start_time == pytest.approx(0)
    assert output.simulation_end_time == pytest.approx(0.5)
    output = renderer.step([PRODUCT])
    output.wait()
    assert output.simulation_start_time == pytest.approx(0.5)
    assert scene.time == pytest.approx(0.5)
    with pytest.raises(RuntimeError):
        scene.advance(-1)


def test_rays_hit_the_closest_surface_in_their_world(scene):
    chrono_batch_render.World.define(scene, "/env_0")
    chrono_batch_render.World.define(scene, "/env_1")
    chrono_batch_render.Sphere.define(scene, "/env_0/Ball", translate=(3, 0, 0), radius=1)
    chrono_batch_render.Sphere.define(scene, "/env_1/Ball", translate=(6, 0, 0), radius=1)
    caster = chrono_batch_render.RayCaster(scene)
    origins = np.zeros((3, 3), dtype=np.float32)
    directions = np.array([[1, 0, 0], [1, 0, 0], [-1, 0, 0]], dtype=np.float32)
    hits = caster.cast(
        origins,
        directions,
        worlds=np.array([0, 1, 0], dtype=np.uint32),
        outputs=chrono_batch_render.RayOutput.DISTANCE | chrono_batch_render.RayOutput.NORMAL,
    )
    hits.done.wait()
    np.testing.assert_array_equal(hits.hit.numpy(), [1, 1, 0])
    np.testing.assert_allclose(hits.distance.numpy()[:2], [2, 5], atol=1e-4)
    np.testing.assert_allclose(hits.normal.numpy()[0], [-1, 0, 0], atol=1e-4)
    with pytest.raises(RuntimeError):
        caster.cast(origins, directions[:2])


def test_renderers_choose_how_they_stream(scene):
    two_worlds(scene)
    for streaming in ("blocking", "progressive"):
        renderer = chrono_batch_render.Renderer(scene, streaming=streaming)
        output = renderer.step([PRODUCT])
        output.wait()
        assert PRODUCT in output
    with pytest.raises(ValueError):
        chrono_batch_render.Renderer(scene, streaming="eager")
