#!/bin/bash

docker run --rm \
-v ~/workspace:/workspace \
-w /workspace/robot_control \
rk3588-cross \
bash -c "
mkdir -p build &&
cd build &&
cmake \
-DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux.cmake \
.. &&
make -j8
"
