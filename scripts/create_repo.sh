#!/bin/bash

mkdir -p include lib src bin doc etc 3rd/include 3rd/lib dist
touch README.md BUILD.md RELEASE.md CHANGELOG.md Makefile .gitignore
git init

# personal
git submodule add https://github.com/ithewei/libhv.git src/hv
cp src/hv/.gitignore .
cp src/hv/.clang-format .
cp src/hv/Makefile .
cp -r src/hv/etc/* etc
cp src/hv/examples/hmain_test.cpp src/main.cpp
