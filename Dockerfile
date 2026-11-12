ARG UBUNTU_VERSION=24.04

FROM ubuntu:${UBUNTU_VERSION} AS build

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHRONO_BATCH_RENDER_INSTALL=ON \
    && cmake --build build \
    && cmake --install build --prefix /opt/chrono-batch-render --component runtime \
    && cmake --install build --prefix /opt/chrono-batch-render --component dev

FROM ubuntu:${UBUNTU_VERSION} AS dev

LABEL org.opencontainers.image.source="https://github.com/uwsbel/chrono-batch-render" \
      org.opencontainers.image.description="A physically-based, real-time, spectral renderer for planetary robotics and sensing" \
      org.opencontainers.image.licenses="BSD-3-Clause"

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        libvulkan1 \
        mesa-vulkan-drivers \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/chrono-batch-render /opt/chrono-batch-render
ENV CMAKE_PREFIX_PATH=/opt/chrono-batch-render \
    NVIDIA_DRIVER_CAPABILITIES=compute,graphics,utility,display
