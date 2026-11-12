# SPDX-License-Identifier: BSD-3-Clause
import threading

import numpy as np

import chrono_batch_render

PRODUCT = "/Render/View"
STEPS = 30
MOVES = 200


def camera_and_balls(scene, count):
    chrono_batch_render.Camera.define(
        scene,
        "/Cam",
        translate=(0, -8, 0),
        orient=chrono_batch_render.look_at((0, -8, 0), (0, 0, 0)),
    )
    for i in range(count):
        chrono_batch_render.Sphere.define(scene, f"/Ball{i}", radius=0.25)
    chrono_batch_render.Scope.define(scene, "/Render")
    chrono_batch_render.RenderVar.define(scene, "/Render/Depth", source_name="DistanceToCamera")
    chrono_batch_render.RenderProduct.define(
        scene, PRODUCT, camera=["/Cam"], resolution=(16, 12), ordered_vars=["/Render/Depth"]
    )
    return chrono_batch_render.Sphere.select(scene, "/Ball*").selection


def run_all(*targets):
    errors = []

    def guarded(target):
        try:
            target()
        except Exception as error:  # noqa: BLE001 (reported below)
            errors.append(error)

    threads = [threading.Thread(target=guarded, args=(target,)) for target in targets]
    for thread in threads:
        thread.start()
    return threads, errors


def finish(threads, errors):
    for thread in threads:
        thread.join(timeout=120)
        assert not thread.is_alive(), "a thread is still waiting for the scene"
    assert not errors, errors


def test_threads_write_while_another_renders(scene):
    balls = camera_and_balls(scene, 4)
    renderer = chrono_batch_render.Renderer(scene)

    def physics():
        poses = np.zeros((4, 7), dtype=np.float32)
        poses[:, 6] = 1
        for move in range(MOVES):
            poses[:, 0] = np.arange(4) + move * 0.001
            balls.write_poses(poses)

    def editor():
        for edit in range(MOVES):
            ball = scene.find(f"/Ball{edit % 4}")
            ball["radius"] = 0.25 + 0.001 * edit
            assert scene.find("/Cam") is not None

    threads, errors = run_all(physics, editor)
    for _ in range(STEPS):
        scene.advance(1 / 60)
        renderer.step([PRODUCT]).wait()
    finish(threads, errors)

    output = renderer.step([PRODUCT])
    output.wait()
    np.testing.assert_allclose(
        balls.read_poses().numpy()[:, 0], np.arange(4) + (MOVES - 1) * 0.001, atol=1e-6
    )
    last = (MOVES - 1) % 4
    assert scene.find(f"/Ball{last}")["radius"] == np.float32(0.25 + 0.001 * (MOVES - 1))
    assert abs(scene.time - STEPS / 60) < 1e-9


def test_threads_define_while_another_renders(scene):
    camera_and_balls(scene, 1)
    renderer = chrono_batch_render.Renderer(scene)

    def definer(first):
        for i in range(first, first + 50):
            chrono_batch_render.Cube.define(scene, f"/Box{i}", translate=(i, 0, 0))

    threads, errors = run_all(lambda: definer(0), lambda: definer(50))
    for _ in range(STEPS):
        renderer.step([PRODUCT]).wait()
    finish(threads, errors)
    assert len(chrono_batch_render.Cube.select(scene, "/Box*").selection.prims) == 100


def test_gil_heavy_threads_dont_deadlock_with_steps(scene):
    balls = camera_and_balls(scene, 8)
    renderer = chrono_batch_render.Renderer(scene)
    stop = threading.Event()
    outputs = []

    def spinner():
        total = 0
        while not stop.is_set():
            total += sum(range(1000))

    def editor():
        while not stop.is_set():
            for i in range(8):
                scene.find(f"/Ball{i}")["radius"] = 0.2 + 0.01 * i

    def mover():
        poses = np.zeros((8, 7), dtype=np.float32)
        poses[:, 6] = 1
        while not stop.is_set():
            poses[:, 2] += 0.001
            balls.write_poses(poses)

    def reader():
        while not stop.is_set():
            if outputs:
                frame = outputs[-1][PRODUCT].frames
                if frame:
                    frame[0].source("DistanceToCamera").numpy()

    threads, errors = run_all(spinner, editor, mover, reader)
    try:
        for _ in range(60):
            scene.advance(1 / 60)
            output = renderer.step([PRODUCT])
            output.wait()
            outputs.append(output)
            del outputs[:-2]
    finally:
        stop.set()
    finish(threads, errors)
