# SPDX-License-Identifier: BSD-3-Clause
from __future__ import annotations

import importlib

from render_bench.backends.base import Backend

BACKENDS = {
    "null": ("render_bench.backends.null:NullBackend", {}),
    "newton": ("render_bench.backends.newton:NewtonBackend", {}),
    "ovrtx": ("render_bench.backends.ovrtx:OvrtxBackend", {}),
    "ovrtx_tiled": ("render_bench.backends.ovrtx:OvrtxBackend", {"tiled": True}),
    "mitsuba": ("render_bench.backends.mitsuba:MitsubaBackend", {}),
    "mitsuba_cpu": (
        "render_bench.backends.mitsuba:MitsubaBackend",
        {"variant": "llvm_ad_rgb"},
    ),
    "madrona_rt": ("render_bench.backends.madrona:MadronaBackend", {}),
}
for api in (None, "vulkan", "cuda"):
    for mode in (None, "hardware", "software"):
        BACKENDS["_".join(["chrono", *filter(None, (api, mode))])] = (
            "render_bench.backends.chrono:ChronoBackend",
            {
                "api": api and api.upper(),
                "traversal": (mode or "auto").upper(),
                "sampling": (mode or "auto").upper(),
            },
        )
BACKENDS["chrono_cuda_software_hwtex"] = (
    "render_bench.backends.chrono:ChronoBackend",
    {"api": "CUDA", "traversal": "SOFTWARE", "sampling": "HARDWARE"},
)


def known(name: str) -> bool:
    return name in BACKENDS or ":" in name


def create(name: str) -> Backend:
    path, options = BACKENDS.get(name, (name, {}))
    module, _, attribute = path.partition(":")
    return getattr(importlib.import_module(module), attribute)(**options)
