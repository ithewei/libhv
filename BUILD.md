## Required

- c99
- c++11

gcc4.8+, msvc2015+

## Dependencies
modify config.mk

## Unix
```
./configure
make
sudo make install
```

## CROSS_COMPILE
```
export CROSS_COMPILE=arm-linux-androideabi-
./configure
make
```

### mingw
see CROSS_COMPILE

For example:
```
sudo apt-get install mingw-w64 # ubuntu
#export CROSS_COMPILE=i686-w64-mingw32-
export CROSS_COMPILE=x86_64-w64-mingw32-
./configure
make
```

## cmake
```
mkdir build
cd build
cmake ..
make libhv
make libhv_static
make unittest
make examples
```
