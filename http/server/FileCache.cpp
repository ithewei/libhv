#include "FileCache.h"

#include <fcntl.h>

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

// platform-abstracted stat + open.
// @return fd (>=0) on success, -1 on error. fills st on success.
// NOTE: open(dir) returns -1 on windows, so a directory yields fd=0 there.
static int stat_and_open(const char* filepath, struct stat* st, int flags) {
#ifdef OS_WIN
    std::wstring wpath = hv::utf8_to_wchar(filepath);
    if (_wstat(wpath.c_str(), (struct _stat*)st) != 0) return -1;
    if (S_ISREG(st->st_mode)) return _wopen(wpath.c_str(), flags);
    if (S_ISDIR(st->st_mode)) return 0;
    return -1;
#else
    if (stat(filepath, st) != 0) return -1;
    return open(filepath, flags);
#endif
}

FileCache::FileCache(size_t capacity) : hv::LRUCache<std::string, file_cache_ptr>(capacity) {
    stat_interval = 10; // s
    expired_time  = 60; // s
}

file_cache_ptr FileCache::Open(const char* filepath, OpenParam* param) {
    file_cache_ptr fc = Get(filepath);
    bool modified = false;
    if (fc) {
        time_t now = time(NULL);
        if (now - fc->stat_time > stat_interval) {
            fc->stat_time = now;
            fc->stat_cnt++;
            modified = fc->is_modified();
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
        int fd = stat_and_open(filepath, &st, flags);
        if (fd < 0) {
            param->error = ERR_OPEN_FILE;
            return NULL;
        }
        defer(if (fd > 0) { close(fd); })
        if (fc == NULL) {
            if (S_ISREG(st.st_mode) ||
                (S_ISDIR(st.st_mode) &&
                 filepath[strlen(filepath)-1] == '/')) {
                fc = std::make_shared<file_cache_t>();
                fc->filepath = filepath;
                fc->st = st;
                time(&fc->open_time);
                fc->stat_time = fc->open_time;
                fc->stat_cnt = 1;
                put(filepath, fc);
            }
            else {
                param->error = ERR_MISMATCH;
                return NULL;
            }
        }
        if (S_ISREG(fc->st.st_mode)) {
            param->filesize = fc->st.st_size;
            // FILE
            if (param->need_read) {
                if (fc->st.st_size > param->max_read) {
                    param->error = ERR_OVER_LIMIT;
                    return NULL;
                }
                fc->resize_buf(fc->st.st_size);
                int nread = read(fd, fc->filebuf.base, fc->filebuf.len);
                if (nread != fc->filebuf.len) {
                    hloge("Failed to read file: %s", filepath);
                    param->error = ERR_READ_FILE;
                    Close(filepath);
                    return NULL;
                }
            }
            const char* suffix = strrchr(filepath, '.');
            if (suffix) {
                http_content_type content_type = http_content_type_enum_by_suffix(suffix+1);
                if (content_type == TEXT_HTML) {
                    fc->content_type = "text/html; charset=utf-8";
                } else if (content_type == TEXT_PLAIN) {
                    fc->content_type = "text/plain; charset=utf-8";
                } else {
                    fc->content_type = http_content_type_str_by_suffix(suffix+1);
                }
            }
        }
        else if (S_ISDIR(fc->st.st_mode)) {
            // DIR
            std::string page;
            make_index_of_page(filepath, page, param->path);
            fc->resize_buf(page.size());
            memcpy(fc->filebuf.base, page.c_str(), page.size());
            fc->content_type = "text/html; charset=utf-8";
        }
        gmtime_fmt(fc->st.st_mtime, fc->last_modified);
        snprintf(fc->etag, sizeof(fc->etag), ETAG_FMT, (size_t)fc->st.st_mtime, (size_t)fc->st.st_size);
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
        return (now - fc->stat_time > expired_time);
    });
}
