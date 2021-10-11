# Build

## Install Qt
Download from <https://download.qt.io/archive/qt/> and install.

Add qmake and mingw toolchain to the environment variable PATH, for example:<br>
`C:\Qt\Qt5.14.2\5.14.2\mingw73_64\bin`,
`C:\Qt\Qt5.14.2\Tools\mingw730_64\bin`


## Install cmake
Download from <https://cmake.org/download/> and install.

Add cmake/bin to the environment variable PATH, for example:<br>
`C:\Program Files\CMake\bin`

## Build libhv
```shell
git clone https://github.com/ithewei/libhv
cd libhv
cmake . -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=mingw32-make -Bbuild/mingw64
cmake --build build/mingw64
```

## Build examples/qt
```shell
cd examples/qt/server
qmake
mingw32-make
windeployqt.exe release
```

```shell
cd examples/qt/client
qmake
mingw32-make
windeployqt.exe release
```
