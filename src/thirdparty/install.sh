#!/bin/bash

# List of libraries that need to be installed
# fmt
# libpq
# grpc
# re2
# boost >= 1.83.0
# jemmalloc
# rocksdb with USE_RTTI=1

set -e

# Prevent apt from opening interactive dialog boxes
export DEBIAN_FRONTEND=noninteractive

THIRDPARTY_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_DIR="$THIRDPARTY_DIR/install"
export LD_LIBRARY_PATH="$INSTALL_DIR/lib:$INSTALL_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cd "$THIRDPARTY_DIR"
# Make the destination directory for all thirdparty libraries
mkdir -p "$INSTALL_DIR"

# Determine the number of parallel threads to use
NUM_JOBS="${NUM_JOBS:-${JOBS:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}}"

# Avoid OOM on high-core machines with constrained memory:
# Allow at most 1 job per ~1.5 GB of RAM (minimum 1 job)
if [ -f /proc/meminfo ]; then
    MEM_TOTAL_KB=$(awk '/MemTotal:/ {print $2}' /proc/meminfo)
    if [ -n "$MEM_TOTAL_KB" ] && [ "$MEM_TOTAL_KB" -gt 0 ]; then
        MEM_MAX_JOBS=$(( MEM_TOTAL_KB / 1572864 ))
        if [ "$MEM_MAX_JOBS" -lt 1 ]; then
            MEM_MAX_JOBS=1
        fi
        if [ "$NUM_JOBS" -gt "$MEM_MAX_JOBS" ]; then
            echo "Memory constrained: adjusting parallel jobs from $NUM_JOBS to $MEM_MAX_JOBS to prevent OOM"
            NUM_JOBS=$MEM_MAX_JOBS
        fi
    fi
fi

export CMAKE_BUILD_PARALLEL_LEVEL="$NUM_JOBS"
echo "Using $NUM_JOBS parallel threads for builds"

# Consolidate all system package dependencies
APT_DEPS=(
    libfmt-dev
    libpq-dev
    libtbb-dev
    openjdk-17-jdk
    build-essential
    autoconf
    libtool
    pkg-config
    libssl-dev
    libnuma-dev
    libgflags-dev
    libsnappy-dev
    zlib1g-dev
    libbz2-dev
    liblz4-dev
    libzstd-dev
    liburing-dev
    python3
    python3-pip
)

# Check if cmake is already present (e.g. via snap or manual install)
if ! command -v cmake >/dev/null 2>&1 && ! dpkg -s cmake >/dev/null 2>&1; then
    APT_DEPS+=(cmake)
fi

# Pick libcurl dev package (use whichever is already present, default to libcurl4-openssl-dev)
if ! dpkg -s libcurl4-openssl-dev >/dev/null 2>&1 && ! dpkg -s libcurl4-gnutls-dev >/dev/null 2>&1; then
    APT_DEPS+=(libcurl4-openssl-dev)
fi

MISSING_PKGS=()
for pkg in "${APT_DEPS[@]}"; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        MISSING_PKGS+=("$pkg")
    fi
done

if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    echo "Installing missing system packages: ${MISSING_PKGS[*]}"
    sudo apt-get install -y "${MISSING_PKGS[@]}"
fi

# grpc (& re2 for pkg-conf resolution)
if [ ! -e "$INSTALL_DIR/lib/libgrpc.a" ]; then
    if [ ! -e "grpc" ]; then
        git clone --recurse-submodules --jobs "$NUM_JOBS" -b v1.64.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc
    fi
    cd grpc
    mkdir -p cmake/build
    pushd cmake/build
    cmake -DgRPC_INSTALL=ON \
          -DgRPC_BUILD_TESTS=OFF \
          -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
          ../..
    make -j "$NUM_JOBS"
    make install
    popd
    cd third_party/re2
    sed -i 's|prefix=/usr/local|prefix?=/usr/local|' Makefile
    make -j "$NUM_JOBS" prefix="$INSTALL_DIR"
    make install prefix="$INSTALL_DIR"
    cd "$THIRDPARTY_DIR"
fi

# jemmalloc
if [ ! -e "$INSTALL_DIR/lib/libjemalloc.a" ]; then
    if [ ! -e jemalloc ]; then
        git clone https://github.com/jemalloc/jemalloc.git
    fi
    cd jemalloc
    ./autogen.sh --prefix="$INSTALL_DIR"
    make -j "$NUM_JOBS"
    make install
    cd "$THIRDPARTY_DIR"
fi

# boost
if [ ! -e "$INSTALL_DIR/lib/libboost_atomic.a" ]; then
    if [ ! -e boost_1_88_0 ]; then
        wget https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.gz
        tar -xzf boost_1_88_0.tar.gz
        rm boost_1_88_0.tar.gz
    fi
    cd boost_1_88_0
    ./bootstrap.sh --prefix="$INSTALL_DIR"
    ./b2 -j "$NUM_JOBS" install
    cd "$THIRDPARTY_DIR"
fi

# rocksdb
if [ ! -e "$INSTALL_DIR/lib/librocksdb.a" ]; then 
    if [ ! -e rocksdb ]; then
        git clone https://github.com/facebook/rocksdb.git
    fi
    cd rocksdb
    make -j "$NUM_JOBS" install USE_RTTI=1 PREFIX="$INSTALL_DIR"
    cd "$THIRDPARTY_DIR"
fi

# antlr4 jar
if [ ! -e "$INSTALL_DIR/antlr-4.13.2-complete.jar" ]; then 
    curl -L -o "$INSTALL_DIR/antlr-4.13.2-complete.jar" https://www.antlr.org/download/antlr-4.13.2-complete.jar
fi

# antlr4 cpp runtime
if [ ! -e "$INSTALL_DIR/lib/libantlr4-runtime.a" ]; then
    if [ ! -e antlr4-4.13.2 ]; then
        curl -L -O https://github.com/antlr/antlr4/archive/refs/tags/4.13.2.zip
        unzip 4.13.2.zip
        rm 4.13.2.zip
    fi
    cd antlr4-4.13.2/runtime/Cpp
    mkdir -p build && cd build
    cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DANTLR4_INSTALL=ON ..
    make -j "$NUM_JOBS"
    make install
    cd "$THIRDPARTY_DIR"
fi

if [ ! -e "$INSTALL_DIR/lib/libaws-cpp-sdk-core.so" ]; then
    if [ ! -e aws-sdk-cpp ]; then
        git clone --recurse-submodules --jobs "$NUM_JOBS" https://github.com/aws/aws-sdk-cpp
    fi
    cd aws-sdk-cpp
    mkdir -p build && cd build
    cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_ONLY="s3" ..
    cmake --build . --config=Debug -j "$NUM_JOBS"
    cmake --install . --config=Debug
    cd "$THIRDPARTY_DIR"
fi

# install duckdb
# curl https://install.duckdb.org | sh

# python packages (Faker and Cheetah)
pip install fsspec duckdb || pip3 install fsspec duckdb

# Worry about libpqxx later
# libpqxx

echo "Installed all third party libraries!"