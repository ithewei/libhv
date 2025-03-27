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

FileCache::FileCache() {
    stat_interval = 10; // s
    expired_time  = 60; // s
}

file_cache_ptr FileCache::Open(const char* filepath, OpenParam* param) {
    std::lock_guard<std::mutex> locker(mutex_);
    file_cache_ptr fc = Get(filepath);
#ifdef OS_WIN
    std::wstring wfilepath;
#endif
    bool modified = false;
    if (fc) {
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
#ifdef OS_WIN
        if(wfilepath.empty()) wfilepath = hv::utf8_to_wchar(filepath);
        if(_wstat(wfilepath.c_str(), (struct _stat*)&st) != 0) {
            param->error = ERR_OPEN_FILE;
            return NULL;
        }
        if(S_ISREG(st.st_mode)) {
            fd = _wopen(wfilepath.c_str(), flags);
        }else if (S_ISDIR(st.st_mode)) {
            // NOTE: open(dir) return -1 on windows
            fd = 0;
        }
#else
        if(stat(filepath, &st) != 0) {
            param->error = ERR_OPEN_FILE;
            return NULL;
        }
        fd = open(filepath, flags);
#endif
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
                cached_files[filepath] = fc;
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

bool FileCache::Close(const char* filepath) {
    std::lock_guard<std::mutex> locker(mutex_);
    auto iter = cached_files.find(filepath);
    if (iter != cached_files.end()) {
        iter = cached_files.erase(iter);
        return true;
    }
    return false;
}

bool FileCache::Close(const file_cache_ptr& fc) {
    std::lock_guard<std::mutex> locker(mutex_);
    auto iter = cached_files.begin();
    while (iter != cached_files.end()) {
        if (iter->second == fc) {
            iter = cached_files.erase(iter);
            return true;
        } else {
            ++iter;
        }
    }
    return false;
}

file_cache_ptr FileCache::Get(const char* filepath) {
    auto iter = cached_files.find(filepath);
    if (iter != cached_files.end()) {
        return iter->second;
    }
    return NULL;
}

void FileCache::RemoveExpiredFileCache() {
    std::lock_guard<std::mutex> locker(mutex_);
    time_t now = time(NULL);
    auto iter = cached_files.begin();
    while (iter != cached_files.end()) {
        if (now - iter->second->stat_time > expired_time) {
            iter = cached_files.erase(iter);
        } else {
            ++iter;
        }
    }
}
