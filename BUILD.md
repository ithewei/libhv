## Prequired

- c99
- c++11

gcc4.8+, msvc2015 or later

## Makefile
options see [config.ini](config.ini)
```
./configure --with-openssl
make
sudo make install
```

## cmake
options see [CMakeLists.txt](CMakeLists.txt)
```
mkdir build
cd build
cmake .. -DWITH_OPENSSL=ON
cmake --build .
```

## Unix
use Makefile or cmake

## Windows
use cmake
```
mkdir win64
cd win64
cmake .. -G "Visual Studio 15 2017 Win64"
#cmake .. -G "Visual Studio 16 2019" -A x64
#cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build .
```

## CROSS_COMPILE
use Makefile
```
sudo apt install gcc-arm-linux-gnueabi g++-arm-linux-gnueabi # ubuntu
export CROSS_COMPILE=arm-linux-gnueabi-
./configure
make clean
make libhv
```
or use cmake
```
mkdir build
cd build
cmake .. -DCMAKE_C_COMPILER=arm-linux-gnueabi-gcc -DCMAKE_CXX_COMPILER=arm-linux-gnueabi-g++
cmake --build . --target libhv libhv_static
```

### mingw
see CROSS_COMPILE
```
sudo apt install mingw-w64 # ubuntu
#export CROSS_COMPILE=i686-w64-mingw32-
export CROSS_COMPILE=x86_64-w64-mingw32-
./configure
make clean
make libhv
```

### Android
see CROSS_COMPILE
```
#https://developer.android.com/ndk/downloads
#export NDK_ROOT=~/Downloads/android-ndk-r21b
#sudo $NDK_ROOT/build/tools/make-standalone-toolchain.sh --arch=arm   --platform=android-21 --install-dir=/opt/ndk/arm
#sudo $NDK_ROOT/build/tools/make-standalone-toolchain.sh --arch=arm64 --platform=android-21 --install-dir=/opt/ndk/arm64
#export PATH=/opt/ndk/arm/bin:/opt/ndk/arm64/bin:$PATH
#export CROSS_COMPILE=arm-linux-androideabi-
export CROSS_COMPILE=aarch64-linux-android-
./configure
make clean
make libhv
```

### iOS
```
mkdir build
cd build
cmake .. -G Xcode -DCMAKE_TOOLCHAIN_FILE=../cmake/ios.toolchain.cmake -DPLATFORM=OS -DDEPLOYMENT_TARGET=9.0 -DARCHS="arm64"
cmake --build . --target hv_static --config Release
```

## targets

### lib
- make libhv

### examples
- make examples

### unittest
- make unittest

## options

### compile without c++
```
./configure --without-evpp
make clean && make libhv
```

### compile WITH_OPENSSL
Enable SSL/TLS in libhv is so easy :)
```
// see ssl/hssl.h
hssl_ctx_t hssl_ctx_new(hssl_ctx_opt_t* opt);

// see event/hloop.h
int hio_new_ssl_ctx(hio_t* io, hssl_ctx_opt_t* opt);
```

https is the best example.
```
sudo apt install openssl libssl-dev # ubuntu
./configure --with-openssl
make clean && make
bin/httpd -s restart -d
bin/curl -v http://localhost:8080
bin/curl -v https://localhost:8443
```

### compile WITH_CURL
```
./configure --with-curl
make clean && make
bin/httpd -s restart -d
bin/curl -v http://localhost:8080
```

### compile WITH_NGHTTP2
```
sudo apt install libnghttp2-dev # ubuntu
./configure --with-nghttp2
make clean && make
bin/httpd -s restart -d
bin/curl -v http://localhost:8080 --http2
```

### compile WITH_KCP
```
./configure --with-kcp
make clean && make
```

### compile WITH_MQTT
```
./configure --with-mqtt
make clean && make
```

### More
```
./configure --help
```
