from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import copy
import os


class LibhvConan(ConanFile):
    name = "libhv"
    version = "1.3.3"
    license = "BSD-3-Clause"
    description = "C/C++ network library for TCP/UDP/SSL/HTTP/WebSocket/MQTT client/server (Gastronomous fork)"
    url = "https://github.com/Gastronomous-Technologies/libhv"
    settings = "os", "compiler", "build_type", "arch"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_openssl": [True, False],
        "with_http": [True, False],
        "with_mqtt": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_openssl": True,
        "with_http": True,
        "with_mqtt": True,
    }

    exports_sources = (
        "CMakeLists.txt", "cmake/*",
        "*.h", "*.h.in",
        "base/*", "ssl/*", "event/*", "util/*", "cpputil/*",
        "evpp/*", "protocol/*", "http/*", "mqtt/*",
        "LICENSE", "README.md",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def requirements(self):
        if self.options.with_openssl:
            self.requires("openssl/[>=1.1 <4]")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeDeps(self).generate()
        tc = CMakeToolchain(self)
        tc.variables["BUILD_SHARED"] = bool(self.options.shared)
        tc.variables["BUILD_STATIC"] = not bool(self.options.shared)
        tc.variables["BUILD_EXAMPLES"] = False
        tc.variables["BUILD_UNITTEST"] = False
        tc.variables["WITH_EVPP"] = True  # required for the C++ HTTP API
        tc.variables["WITH_HTTP"] = bool(self.options.with_http)
        tc.variables["WITH_HTTP_SERVER"] = bool(self.options.with_http)
        tc.variables["WITH_HTTP_CLIENT"] = bool(self.options.with_http)
        tc.variables["WITH_MQTT"] = bool(self.options.with_mqtt)
        tc.variables["WITH_OPENSSL"] = bool(self.options.with_openssl)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["hv"] if self.options.shared else ["hv_static"]
        if not self.options.shared:
            self.cpp_info.defines = ["HV_STATICLIB"]
        # libhv installs its headers under <prefix>/include/hv
        self.cpp_info.includedirs = ["include"]
        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.system_libs = ["pthread", "m", "dl", "rt"]
