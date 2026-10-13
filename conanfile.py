from conans import ConanFile, CMake, tools


class LibhvConan(ConanFile):
    name = "libhv"
    version = "1.3.4"
    license = "BSD-3-Clause"
    url = "https://github.com/ithewei/libhv"
    homepage = "https://github.com/ithewei/libhv"
    description = "Like libevent, libev, and libuv, libhv provides event-loop with non-blocking IO and timer, but simpler api and richer protocols."
    topics = ("http", "websocket", "http-server", "http-client", "event-loop",
              "network", "tcp", "udp", "ssl", "mqtt")
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_protocol": [True, False],
        "with_evpp": [True, False],
        "with_http": [True, False],
        "with_http_server": [True, False],
        "with_http_client": [True, False],
        "with_mqtt": [True, False],
        "with_nghttp2": [True, False],
        "with_openssl": [True, False],
        "with_gnutls": [True, False],
        "with_mbedtls": [True, False],
        "enable_uds": [True, False],
        "use_multimap": [True, False],
        "with_kcp": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_protocol": False,
        "with_evpp": True,
        "with_http": True,
        "with_http_server": True,
        "with_http_client": True,
        "with_mqtt": False,
        "with_nghttp2": False,
        "with_openssl": False,
        "with_gnutls": False,
        "with_mbedtls": False,
        "enable_uds": False,
        "use_multimap": False,
        "with_kcp": False,
    }
    generators = "cmake", "cmake_find_package"
    exports_sources = (
        "CMakeLists.txt", "hconfig.h.in", "hv.h", "hexport.h", "hv.rc.in",
        "base/*", "ssl/*", "event/*", "util/*", "cpputil/*", "evpp/*",
        "protocol/*", "http/*", "mqtt/*", "cmake/*",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            del self.options.fPIC

    def requirements(self):
        if self.options.with_openssl:
            self.requires("openssl/1.1.1q")
        if self.options.with_nghttp2:
            self.requires("nghttp2/1.49.0")
        if self.options.with_mbedtls:
            self.requires("mbedtls/3.2.1")

    def _configure_cmake(self):
        cmake = CMake(self)
        cmake.definitions["BUILD_SHARED"] = self.options.shared
        cmake.definitions["BUILD_STATIC"] = not self.options.shared
        cmake.definitions["BUILD_EXAMPLES"] = False
        cmake.definitions["BUILD_UNITTEST"] = False
        cmake.definitions["WITH_PROTOCOL"] = self.options.with_protocol
        cmake.definitions["WITH_EVPP"] = self.options.with_evpp
        cmake.definitions["WITH_HTTP"] = self.options.with_http
        cmake.definitions["WITH_HTTP_SERVER"] = self.options.with_http_server
        cmake.definitions["WITH_HTTP_CLIENT"] = self.options.with_http_client
        cmake.definitions["WITH_MQTT"] = self.options.with_mqtt
        cmake.definitions["WITH_NGHTTP2"] = self.options.with_nghttp2
        cmake.definitions["WITH_OPENSSL"] = self.options.with_openssl
        cmake.definitions["WITH_GNUTLS"] = self.options.with_gnutls
        cmake.definitions["WITH_MBEDTLS"] = self.options.with_mbedtls
        cmake.definitions["ENABLE_UDS"] = self.options.enable_uds
        cmake.definitions["USE_MULTIMAP"] = self.options.use_multimap
        cmake.definitions["WITH_KCP"] = self.options.with_kcp
        cmake.configure()
        return cmake

    def build(self):
        cmake = self._configure_cmake()
        cmake.build()

    def package(self):
        self.copy("LICENSE", dst="licenses")
        cmake = self._configure_cmake()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["hv" if self.options.shared else "hv_static"]
        if not self.options.shared:
            self.cpp_info.defines.append("HV_STATICLIB")
        if self.settings.os == "Linux":
            self.cpp_info.system_libs.extend(["pthread", "m", "dl"])
            if tools.os_info.is_linux:
                self.cpp_info.system_libs.append("rt")
        if self.settings.os == "Windows":
            self.cpp_info.system_libs.extend([
                "secur32", "crypt32", "winmm", "iphlpapi", "ws2_32",
            ])
        if self.settings.os == "Macos":
            self.cpp_info.frameworks.extend(["CoreFoundation", "Security"])
        if self.options.with_openssl:
            self.cpp_info.defines.append("WITH_OPENSSL")
        if self.options.with_gnutls:
            self.cpp_info.defines.append("WITH_GNUTLS")
        if self.options.with_mbedtls:
            self.cpp_info.defines.append("WITH_MBEDTLS")
        if self.options.with_nghttp2:
            self.cpp_info.defines.append("WITH_NGHTTP2")
        if self.options.enable_uds:
            self.cpp_info.defines.append("ENABLE_UDS")
        if self.options.use_multimap:
            self.cpp_info.defines.append("USE_MULTIMAP")
