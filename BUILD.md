## Prequired

- c99
- c++11

gcc4.8+, msvc2015 or later

## Makefile
options modify [config.mk](config.mk)
```
./configure
make
sudo make install
```

## cmake
options modify [CMakeLists.txt](CMakeLists.txt)
```
mkdir build
cd build
cmake ..
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

### android
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

## targets

### lib
- make libhv

### examples
- make examples

### unittest
- make unittest

## options

### compile WITH_OPENSSL
Enable SSL in libhv is so easy, just only two apis:
```
// init ssl_ctx, see base/hssl.h
hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param);

// enable ssl, see event/hloop.h
int hio_enable_ssl(hio_t* io);
```

https is the best example.
```
sudo apt install openssl libssl-dev # ubuntu
make clean
make WITH_OPENSSL=yes
# editor etc/httpd.conf => ssl = on
bin/httpd -s restart -d
bin/curl -v https://localhost:8080
curl -v https://localhost:8080 --insecure
```

### compile WITH_CURL
```
make WITH_CURL=yes DEFINES="CURL_STATICLIB"
```

### compile WITH_NGHTTP2
```
sudo apt install libnghttp2-dev # ubuntu
make clean
make WITH_NGHTTP2=yes
bin/httpd -d
bin/curl -v localhost:8080 --http2
```
