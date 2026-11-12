# Chrono Batch Render Benchmarks

A framework for comparing GPU batch renderers on throughput, scaling and image quality. It renders the same scenes under the same timing rules, then reports speed and accuracy side by side.

## Capabilities

- **Shared scenes.** Scenes are generated in Python and passed to every backend through one interface, so each renderer draws the same geometry, cameras and lights.
- **Per-backend environments.** Each backend runs in its own virtual environment and a separate worker process. Backends with conflicting CUDA, USD or Python versions can run in the same suite, and a crash in one backend doesn't stop the run.
- **Adaptive timing.** Warmup continues until frame times settle. Repeats continue until the confidence interval reaches its target. Each result records its spread, the machine, the driver and the clocks it ran at.
- **Machine setup.** `scripts/system-prep` locks GPU and memory clocks, pins the CPU governor, moves services and interrupts off benchmark cores, allows realtime priority and starts CUDA MPS. `restore` undoes all of it.
- **Parameter sweeps.** A suite sweeps any combination of worlds, cameras, resolution, outputs, lighting tier, samples per pixel and the share of the GPU a backend may use.
- **Reference checks.** `check` compares a backend's depth and segmentation against a reference ray caster. `quality` scores captured frames with SSIM, FLIP and LPIPS against an analytic reference or another backend such as Mitsuba.
- **Kernel profiling.** `profile` runs a suite under Nsight Systems or Nsight Compute and lists the slowest kernels at each point.
- **Hardware counters.** `--counters` adds CPU, memory, cache and GPU counters to each result.
- **Startup timing.** Startup is timed separately from steady-state frames, and cold runs drop the page cache first.
- **Reports and media.** Results are stored as Parquet. `report` writes an HTML page with scaling, quality and startup charts, and `images`, `movie`, `panel` and `montage` turn captures into labelled comparisons.

## Quick Start

Install the harness into its own environment.

```bash
cd benchmarks
pip install -e ".[media,test]"
```

It's recommended to set up a [uv](https://docs.astral.sh/uv/) environment in the `benchmarks` directory itself.

### Building the Environments

Each backend will get its own virtual environment.

Chrono Batch Render is benchmarked from this repository, not from PyPI. `scripts/setup-chrono` builds its Python module from the working tree into `../build-render-bench`, and `envs/chrono/paths.txt` puts that build on the environment's path. Rerun `scripts/setup-chrono` after changing the renderer, since the benchmarks use whatever was last built.

Madrona is built from source and needs Python 3.12 and CUDA 12.8. Set `CUDA_ROOT` if CUDA isn't in `/usr/local/cuda-12.8`, and `JOBS` to change the default of 4 build jobs. `scripts/setup-madrona` configures `native/madrona`, which fetches the Madrona engine at a pinned commit into `native/madrona/build/madrona-src` and applies the patches in `native/madrona/patches`. Madrona compiles its kernels from those sources at run time, so keep the build directory in place.

A typical workflow would look like this:

```bash
# Chrono Batch Render, built from this repo into ../build-render-bench
scripts/make-env chrono
scripts/setup-chrono

# Newton and OVRTX
scripts/make-env newton
scripts/make-env ovrtx

# Madrona, which needs Python 3.12 and CUDA 12.8
scripts/make-env madrona --python "$(uv python find 3.12)"
scripts/setup-madrona
```

Note that the Madrona build can take some time as it has to clone the repo and dependencies.

### Checking Backends

Then the `check` command will build a small scene and render one frame and compare against a reference to identify any large deviations or incorrectly configured environments before kicking off benchmark runs.

```bash
render-bench check chrono_vulkan_hardware
render-bench check chrono_cuda_software
render-bench check newton
render-bench check ovrtx_tiled --tier direct
render-bench check madrona_rt --tier depth
```

### Smoke Test

The smoke suite is a short timed run of Chrono Batch Render alone. It confirms the harness works end to end, from worker processes and timing to captures and results, before a long run.

```bash
render-bench run configs/suites/smoke.toml
render-bench summary data/results/<time>-smoke
```

### Compare

The compare suite needs every environment above. Lock the clocks for the run and release them afterwards. `system-prep status` shows what is applied.

```bash
sudo scripts/system-prep apply --gpu-clock 2100
render-bench run configs/suites/compare.toml
sudo scripts/system-prep restore
```

Each run is written to `data/results/<time>-<suite>/`. Summarise it or build a report from one or more runs.

```bash
render-bench summary data/results/<time>-compare
render-bench report data/results/<time>-compare -o report.html
```

`quality` also needs `scripts/make-env mitsuba`.

## Suites

| Suite | Backends | Purpose |
| --- | --- | --- |
| `smoke` | Chrono Batch Render | A quick check that the harness and Chrono Batch Render work end to end |
| `compare` | Chrono Batch Render Vulkan hardware and CUDA software, Newton, OVRTX tiled, Madrona RT | The headline comparison across 16 to 1,024 worlds at 64x64 and 128x128 |
| `quality` | Every backend including Mitsuba | Captures frames for `render-bench quality` |

Run only some backends from a suite with `--backend`.

```bash
render-bench run configs/suites/compare.toml --backend newton --backend madrona_rt
```

## Hide and Seek

The `hide_and_seek` scene follows the Hide and Seek benchmark in Rosenzweig, Shacklett et al., "High-Throughput Batch Rendering for Embodied AI" (SIGGRAPH Asia 2024). It is a procedural reconstruction of the scene, not Madrona's own level generator. Each world is a walled arena with three hiders and three seekers, four boxes and two ramps. Every backend renders it through the shared scene description.

Cameras ride on the agents with a 60 degree vertical field of view. The `throughput` workload defaults to six cameras per world, one per agent, and a suite can sweep `cameras_per_world` to change that. A scene config can also set `cameras_per_world`, which replaces the suite's value for that scene.

Madrona's ray tracer ignores material base colors and only multiplies textures, so its backend gives each object a 1x1 texture of its base color. It casts shadow rays only when `MADRONA_RT_SHADOWS=1` is set while its kernels compile, which the backend does for the `direct` tier.

## Framework

Configs live in three folders under `configs/`.

- **`scenes/`** names a dataset from `render_bench/datasets` with its seed and parameters.
- **`workloads/`** holds the base render settings and timing rules that suites start from.
- **`suites/`** picks backends, scenes and a workload, then lists the values to sweep. Each sweep combination becomes one point. A suite's `[measurement]` overrides the workload's timing and `[capture]` saves frames.

The harness in `render_bench/harness.py` builds the scene and runs each point in a worker process, using the Python from that backend's environment. The worker loads the scene, warms up, times frames, captures images and writes its samples back. The run directory holds a `manifest.json` with the suite, machine and settings, plus the samples as Parquet.

## Backends

| Name | Renderer | Environment |
| --- | --- | --- |
| `chrono`, `chrono_<api>_<mode>` | Chrono Batch Render, built from this repository by `scripts/setup-chrono`. `api` is `vulkan` or `cuda`, and `mode` is `hardware` or `software`. | `envs/chrono` |
| `newton` | Newton's tiled camera renderer | `envs/newton` |
| `madrona_rt` | Madrona's ray tracer, built from a pinned Madrona commit with the patches in `native/madrona/patches` | `envs/madrona` |
| `ovrtx`, `ovrtx_tiled` | NVIDIA OVRTX | `envs/ovrtx` |
| `mitsuba`, `mitsuba_cpu` | Mitsuba 3 on CUDA or LLVM, used mainly as a quality reference | `envs/mitsuba` |
| `null` | Does nothing, to measure the harness's own overhead | any |

A backend that can't run a point reports why, for example an unsupported tier or too many worlds, and the run continues.

## Extending

### Adding a Backend

1. Implement the `Backend` protocol in `render_bench/backends/base.py` in a new module under `render_bench/backends/`. It provides `info`, `capabilities`, `load`, `update`, `render`, `sync`, `readback`, `stream` and `close`.
2. Declare what it supports in `capabilities()` so the harness skips points it can't run.
3. Register a name in `BACKENDS` in `render_bench/backends/__init__.py`. The part before the first underscore picks the environment, so `foo_fast` runs in `envs/foo`.
4. Add `envs/<name>/requirements.txt`, plus `envs/<name>/paths.txt` for local builds that must be on the Python path, then run `scripts/make-env <name>`.
5. Verify it against the reference with `render-bench check <name> --save check.png` before timing it.

### Adding a Benchmark

A benchmark is a suite, so most new scenarios need only TOML. Start from the smallest change that covers what you want to measure.

1. **New sweep over an existing scene.** Copy a suite in `configs/suites/`, then set `name`, `backends`, `scenes` and `workload`. List the values to vary under `[sweep]`, which takes any `Workload` field (`worlds`, `cameras_per_world`, `tier`, `outputs`, `spp`, `sm_percent`, `dt`) plus `resolution` as `[width, height]` pairs. Add `[measurement]` to change the timing rules and `[capture]` to save frames.
2. **Variant of an existing scene.** Add `configs/scenes/<name>.toml` with `dataset`, `seed` and a `[params]` table that overrides the dataset's `DEFAULTS`, such as agent counts or `fov_y` for `hide_and_seek`. Unknown params raise an error. Set `cameras_per_world` at the top level only if the scene must always use that many cameras, since it replaces the suite's value.
3. **New scene.** Write a new dataset as described in [Adding a Scene](#adding-a-scene).
4. **New base settings.** Add `configs/workloads/<name>.toml` with `[workload]` and `[measurement]` tables when several suites should share different defaults than `throughput`.
5. Inspect the scene with `render-bench scene <name> --worlds 4 --cameras 2`, then confirm every backend draws it the same with `render-bench check <backend> --scene <name> --save check.png` before timing it.

### Adding a Scene

A scene is a dataset, a Python function that generates every world, plus a config in `configs/scenes/` that picks its seed and parameters. Every backend renders the same `Scene`, so a new dataset works with all of them without backend changes.

1. **Write the dataset.** Add `render_bench/datasets/<name>.py` with `build(spec, worlds, cameras_per_world) -> Scene`. Merge `spec.params` over a `DEFAULTS` dict and raise on unknown keys, so a typo in a config fails instead of being ignored. Seed a generator from `spec.seed` so the same config always builds the same scene.
2. **Fill in the `Scene`** from `render_bench/scene.py`. Every array is flat across all worlds, and each world must have the same instance layout.

    | Field | Contents |
    | --- | --- |
    | `meshes` | Unit shapes from `datasets/shapes.py` (`plane`, `box`, `ramp`, `cylinder`) or your own `Mesh` |
    | `materials` | A `Material(base_color)` per kind of object |
    | `classes` | Segmentation class names |
    | `instances` | Per-instance `world`, `mesh`, `material` and `semantic` indices, and 4x4 `transform`s from `compose(position, yaw, scale)` |
    | `cameras` | Per-camera `world`, `parent`, `local`, `fov_y`, `near` and `far`. `parent` is a moving instance the camera rides on, with `local` as its mount, or `-1` for a fixed camera, with `local` as its world pose |
    | `lights` | `DistantLight`s. The `direct` tier and the reference use the first one |
    | `motion` | A `LissajousMotion` over the instances that move, which the harness streams to backends every frame |
    | `ambient`, `orbit` | Optional ambient light, and an `OrbitMotion` that flies cameras around the scene |

3. **Follow the conventions.** Multiply each camera pose by `LOOK_ALONG_X`, as `hide_and_seek` does, so the camera looks along its yaw. End `build` with `scene.instances.transform[moving] = scene.state(0.0).transform` so frame 0 matches the motion.
4. **Register it.** Add the dataset to `DATASETS` in `render_bench/datasets/__init__.py`.
5. **Add a config.** Create `configs/scenes/<name>.toml` with `dataset`, `seed` and a `[params]` table. Several configs can share one dataset with different params.

    ```toml
    dataset = "<name>"
    seed = 0

    [params]
    # any keys from the dataset's DEFAULTS
    ```

6. **Verify it.** Print its size, then check every backend against the reference. A new scene is the most likely place for a backend's conventions to break.

    ```bash
    render-bench scene <name> --worlds 4 --cameras 2
    render-bench check <backend> --scene <name> --tier direct --save check.png
    ```

7. **Test it.** Add a test like `tests/test_hide_and_seek.py` that pins the instance counts per world and checks that a seed always builds the same scene.
8. **Use it.** List it under `scenes` in a suite.
