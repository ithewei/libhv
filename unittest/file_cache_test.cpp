#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "FileCache.h"
#include "herr.h"
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

    // The default OpenParam limit follows the owning cache instance.
    const std::string limit_filepath = filepath + ".limit";
    CHECK(write_file(limit_filepath, "12345"));
    FileCache limited_cache;
    limited_cache.SetMaxFileSize(4);
    FileCache::OpenParam limited_param;
    CHECK(limited_cache.Open(limit_filepath.c_str(), &limited_param) == NULL);
    CHECK(limited_param.error == ERR_OVER_LIMIT);
    FileCache::OpenParam override_param;
    override_param.max_read = 5;
    file_cache_ptr overridden = limited_cache.Open(limit_filepath.c_str(), &override_param);
    CHECK(overridden != NULL);
    CHECK(overridden->filebuf.len == 5);
    CHECK(limited_cache.Close(limit_filepath.c_str()));
    limited_cache.SetMaxFileSize(5);
    FileCache::OpenParam allowed_param;
    file_cache_ptr allowed = limited_cache.Open(limit_filepath.c_str(), &allowed_param);
    CHECK(allowed != NULL);
    CHECK(allowed->filebuf.len == 5);
    {
        std::lock_guard<std::mutex> lock(allowed->mutex);
        CHECK(!allowed->prepend_header("oversized", allowed->header_reserve + 1));
        CHECK(allowed->httpbuf.base == allowed->filebuf.base);
        CHECK(allowed->httpbuf.len == allowed->filebuf.len);
        CHECK(allowed->header_used == 0);
    }
    CHECK(std::remove(limit_filepath.c_str()) == 0);

    // Concurrent cold misses for one path must reuse the same published entry.
    const std::string concurrent_filepath = filepath + ".concurrent";
    const std::string concurrent_content(static_cast<size_t>(2) * 1024 * 1024, 'x');
    CHECK(write_file(concurrent_filepath, concurrent_content));
    FileCache concurrent_cache;
    concurrent_cache.SetMaxFileSize(4 * 1024 * 1024);
    const size_t thread_count = 8;
    std::vector<file_cache_ptr> results(thread_count);
    std::vector<std::thread> threads;
    std::mutex start_mutex;
    std::condition_variable start_cv;
    size_t ready = 0;
    bool start = false;
    for (size_t i = 0; i < thread_count; ++i) {
        threads.push_back(std::thread([&, i]() {
            {
                std::unique_lock<std::mutex> lock(start_mutex);
                ++ready;
                start_cv.notify_all();
                start_cv.wait(lock, [&]() { return start; });
            }
            FileCache::OpenParam param;
            results[i] = concurrent_cache.Open(concurrent_filepath.c_str(), &param);
        }));
    }
    {
        std::unique_lock<std::mutex> lock(start_mutex);
        start_cv.wait(lock, [&]() { return ready == thread_count; });
        start = true;
    }
    start_cv.notify_all();
    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }
    CHECK(results[0] != NULL);
    for (size_t i = 1; i < results.size(); ++i) {
        CHECK(results[i] == results[0]);
    }

    // A concurrent refresh also publishes one replacement and preserves the
    // old buffer held by in-flight responses.
    concurrent_cache.stat_interval = -1;
    CHECK(write_file(concurrent_filepath, "refreshed"));
    std::vector<file_cache_ptr> refreshed_results(thread_count);
    threads.clear();
    ready = 0;
    start = false;
    for (size_t i = 0; i < thread_count; ++i) {
        threads.push_back(std::thread([&, i]() {
            {
                std::unique_lock<std::mutex> lock(start_mutex);
                ++ready;
                start_cv.notify_all();
                start_cv.wait(lock, [&]() { return start; });
            }
            FileCache::OpenParam param;
            refreshed_results[i] = concurrent_cache.Open(concurrent_filepath.c_str(), &param);
        }));
    }
    {
        std::unique_lock<std::mutex> lock(start_mutex);
        start_cv.wait(lock, [&]() { return ready == thread_count; });
        start = true;
    }
    start_cv.notify_all();
    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }
    CHECK(refreshed_results[0] != NULL);
    CHECK(refreshed_results[0] != results[0]);
    CHECK(refreshed_results[0]->filebuf.len == 9);
    CHECK(std::memcmp(refreshed_results[0]->filebuf.base, "refreshed", 9) == 0);
    for (size_t i = 1; i < refreshed_results.size(); ++i) {
        CHECK(refreshed_results[i] == refreshed_results[0]);
    }
    CHECK(results[0]->filebuf.len == concurrent_content.size());
    CHECK(std::memcmp(results[0]->filebuf.base, concurrent_content.data(), concurrent_content.size()) == 0);
    CHECK(std::remove(concurrent_filepath.c_str()) == 0);
    return 0;
}
