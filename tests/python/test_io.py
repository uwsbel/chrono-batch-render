# SPDX-License-Identifier: BSD-3-Clause
import numpy as np

import chrono_batch_render


def test_images_write_in_the_format_their_type_needs(device, tmp_path):
    color = device.import_tensor(np.full((4, 6, 4), 200, dtype=np.uint8))
    depth = device.import_tensor(np.linspace(0, 5, 24, dtype=np.float32).reshape(4, 6))
    png = chrono_batch_render.write_image(tmp_path / "color", color)
    exr = chrono_batch_render.write_image(tmp_path / "depth", depth)
    assert png.suffix == ".png" and png.exists()
    assert exr.suffix == ".exr" and exr.exists()


def test_values_colorize_into_displayable_images(device):
    depth = device.import_tensor(np.linspace(0, 1, 12, dtype=np.float32).reshape(3, 4))
    gray = chrono_batch_render.colorize_scalars(depth).numpy()
    assert gray.shape == (3, 4, 4) and gray.dtype == np.uint8
    assert gray[0, 0, 0] < gray[2, 3, 0]
    turbo = chrono_batch_render.colorize_scalars(
        depth, colormap=chrono_batch_render.Colormap.TURBO, reversed=True
    ).numpy()
    assert not np.array_equal(turbo, gray)

    hdr = device.import_tensor(np.full((2, 2, 4), 4, dtype=np.float32))
    assert chrono_batch_render.colorize_hdr(hdr, exposure=-2).numpy()[0, 0, 3] == 255
    normals = device.import_tensor(np.tile(np.array([0, 0, 1, 0], dtype=np.float32), (2, 2, 1)))
    assert chrono_batch_render.colorize_normals(normals).numpy()[0, 0, 2] > 200
