# SPDX-License-Identifier: BSD-3-Clause
import pytest

import chrono_batch_render


@pytest.fixture(scope="session")
def device():
    try:
        return chrono_batch_render.Device()
    except RuntimeError as error:
        pytest.skip(f"no supported adapter: {error}")


@pytest.fixture
def scene(device):
    return chrono_batch_render.Scene(device)
