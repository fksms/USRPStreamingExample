#!/bin/bash

if [ $1 = "debug" ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build
else
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
fi