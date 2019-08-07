#!/bin/bash

mkdir -p include lib src bin doc etc 3rd/include 3rd/lib dist
touch README.md BUILD.md RELEASE.md CHANGELOG.md Makefile .gitignore
git init

# personal
git submodule add https://github.com/ithewei/hw.git src/hw
cp src/hw/.gitignore .
cp src/hw/Makefile .
cp -r src/hw/etc/* etc
cp src/hw/main.cpp.tmpl src/main.cpp

make
