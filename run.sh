#!/bin/sh

# rm -rf build
mkdir build
cd build
cmake ..
cmake --build ..
make
cd ..
build/castor