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
INSTALL_DIR=$THIRDPARTY_DIR/install
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$INSTALL_DIR

cd $THIRDPARTY_DIR
# Make the destination directory for all thirdparty libraries
mkdir -p $INSTALL_DIR

# fmt
sudo apt install -y libfmt-dev

# libpq
sudo apt install -y libpq-dev

# libtbb
sudo apt-get install -y libtbb-dev

# openjdk
sudo apt install -y openjdk-17-jdk

# grpc (& re2 for pkg-conf resolution)
if [ ! -e $INSTALL_DIR/lib/libgrpc.a ]; then
    sudo apt install -y cmake
    sudo apt install -y build-essential autoconf libtool pkg-config libssl-dev libtbb-dev libnuma-dev
    if [ ! -e "grpc" ]; then
          git clone --recurse-submodules -b v1.64.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc
    fi
    cd grpc
    mkdir -p cmake/build
    pushd cmake/build
    cmake -DgRPC_INSTALL=ON \
          -DgRPC_BUILD_TESTS=OFF \
          -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
          ../..
    make -j 4
    make install
    popd
    cd third_party/re2
    sed -i 's|prefix=/usr/local|prefix?=/usr/local|' Makefile
    make prefix=$INSTALL_DIR
    make install prefix=$INSTALL_DIR
    cd ../../.. 
fi

# jemmalloc
if [ ! -e $INSTALL_DIR/lib/libjemalloc.a ]; then
    sudo apt-get install -y autoconf
    if [ ! -e jemalloc ]; then
        git clone https://github.com/jemalloc/jemalloc.git
    fi
    cd jemalloc
    ./autogen.sh --prefix=$INSTALL_DIR
    make
    make install
    cd ..
fi

# boost
if [ ! -e $INSTALL_DIR/lib/libboost_atomic.a ]; then
    if [ ! -e boost_1_88_0 ]; then
        wget https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.gz
        tar -xzf boost_1_88_0.tar.gz
        rm boost_1_88_0.tar.gz
    fi
    cd boost_1_88_0
    ./bootstrap.sh --prefix=$INSTALL_DIR
    ./b2 install
    cd ..
fi

# rocksdb
if [ ! -e $INSTALL_DIR/lib/librocksdb.a ]; then 
    sudo apt-get install -y libgflags-dev libsnappy-dev zlib1g-dev libbz2-dev liblz4-dev libzstd-dev liburing-dev
    if [ ! -e rocksdb ]; then
        git clone https://github.com/facebook/rocksdb.git
    fi
    cd rocksdb
    make install USE_RTTI=1 PREFIX=$INSTALL_DIR -j4
    cd ..
fi

# antlr4 jar
if [ ! -e $INSTALL_DIR/antlr-4.13.2-complete.jar ]; then 
    cd install
    curl -L -O https://www.antlr.org/download/antlr-4.13.2-complete.jar
    cd ..
fi

# antlr4 cpp runtime
if [ ! -e $INSTALL_DIR/lib/libantlr4-runtime.a ]; then
    if [ ! -e antlr4-4.13.2 ]; then
        curl -L -O https://github.com/antlr/antlr4/archive/refs/tags/4.13.2.zip
        unzip 4.13.2.zip
        rm 4.13.2.zip
    fi
    cd antlr4-4.13.2/runtime/Cpp
    mkdir build && cd build
    cmake -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DANTLR4_INSTALL=ON ..
    make install
    cd ../../../..
fi

if [ ! -e $INSTALL_DIR/lib/libaws-cpp-sdk-core.so ]; then
    sudo apt-get install -y libcurl4-gnutls-dev
    if [ ! -e aws-sdk-cpp ]; then
        git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp
    fi
    cd aws-sdk-cpp
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DBUILD_ONLY="s3" ..
    cmake --build . --config=Debug
    cmake --install . --config=Debug
    cd ../..
fi

# install duckdb
# curl https://install.duckdb.org | sh

# python packages (Faker and Cheetah)
sudo apt install -y python3
sudo apt install -y python3-pip
pip install fsspec
pip install duckdb

# Worry about libpqxx later
# libpqxx

echo "Installed all third party libraries!"