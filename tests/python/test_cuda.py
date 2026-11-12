# SPDX-License-Identifier: BSD-3-Clause
import glob
import os
import subprocess
import sys

import pytest

import chrono_batch_render

BUILD = """
import chrono_batch_render
scene = chrono_batch_render.Scene(chrono_batch_render.Device(backend=chrono_batch_render.Backend.CUDA))
for i in range(40):
    chrono_batch_render.Sphere.define(scene, f"/Ball{i}", translate=(3 * i, 0, 0))
scene.publish(wait=True)
print("built")
"""


def test_bvhs_build_on_cuda_with_nvrtc_12():
    if not sys.platform.startswith("linux"):
        pytest.skip("preloading an NVRTC needs Linux")
    if not any(
        adapter.backend == chrono_batch_render.Backend.CUDA and not adapter.unsupported
        for adapter in chrono_batch_render.list_adapters()
    ):
        pytest.skip("no CUDA adapter")
    nvrtc = sorted(glob.glob("/usr/local/cuda-12*/lib64/libnvrtc.so.12"))
    if not nvrtc:
        pytest.skip("no NVRTC 12 installed")
    env = dict(os.environ, LD_PRELOAD=nvrtc[-1], CHRONO_BATCH_RENDER_LOG="debug")
    run = subprocess.run(
        [sys.executable, "-c", BUILD], env=env, capture_output=True, text=True, timeout=300
    )
    assert "NVRTC 12." in run.stderr
    assert run.returncode == 0 and "built" in run.stdout, run.stderr[-2000:]
