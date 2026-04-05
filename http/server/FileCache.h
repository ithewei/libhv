#ifndef HV_FILE_CACHE_H_
#define HV_FILE_CACHE_H_

/*
 * FileCache — Enhanced File Cache for libhv HTTP server
 *
 * Features:
 *   1. Configurable max_header_length (default 4096, tunable per-instance)
 *   2. prepend_header() returns bool to report success/failure
 *   3. Exposes header/buffer metrics via accessors
 *   4. Fixes stat() name collision in is_modified()
 *   5. max_cache_num / max_file_size configurable at runtime
 *   6. Reserved header space can be tuned per-instance
 *   7. Source-level API compatible; struct layout differs from original (no ABI/layout compatibility)
 */

#include <memory>
#include <string>
#include <mutex>

#include "hexport.h"
#include "hbuf.h"
#include "hstring.h"
#include "LRUCache.h"

// Default values — may be overridden at runtime via FileCache setters
#define FILE_CACHE_DEFAULT_HEADER_LENGTH  4096        // 4K
#define FILE_CACHE_DEFAULT_MAX_NUM        100
#define FILE_CACHE_DEFAULT_MAX_FILE_SIZE  (1 << 22)   // 4M

typedef struct file_cache_s {
    mutable std::mutex  mutex;      // protects all mutable state below
    std::string filepath;
    struct stat st;
    time_t      open_time;
    time_t      stat_time;
    uint32_t    stat_cnt;
    HBuf        buf;        // header_reserve + file_content
    hbuf_t      filebuf;    // points into buf: file content region
    hbuf_t      httpbuf;    // points into buf: header + file content after prepend
    char        last_modified[64];
    char        etag[64];
    std::string content_type;

    // --- new: expose header metrics ---
    int         header_reserve;     // reserved bytes before file content
    int         header_used;        // actual bytes used by prepend_header

    file_cache_s() {
        stat_cnt = 0;
        header_reserve = FILE_CACHE_DEFAULT_HEADER_LENGTH;
        header_used = 0;
        memset(last_modified, 0, sizeof(last_modified));
        memset(etag, 0, sizeof(etag));
    }

    // NOTE: caller must hold mutex.
    // On Windows, Open() uses _wstat() directly instead of calling this.
    bool is_modified() {
        time_t mtime = st.st_mtime;
        ::stat(filepath.c_str(), &st);
        return mtime != st.st_mtime;
    }

    // NOTE: caller must hold mutex
    bool is_complete() {
        if (S_ISDIR(st.st_mode)) return filebuf.len > 0;
        return filebuf.len == (size_t)st.st_size;
    }

    // NOTE: caller must hold mutex — invalidates filebuf/httpbuf pointers
    void resize_buf(size_t filesize, int reserved) {
        if (reserved < 0) reserved = 0;
        header_reserve = reserved;
        buf.resize((size_t)reserved + filesize);
        filebuf.base = buf.base + reserved;
        filebuf.len = filesize;
        // Invalidate httpbuf since buffer may have been reallocated
        httpbuf.base = NULL;
        httpbuf.len = 0;
        header_used = 0;
    }

    void resize_buf(size_t filesize) {
        resize_buf(filesize, header_reserve);
    }

    // Thread-safe: prepend header into reserved space.
    // Returns true on success, false if header exceeds reserved space.
    // On failure, httpbuf falls back to filebuf (body only, no header).
    bool prepend_header(const char* header, int len) {
        std::lock_guard<std::mutex> lock(mutex);
        if (len <= 0 || len > header_reserve) {
            // Safe fallback: point httpbuf at filebuf so callers always get valid data
            httpbuf = filebuf;
            header_used = 0;
            return false;
        }
        httpbuf.base = filebuf.base - len;
        httpbuf.len = (size_t)len + filebuf.len;
        memcpy(httpbuf.base, header, len);
        header_used = len;
        return true;
    }

    // --- thread-safe accessors ---
    int  get_header_reserve()  const { std::lock_guard<std::mutex> lock(mutex); return header_reserve; }
    int  get_header_used()     const { std::lock_guard<std::mutex> lock(mutex); return header_used; }
    int  get_header_remaining() const { std::lock_guard<std::mutex> lock(mutex); return header_reserve - header_used; }
    bool header_fits(int len)  const { std::lock_guard<std::mutex> lock(mutex); return len > 0 && len <= header_reserve; }
} file_cache_t;

typedef std::shared_ptr<file_cache_t> file_cache_ptr;

class HV_EXPORT FileCache : public hv::LRUCache<std::string, file_cache_ptr> {
public:
    // --- configurable parameters (were hardcoded macros before) ---
    int stat_interval;      // seconds between stat() checks
    int expired_time;       // seconds before cache entry expires
    int max_header_length;  // reserved header bytes per entry
    int max_file_size;      // max cached file size (larger = large-file path)

    explicit FileCache(size_t capacity = FILE_CACHE_DEFAULT_MAX_NUM);

    struct OpenParam {
        bool    need_read;
        int     max_read;       // per-request override for max file size
        const char* path;       // URL path (for directory listing)
        size_t  filesize;       // [out] actual file size
        int     error;          // [out] error code if Open returns NULL

        OpenParam() {
            need_read = true;
            max_read = FILE_CACHE_DEFAULT_MAX_FILE_SIZE;
            path = "/";
            filesize = 0;
            error = 0;
        }
    };

    file_cache_ptr Open(const char* filepath, OpenParam* param);
    bool Exists(const char* filepath) const;
    bool Close(const char* filepath);
    void RemoveExpiredFileCache();

    // --- new: getters ---
    int  GetMaxHeaderLength()   const { return max_header_length; }
    int  GetMaxFileSize()       const { return max_file_size; }
    int  GetStatInterval()      const { return stat_interval; }
    int  GetExpiredTime()       const { return expired_time; }

    // --- new: setters ---
    void SetMaxHeaderLength(int len)    { max_header_length = len < 0 ? 0 : len; }
    void SetMaxFileSize(int size)       { max_file_size = size < 1 ? 1 : size; }

protected:
    file_cache_ptr Get(const char* filepath);
};

#endif // HV_FILE_CACHE_H_
