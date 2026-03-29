# FileCacheEx — Enhanced File Cache

`FileCacheEx` is a drop-in replacement for `FileCache` that fixes thread-safety issues and provides runtime-configurable parameters.

## Header

```c++
#include "FileCacheEx.h"
```

## Improvements over FileCache

| Feature | FileCache | FileCacheEx |
|---------|-----------|-------------|
| HTTP header reserve | Compile-time 1024 bytes | Runtime configurable, default 4096 |
| `prepend_header()` | Returns `void`, silently drops overflow | Returns `bool`, `false` on overflow |
| Per-entry thread safety | No locking | Per-entry `std::mutex` |
| `resize_buf()` httpbuf | May leave dangling reference | Explicitly invalidated (UAF prevention) |
| File read | Single `read()`, may short-read | Loop with `EINTR` handling |
| Cache insertion | Before initialization completes | Deferred until fully initialized |
| Max file size | Compile-time `FILE_CACHE_MAX_SIZE` | Runtime `SetMaxFileSize()` |
| DLL export | None | `HV_EXPORT` |

## Data Structures

```c++
typedef struct file_cache_ex_s {
    mutable std::mutex  mutex;          // per-entry lock
    std::string         filepath;
    struct stat         st;
    time_t              open_time;
    time_t              stat_time;
    uint32_t            stat_cnt;
    HBuf                buf;            // header_reserve + file_content
    hbuf_t              filebuf;        // points into buf: file content
    hbuf_t              httpbuf;        // points into buf: header + content
    char                last_modified[64];
    char                etag[64];
    std::string         content_type;
    int                 header_reserve; // bytes reserved before content
    int                 header_used;    // bytes actually used by header

    bool is_modified();                         // caller must hold mutex
    bool is_complete();                         // caller must hold mutex
    void resize_buf(size_t filesize, int reserved);  // caller must hold mutex
    void resize_buf(size_t filesize);
    bool prepend_header(const char* header, int len); // thread-safe

    // Thread-safe accessors
    int  get_header_reserve()   const;
    int  get_header_used()      const;
    int  get_header_remaining() const;
    bool header_fits(int len)   const;
} file_cache_ex_t;

typedef std::shared_ptr<file_cache_ex_t> file_cache_ex_ptr;
```

## FileCacheEx Class

```c++
class HV_EXPORT FileCacheEx : public hv::LRUCache<std::string, file_cache_ex_ptr> {
public:
    int stat_interval;      // seconds between stat() checks, default 10
    int expired_time;       // seconds before expiry, default 60
    int max_header_length;  // header reserve per entry, default 4096
    int max_file_size;      // max cached file size, default 4MB

    explicit FileCacheEx(size_t capacity = 100);

    file_cache_ex_ptr Open(const char* filepath, OpenParam* param);
    bool Exists(const char* filepath) const;
    bool Close(const char* filepath);
    void RemoveExpiredFileCache();

    int  GetMaxHeaderLength()   const;
    int  GetMaxFileSize()       const;
    int  GetStatInterval()      const;
    int  GetExpiredTime()       const;

    void SetMaxHeaderLength(int len);
    void SetMaxFileSize(int size);
};
```

## Usage

```c++
FileCacheEx filecache(200);              // LRU capacity = 200
filecache.SetMaxHeaderLength(8192);      // 8K header reserve
filecache.SetMaxFileSize(1 << 24);       // 16MB max file
filecache.stat_interval = 5;
filecache.expired_time = 120;

FileCacheEx::OpenParam param;
file_cache_ex_ptr fc = filecache.Open("/var/www/index.html", &param);
if (fc) {
    std::string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
    if (fc->prepend_header(header.c_str(), header.size())) {
        // send fc->httpbuf (header + content)
    } else {
        // header exceeds reserve, fall back to separate send
    }
}
```

## Migration from FileCache

1. Replace `#include "FileCache.h"` with `#include "FileCacheEx.h"`
2. Change types: `FileCache` → `FileCacheEx`, `file_cache_ptr` → `file_cache_ex_ptr`
3. Handle `bool` return from `prepend_header()`
4. Optional: configure via `SetMaxHeaderLength()` / `SetMaxFileSize()`

## Thread Safety

- `FileCacheEx` inherits `hv::LRUCache` global mutex for LRU operations
- Each `file_cache_ex_s` entry has its own `std::mutex` for entry-level protection
- `prepend_header()` locks automatically; callers need no external synchronization
- `is_modified()`, `is_complete()`, `resize_buf()` require caller to hold the mutex
