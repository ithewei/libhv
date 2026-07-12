#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "FileCache.h"
#include "hthread.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static bool write_file(const std::string& filepath, const std::string& content) {
    std::ofstream stream(filepath.c_str(), std::ios::binary | std::ios::trunc);
    stream.write(content.data(), content.size());
    return stream.good();
}

int main() {
    const std::string filepath = "libhv_file_cache_test_" + std::to_string(hv_getpid()) + ".txt";

    CHECK(write_file(filepath, "old"));

    FileCache cache;
    cache.stat_interval = -1; // force a stat check on every cache hit

    FileCache::OpenParam first_param;
    first_param.max_read = 1024;
    file_cache_ptr first = cache.Open(filepath.c_str(), &first_param);
    CHECK(first != NULL);
    CHECK(first->filebuf.len == 3);
    CHECK(std::memcmp(first->filebuf.base, "old", 3) == 0);

    CHECK(write_file(filepath, "new-content"));

    FileCache::OpenParam second_param;
    second_param.max_read = 1024;
    file_cache_ptr second = cache.Open(filepath.c_str(), &second_param);
    CHECK(second != NULL);
    CHECK(second != first);
    CHECK(second->filebuf.len == 11);
    CHECK(std::memcmp(second->filebuf.base, "new-content", 11) == 0);

    // A refreshed entry must not mutate buffers retained by in-flight responses.
    CHECK(first->filebuf.len == 3);
    CHECK(std::memcmp(first->filebuf.base, "old", 3) == 0);

    CHECK(std::remove(filepath.c_str()) == 0);

    FileCache::OpenParam missing_param;
    file_cache_ptr missing = cache.Open(filepath.c_str(), &missing_param);
    CHECK(missing == NULL);
    CHECK(missing_param.error != 0);

    // A failed refresh must invalidate the old entry instead of serving it
    // until the next stat interval.
    cache.stat_interval = 60;
    FileCache::OpenParam repeated_missing_param;
    file_cache_ptr repeated_missing = cache.Open(filepath.c_str(), &repeated_missing_param);
    CHECK(repeated_missing == NULL);
    CHECK(repeated_missing_param.error != 0);
    return 0;
}
