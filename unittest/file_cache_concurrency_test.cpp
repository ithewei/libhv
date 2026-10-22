#include "FileCache.h"

#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

int main() {
    const std::string path = "/tmp/hv-file-cache-concurrency-" + std::to_string(getpid()) + ".txt";
    {
        std::ofstream out(path.c_str(), std::ios::binary);
        out << "file-cache-concurrency";
    }

    FileCache cache;
    const size_t thread_count = 8;
    std::vector<file_cache_ptr> results(thread_count);
    std::vector<std::thread> workers;
    std::mutex start_mutex;
    std::condition_variable start_cv;
    size_t ready = 0;
    bool start = false;

    for (size_t i = 0; i < thread_count; ++i) {
        workers.emplace_back([&, i]() {
            {
                std::unique_lock<std::mutex> lock(start_mutex);
                ++ready;
                start_cv.notify_all();
                start_cv.wait(lock, [&]() { return start; });
            }
            FileCache::OpenParam param;
            results[i] = cache.Open(path.c_str(), &param);
        });
    }

    {
        std::unique_lock<std::mutex> lock(start_mutex);
        start_cv.wait(lock, [&]() { return ready == thread_count; });
        start = true;
    }
    start_cv.notify_all();
    for (auto& worker : workers) worker.join();

    bool ok = results[0] != NULL;
    for (size_t i = 1; i < results.size(); ++i) {
        ok = ok && results[i] == results[0];
    }

    std::remove(path.c_str());
    return ok ? 0 : 1;
}
