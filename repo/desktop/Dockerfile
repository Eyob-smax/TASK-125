# ShelterOps Desk Console — Build and Test Container
#
# PURPOSE: Reproducible build of shelterops_lib and execution of unit/api tests
# on a Linux CI host. The Win32/DX11 GUI executable (ShelterOpsDesk.exe) cannot
# be rendered in this container; native Windows 11 execution is required for GUI
# verification. See repo/README.md for details.

FROM ubuntu:22.04 AS base

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    python3 \
    ca-certificates \
    libssl-dev \
    autoconf \
    autoconf-archive \
    automake \
    libtool \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
ENV VCPKG_ROOT=/vcpkg
RUN git clone https://github.com/microsoft/vcpkg "${VCPKG_ROOT}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
ENV PATH="${VCPKG_ROOT}:${PATH}"

WORKDIR /workspace/desktop

# Copy vcpkg manifest first to enable dependency layer caching
COPY vcpkg.json ./

# Install Linux-compatible packages (imgui/implot are Windows-only in vcpkg.json)
RUN vcpkg install --triplet x64-linux

# Promote vcpkg-downloaded cmake (4.x) over the system cmake (3.22) so that
# cmake_minimum_required(VERSION 3.28) is satisfied in subsequent RUN layers.
RUN cmake_bin="$(find /vcpkg/downloads/tools -name cmake -executable -type f 2>/dev/null | sort -V | tail -1)" \
    && [ -n "$cmake_bin" ] && ln -sf "$cmake_bin" /usr/local/bin/cmake \
    && cmake --version

# Copy full source
COPY . .

# ---------------------------------------------------------------------------
# Build stage: compile shelterops_lib and tests (no Win32/DX11 targets)
# ---------------------------------------------------------------------------
FROM base AS build
RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
    && cmake --build build --target shelterops_lib shelterops_unit_tests shelterops_api_tests
CMD ["cmake", "--build", "build", "--target", "shelterops_lib", "shelterops_unit_tests", "shelterops_api_tests"]

# ---------------------------------------------------------------------------
# Test stage: run CTest against the compiled test binaries
# CTEST_EXTRA_ARGS can be injected from run_tests.sh via --env to add
# label filters (e.g. "-L unit" or "-L api") or "--verbose".
# ---------------------------------------------------------------------------
FROM build AS test
ENV CTEST_EXTRA_ARGS="--output-on-failure --timeout 60"
CMD ["/bin/sh", "-lc", "ctest --test-dir build ${CTEST_EXTRA_ARGS}"]
