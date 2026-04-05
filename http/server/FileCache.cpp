#include "FileCache.h"

#include "herr.h"
#include "hscope.h"
#include "htime.h"
#include "hlog.h"

#include "httpdef.h"    // import http_content_type_str_by_suffix
#include "http_page.h"  // import make_index_of_page

#ifdef OS_WIN
#include "hstring.h" // import hv::utf8_to_wchar
#endif

#define ETAG_FMT    "\"%zx-%zx\""

FileCache::FileCache(size_t capacity)
    : hv::LRUCache<std::string, file_cache_ptr>(capacity) {
    stat_interval    = 10;  // s
    expired_time     = 60;  // s
    max_header_length = FILE_CACHE_DEFAULT_HEADER_LENGTH;
    max_file_size    = FILE_CACHE_DEFAULT_MAX_FILE_SIZE;
}

file_cache_ptr FileCache::Open(const char* filepath, OpenParam* param) {
    file_cache_ptr fc = Get(filepath);
#ifdef OS_WIN
    std::wstring wfilepath;
#endif
    bool modified = false;
    if (fc) {
        std::lock_guard<std::mutex> lock(fc->mutex);
        time_t now = time(NULL);
        if (now - fc->stat_time > stat_interval) {
            fc->stat_time = now;
            fc->stat_cnt++;
#ifdef OS_WIN
            wfilepath = hv::utf8_to_wchar(filepath);
            now = fc->st.st_mtime;
            _wstat(wfilepath.c_str(), (struct _stat*)&fc->st);
            modified = now != fc->st.st_mtime;
#else
            modified = fc->is_modified();
#endif
        }
        if (param->need_read) {
            if (!modified && fc->is_complete()) {
                param->need_read = false;
            }
        }
    }
    if (fc == NULL || modified || param->need_read) {
        struct stat st;
        int flags = O_RDONLY;
#ifdef O_BINARY
        flags |= O_BINARY;
#endif
        int fd = -1;
        bool is_dir = false;
#ifdef OS_WIN
        if (wfilepath.empty()) wfilepath = hv::utf8_to_wchar(filepath);
        if (_wstat(wfilepath.c_str(), (struct _stat*)&st) != 0) {
            param->error = ERR_OPEN_FILE;
            return NULL;
        }
        if (S_ISREG(st.st_mode)) {
            fd = _wopen(wfilepath.c_str(), flags);
        } else if (S_ISDIR(st.st_mode)) {
            is_dir = true;
        }
#else
        if (::stat(filepath, &st) != 0) {
            param->error = ERR_OPEN_FILE;
            return NULL;
        }
        fd = open(filepath, flags);
#endif
        if (fd < 0 && !is_dir) {
            param->error = ERR_OPEN_FILE;
            return NULL;
        }
        defer(if (fd >= 0) { close(fd); })
        if (fc == NULL) {
            if (S_ISREG(st.st_mode) ||
                (S_ISDIR(st.st_mode) &&
                 filepath[strlen(filepath) - 1] == '/')) {
                fc = std::make_shared<file_cache_t>();
                fc->filepath = filepath;
                fc->st = st;
                fc->header_reserve = max_header_length;
                time(&fc->open_time);
                fc->stat_time = fc->open_time;
                fc->stat_cnt = 1;
                // NOTE: do NOT put() into cache yet — defer until fully initialized
            } else {
                param->error = ERR_MISMATCH;
                return NULL;
            }
        }
        // Hold fc->mutex for initialization, but release before put()
        // to avoid lock-order inversion with RemoveExpiredFileCache().
        // Lock order: LRUCache mutex → fc->mutex (never reverse).
        {
            std::lock_guard<std::mutex> lock(fc->mutex);
            // Sync local stat result into cached entry
            fc->st = st;
            if (S_ISREG(fc->st.st_mode)) {
                param->filesize = fc->st.st_size;
                // FILE
                if (param->need_read) {
                    if (fc->st.st_size > param->max_read) {
                        param->error = ERR_OVER_LIMIT;
                        // Don't cache incomplete entries
                        return NULL;
                    }
                    fc->resize_buf(fc->st.st_size, max_header_length);
                    // Loop to handle partial reads (EINTR, etc.)
                    char* dst = fc->filebuf.base;
                    size_t remaining = fc->filebuf.len;
                    while (remaining > 0) {
                        ssize_t nread = read(fd, dst, remaining);
                        if (nread < 0) {
                            if (errno == EINTR) continue;
                            hloge("Failed to read file: %s", filepath);
                            param->error = ERR_READ_FILE;
                            return NULL;
                        }
                        if (nread == 0) {
                            hloge("Unexpected EOF reading file: %s", filepath);
                            param->error = ERR_READ_FILE;
                            return NULL;
                        }
                        dst += nread;
                        remaining -= nread;
                    }
                }
                const char* suffix = strrchr(filepath, '.');
                if (suffix) {
                    http_content_type content_type = http_content_type_enum_by_suffix(suffix + 1);
                    if (content_type == TEXT_HTML) {
                        fc->content_type = "text/html; charset=utf-8";
                    } else if (content_type == TEXT_PLAIN) {
                        fc->content_type = "text/plain; charset=utf-8";
                    } else {
                        fc->content_type = http_content_type_str_by_suffix(suffix + 1);
                    }
                }
            } else if (S_ISDIR(fc->st.st_mode)) {
                // DIR
                std::string page;
                make_index_of_page(filepath, page, param->path);
                fc->resize_buf(page.size(), max_header_length);
                memcpy(fc->filebuf.base, page.c_str(), page.size());
                fc->content_type = "text/html; charset=utf-8";
            }
            gmtime_fmt(fc->st.st_mtime, fc->last_modified);
            snprintf(fc->etag, sizeof(fc->etag), ETAG_FMT,
                     (size_t)fc->st.st_mtime, (size_t)fc->st.st_size);
        } // release fc->mutex before put() to maintain lock ordering
        // Cache the fully initialized entry (acquires LRUCache mutex only)
        put(filepath, fc);
    }
    return fc;
}

bool FileCache::Exists(const char* filepath) const {
    return contains(filepath);
}

bool FileCache::Close(const char* filepath) {
    return remove(filepath);
}

file_cache_ptr FileCache::Get(const char* filepath) {
    file_cache_ptr fc;
    if (get(filepath, fc)) {
        return fc;
    }
    return NULL;
}

void FileCache::RemoveExpiredFileCache() {
    time_t now = time(NULL);
    remove_if([this, now](const std::string& filepath, const file_cache_ptr& fc) {
        // Use try_to_lock to avoid lock-order inversion with Open().
        // If the entry is busy, skip it — it will be checked next cycle.
        std::unique_lock<std::mutex> lock(fc->mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return false;
        }
        return (now - fc->stat_time > expired_time);
    });
}
