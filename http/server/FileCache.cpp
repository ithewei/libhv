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
            struct _stat latest_st;
            if (_wstat(wfilepath.c_str(), &latest_st) != 0) {
                modified = true;
            } else {
                modified = fc->st.st_mtime != latest_st.st_mtime ||
                           fc->st.st_size != latest_st.st_size ||
                           fc->st.st_mode != latest_st.st_mode;
            }
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
        auto fail_open = [this, filepath, param, &fc](int error) -> file_cache_ptr {
            param->error = error;
            if (fc) {
                remove(filepath);
            }
            return NULL;
        };
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
            return fail_open(ERR_OPEN_FILE);
        }
        if (S_ISREG(st.st_mode)) {
            fd = _wopen(wfilepath.c_str(), flags);
        } else if (S_ISDIR(st.st_mode)) {
            is_dir = true;
        }
#else
        if (::stat(filepath, &st) != 0) {
            return fail_open(ERR_OPEN_FILE);
        }
        fd = open(filepath, flags);
#endif
        if (fd < 0 && !is_dir) {
            return fail_open(ERR_OPEN_FILE);
        }
        defer(if (fd >= 0) { close(fd); })
        size_t filepath_len = strlen(filepath);
        if (!S_ISREG(st.st_mode) &&
            !(S_ISDIR(st.st_mode) && filepath_len > 0 && filepath[filepath_len - 1] == '/')) {
            return fail_open(ERR_MISMATCH);
        }

        // Build a replacement off-cache. Published entries stay immutable so
        // in-flight responses never observe a realloc or partially refreshed metadata.
        file_cache_ptr refreshed = std::make_shared<file_cache_t>();
        refreshed->filepath = filepath;
        refreshed->st = st;
        refreshed->header_reserve = max_header_length;
        time(&refreshed->open_time);
        refreshed->stat_time = refreshed->open_time;
        refreshed->stat_cnt = 1;

        if (S_ISREG(st.st_mode)) {
            param->filesize = st.st_size;
            // FILE
            if (param->need_read && st.st_size > param->max_read) {
                return fail_open(ERR_OVER_LIMIT);
            }
            if (param->need_read) {
                refreshed->resize_buf(st.st_size, max_header_length);
                // Loop to handle partial reads (EINTR, etc.). The entry is not
                // published yet, so no cache mutex is needed around blocking IO.
                char* dst = refreshed->filebuf.base;
                size_t remaining = refreshed->filebuf.len;
                while (remaining > 0) {
                    int nread = read(fd, dst, remaining);
                    if (nread < 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        hloge("Failed to read file: %s", filepath);
                        return fail_open(ERR_READ_FILE);
                    }
                    if (nread == 0) {
                        hloge("Unexpected EOF reading file: %s", filepath);
                        return fail_open(ERR_READ_FILE);
                    }
                    dst += nread;
                    remaining -= nread;
                }
            }
            const char* suffix = strrchr(filepath, '.');
            if (suffix) {
                http_content_type content_type = http_content_type_enum_by_suffix(suffix + 1);
                if (content_type == TEXT_HTML) {
                    refreshed->content_type = "text/html; charset=utf-8";
                } else if (content_type == TEXT_PLAIN) {
                    refreshed->content_type = "text/plain; charset=utf-8";
                } else {
                    refreshed->content_type = http_content_type_str_by_suffix(suffix + 1);
                }
            }
        } else if (S_ISDIR(st.st_mode)) {
            // DIR
            std::string page;
            make_index_of_page(filepath, page, param->path);
            refreshed->resize_buf(page.size(), max_header_length);
            memcpy(refreshed->filebuf.base, page.c_str(), page.size());
            refreshed->content_type = "text/html; charset=utf-8";
        }
        gmtime_fmt(refreshed->st.st_mtime, refreshed->last_modified);
        snprintf(refreshed->etag, sizeof(refreshed->etag), ETAG_FMT,
                 (size_t)refreshed->st.st_mtime, (size_t)refreshed->st.st_size);

        // Cache the fully initialized entry (acquires LRUCache mutex only)
        fc = refreshed;
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
