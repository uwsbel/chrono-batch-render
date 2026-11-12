# SPDX-License-Identifier: BSD-3-Clause
import math

import numpy as np
import pytest

import chrono_batch_render


def rotate(q, v):
    x, y, z, w = q
    u = np.array([x, y, z])
    v = np.asarray(v, dtype=float)
    return v + 2 * np.cross(u, np.cross(u, v) + w * v)


def test_look_at_turns_minus_z_toward_the_target():
    q = chrono_batch_render.look_at((0, -5, 1), (0, 0, 1))
    assert len(q) == 4
    np.testing.assert_allclose(rotate(q, (0, 0, -1)), (0, 1, 0), atol=1e-12)
    np.testing.assert_allclose(rotate(q, (0, 1, 0)), (0, 0, 1), atol=1e-12)


def test_rotations_agree_with_each_other():
    half = math.sqrt(0.5)
    np.testing.assert_allclose(
        chrono_batch_render.from_axis_angle((0, 0, 2), 90), (0, 0, half, half)
    )
    np.testing.assert_allclose(
        chrono_batch_render.look_along((1, 0, 0)),
        chrono_batch_render.look_at((3, 3, 3), (4, 3, 3)),
        atol=1e-12,
    )
    np.testing.assert_allclose(
        rotate(chrono_batch_render.from_azimuth_elevation(0, 0), (0, 0, -1)), (0, -1, 0), atol=1e-12
    )


def test_zero_directions_raise():
    with pytest.raises(RuntimeError):
        chrono_batch_render.look_along((0, 0, 0))
