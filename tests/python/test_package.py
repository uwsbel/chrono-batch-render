# SPDX-License-Identifier: BSD-3-Clause
import re

import chrono_batch_render


def test_exports_resolve():
    missing = [
        name for name in chrono_batch_render.__all__ if not hasattr(chrono_batch_render, name)
    ]
    assert missing == []


def test_reports_its_version_and_build():
    assert re.match(r"^\d+\.\d+\.\d+", chrono_batch_render.__version__)
    info = chrono_batch_render.build_info()
    assert info.version == chrono_batch_render.__version__
    assert chrono_batch_render.backend_name(chrono_batch_render.Backend.VULKAN) == "vulkan"


def test_lists_adapters():
    adapters = chrono_batch_render.list_adapters()
    assert all(adapter.alias and adapter.name for adapter in adapters)


def test_sets_the_log_level():
    previous = chrono_batch_render.get_log_level()
    try:
        chrono_batch_render.set_log_level(chrono_batch_render.LogLevel.DEBUG)
        assert chrono_batch_render.get_log_level() == chrono_batch_render.LogLevel.DEBUG
        chrono_batch_render.set_log_level(chrono_batch_render.LogLevel.OFF)
        assert chrono_batch_render.get_log_level() == chrono_batch_render.LogLevel.OFF
    finally:
        chrono_batch_render.set_log_level(previous)
