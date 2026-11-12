# Installation

Chrono Batch Render ships as a Python package on PyPI, as C++ packages on GitHub releases, as a Docker image, and builds from source everywhere else.

## Requirements

| | Supported |
| --- | --- |
| Linux | x86_64 and aarch64 (glibc 2.34) |
| Windows | x64 and ARM64 |
| macOS | 26 or later on Apple silicon |
| Python | 3.10 or later |

Chrono Batch Render renders on any device it has a backend for.

| Backend | Devices | Driver |
| --- | --- | --- |
| Vulkan | NVIDIA, AMD and Intel GPUs, Apple GPUs through MoltenVK, and CPUs through lavapipe | Vulkan 1.3 or later |
| CUDA | NVIDIA GPUs | CUDA 12.0 or later |
| HIP | AMD GPUs, both CDNA and RDNA | ROCm 6.2 or later |
| Metal | Apple GPUs | In progress |
| CPU | x86_64 and aarch64 CPUs through Embree | In progress |

Ray tracing hardware is optional. On devices without it, Chrono Batch Render builds and traverses its own BVH in software, so every backend renders the same scenes. Hardware texture sampling is optional too, and devices without it filter textures in software.

On macOS, the Vulkan backend runs on Apple GPUs through MoltenVK, which Homebrew installs along with the Vulkan loader. macOS doesn't search Homebrew's library folder, so point `DYLD_FALLBACK_LIBRARY_PATH` at it too, and add that line to your shell profile to keep it.

```bash
brew install molten-vk vulkan-loader
export DYLD_FALLBACK_LIBRARY_PATH=$(brew --prefix)/lib
```

## Python

```bash
pip install chrono-batch-render
```

Wheels are built for Linux, Windows and macOS. Check the install by listing the devices Chrono Batch Render can render with.

```bash
chrono-batch-render --devices
python -c "import chrono_batch_render; print(chrono_batch_render.__version__)"
```

### Viewer

The `chrono-batch-render` wheel includes the interactive viewer. It needs a GPU that can present to a window.

```bash
chrono-batch-render
```

With no options, the `chrono-batch-render` command opens the viewer on the default scene, and `chrono-batch-render --help` lists its options. From Python, open a viewer on your own scene.

```python
import chrono_batch_render
import chrono_batch_render.viewer

device = chrono_batch_render.Device(backend=chrono_batch_render.Backend.VULKAN, presentation=True)
scene = chrono_batch_render.Scene(device)
viewer = chrono_batch_render.viewer.Viewer(scene, product="/Render/View")
viewer.run()
```

### Without a window

The `chrono-batch-render` command also renders the default scene without a window, which needs no display. `--output` writes the last frame's AOVs to a folder, `--worlds` clones the scene into several worlds with one image each, and `--preset` picks real-time or minimal shading.

```bash
chrono-batch-render --output frames --size 640x360 --aovs DistanceToCamera,Normal --worlds 4
```

`--benchmark` times steps one at a time and three in flight, after `--warmup` steps, and `--json` prints the timings as one line for scripts. `--profile` adds the profiler's report of where the time went.

```bash
chrono-batch-render --benchmark --frames 200 --size 1280x720 --json
chrono-batch-render --headless --frames 60 --profile
```

## C++

Download the archive for your platform from the [latest release](https://github.com/uwsbel/chrono-batch-render/releases/latest). It holds the library, its C++17 headers, the CMake package and a pkg-config file. The Linux archives are built against glibc 2.34, so they run on any distribution from the [requirements](#requirements).

Releases follow tags, so `latest` points at the newest one. The commands below install the latest release from the command line. On Linux and macOS, look up its version first.

```bash
VERSION=$(curl -fsSL https://api.github.com/repos/uwsbel/chrono-batch-render/releases/latest \
    | sed -nE 's/.*"tag_name": *"v([^"]+)".*/\1/p')
```

Set `VERSION` yourself to install another release.

=== "Linux"

    ```bash
    URL=https://github.com/uwsbel/chrono-batch-render/releases/download/v$VERSION
    curl -fL $URL/chrono-batch-render-$VERSION-Linux-$(uname -m).tar.gz \
        | tar -xz -C ~/.local --strip-components=1
    ```

=== "macOS"

    ```bash
    URL=https://github.com/uwsbel/chrono-batch-render/releases/download/v$VERSION
    mkdir -p ~/.local
    curl -fL $URL/chrono-batch-render-$VERSION-macOS-aarch64.tar.gz \
        | tar -xz -C ~/.local --strip-components=1
    ```

=== "Windows"

    ```powershell
    $Release = Invoke-RestMethod https://api.github.com/repos/uwsbel/chrono-batch-render/releases/latest
    $VERSION = $Release.tag_name.TrimStart("v")
    $URL = "https://github.com/uwsbel/chrono-batch-render/releases/download/v$VERSION"
    $ARCH = if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") { "aarch64" } else { "x86_64" }
    Invoke-WebRequest "$URL/chrono-batch-render-$VERSION-Windows-$ARCH.zip" -OutFile chrono_batch_render.zip
    Expand-Archive chrono_batch_render.zip .
    Rename-Item "chrono-batch-render-$VERSION-Windows-$ARCH" chrono-batch-render
    ```

Each release also lists a `SHA256SUMS` file to check the downloads against.

The Windows ZIP holds both Debug and Release builds, so either configuration links against it. Add the `chrono-batch-render` folder to `CMAKE_PREFIX_PATH` so CMake finds it, and its `bin` folder to `PATH` so programs built against it find the DLLs.

Then find it from CMake and link against `chrono_batch_render::chrono_batch_render`.

```cmake
find_package(chrono_batch_render REQUIRED CONFIG)
target_link_libraries(my_app PRIVATE chrono_batch_render::chrono_batch_render)
```

Chrono Batch Render's headers need only C++17, so projects on C++17 or later can use the packages. `chrono_batch_render::chrono_batch_render` asks CMake for C++17 at least, and with pkg-config pass `-std=c++17` or later yourself. Building Chrono Batch Render itself needs C++20.

### Viewer

The archives also hold the viewer library and the `chrono-batch-render` command line tool. The viewer is a separate library, so projects that only link `chrono_batch_render::chrono_batch_render` don't load it or the windowing libraries it opens. To use it, ask CMake for the viewer component and link against `chrono_batch_render::viewer`.

```cmake
find_package(chrono_batch_render REQUIRED CONFIG COMPONENTS viewer)
target_link_libraries(my_app PRIVATE chrono_batch_render::viewer)
```

## Docker

The image is Ubuntu 24.04 with Chrono Batch Render installed in `/opt/chrono-batch-render`.

```bash
docker run --rm -it ghcr.io/uwsbel/chrono-batch-render
```

Tags follow releases, so `0.1.0`, `0.1` and `latest`, where `latest` is always the newest final release. Release candidates such as `0.1.1rc1` only get their own tag. Add `--gpus all` to render on an NVIDIA GPU, which needs the [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html) on the host.

`CMAKE_PREFIX_PATH` already points at Chrono Batch Render, so a project mounted into the container can build against it.

```bash
docker run --rm -it -v "$PWD:/app" -w /app ghcr.io/uwsbel/chrono-batch-render \
    sh -c "cmake -S . -B build -G Ninja && cmake --build build"
```

## Building from source

Chrono Batch Render needs CMake 3.25 or later and a C++20 compiler, such as GCC 12, Clang 18 or Visual Studio 2022. Its other dependencies are downloaded while it configures.

```bash
git clone https://github.com/uwsbel/chrono-batch-render.git
cd chrono-batch-render
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHRONO_BATCH_RENDER_INSTALL=ON
cmake --build build
cmake --install build --prefix ~/.local
```

| Option | Default | Builds |
| --- | --- | --- |
| `CHRONO_BATCH_RENDER_BUILD_PYTHON` | `OFF` | The Python bindings |
| `CHRONO_BATCH_RENDER_BUILD_VIEWER` | `OFF` | The viewer that the `chrono-batch-render` command opens, which needs X11 and Wayland development files on Linux |
| `CHRONO_BATCH_RENDER_BUILD_TESTS` | `OFF` | The C++ and Python tests |
| `CHRONO_BATCH_RENDER_BUILD_BENCHMARKS` | `OFF` | The benchmarks |
| `CHRONO_BATCH_RENDER_INSTALL` | `OFF` | The install rules and packages |
| `CHRONO_BATCH_RENDER_COVERAGE` | `OFF` | Code coverage instrumentation, with GCC or Clang |

The Python package builds from the checkout with pip. It includes the viewer, so on Linux it needs the X11 and Wayland development files, which these packages provide on Ubuntu.

```bash
sudo apt-get install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev
pip install .
```

### Presets

The presets build the way CI does, each in its own folder.

| Preset | Builds |
| --- | --- |
| `ci` | Release with the tests, in `build-ci` |
| `bench` | Release with the benchmarks, in `build-bench` |
| `coverage` | Debug with coverage and the tests, in `build-coverage` |
| `package` | The TGZ archive on Linux and macOS, viewer included, in `build-package` |
| `package-windows` | The Windows ZIP with Debug and Release builds, in `build-package-windows` |

```bash
cmake --preset ci
cmake --build --preset ci
ctest --preset ci
```

The tests run on the device Chrono Batch Render picks by default, which `CHRONO_BATCH_RENDER_ADAPTER` and `CHRONO_BATCH_RENDER_BACKEND` change. To run them on others, list the adapter aliases that `chrono-batch-render` prints in `CHRONO_BATCH_RENDER_TEST_ADAPTERS`, separated by commas, or set it to `all`.

```bash
CHRONO_BATCH_RENDER_TEST_ADAPTERS=vulkan:0,cuda:0 ctest --preset ci
```

CI sets `CHRONO_BATCH_RENDER_TEST_FULL=1` to run the full suite. It tests every device and also compiles every kernel for every NVIDIA and AMD target, which the tests otherwise skip.

```bash
cmake --preset package
cmake --build --preset package
cpack --preset package
```

## Kernel cache

Chrono Batch Render compiles its kernels the first time they run and keeps them in a cache, so later runs start quickly. The cache lives in `~/.cache/chrono-batch-render` on Linux, `~/Library/Caches/chrono-batch-render` on macOS and `%LOCALAPPDATA%\chrono-batch-render\cache` on Windows, and `CHRONO_BATCH_RENDER_CACHE_DIR` moves it anywhere else. Kernels sit in `kernels/<version>`, so each version of Chrono Batch Render keeps its own, and all of them together stay under 1 GiB by dropping the least recently used. Deleting the folder is always safe.

## Troubleshooting

If `chrono-batch-render --devices` lists nothing, no backend found a driver to load. Install your GPU vendor's driver, or lavapipe for the CPU as in [Requirements](#requirements).
