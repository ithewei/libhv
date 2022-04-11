#ifndef HV_FILE_CACHE_H_
#define HV_FILE_CACHE_H_

#include <memory>
#include <map>
#include <string>
#include <mutex>

#include "hbuf.h"
#include "hstring.h"

#define HTTP_HEADER_MAX_LENGTH      1024        // 1K
#define FILE_CACHE_MAX_SIZE         (1 << 22)   // 4M

typedef struct file_cache_s {
    std::string filepath;
    struct stat st;
    time_t      open_time;
    time_t      stat_time;
    uint32_t    stat_cnt;
    HBuf        buf; // http_header + file_content
    hbuf_t      filebuf;
    hbuf_t      httpbuf;
    char        last_modified[64];
    char        etag[64];
    std::string content_type;

    file_cache_s() {
        stat_cnt = 0;
    }

    bool is_modified() {
        time_t mtime = st.st_mtime;
        stat(filepath.c_str(), &st);
        return mtime != st.st_mtime;
    }

    bool is_complete() {
        return filebuf.len == st.st_size;
    }

    void resize_buf(int filesize) {
        buf.resize(HTTP_HEADER_MAX_LENGTH + filesize);
        filebuf.base = buf.base + HTTP_HEADER_MAX_LENGTH;
        filebuf.len = filesize;
    }

    void prepend_header(const char* header, int len) {
        if (len > HTTP_HEADER_MAX_LENGTH) return;
        httpbuf.base = filebuf.base - len;
        httpbuf.len = len + filebuf.len;
        memcpy(httpbuf.base, header, len);
    }
} file_cache_t;

typedef std::shared_ptr<file_cache_t>           file_cache_ptr;
// filepath => file_cache_ptr
typedef std::map<std::string, file_cache_ptr>   FileCacheMap;

class FileCache {
public:
    FileCacheMap    cached_files;
    std::mutex      mutex_;
    int             stat_interval;
    int             expired_time;

    FileCache() {
        stat_interval = 10; // s
        expired_time  = 60; // s
    }

    struct OpenParam {
        bool need_read;
        int  max_read;
        const char* path;
        size_t  filesize;
        int  error;

        OpenParam() {
            need_read = true;
            max_read = FILE_CACHE_MAX_SIZE;
            path = "/";
            filesize = 0;
            error = 0;
        }
    };
    file_cache_ptr Open(const char* filepath, OpenParam* param);
    bool Close(const char* filepath);
    bool Close(const file_cache_ptr& fc);
    void RemoveExpiredFileCache();

protected:
    file_cache_ptr Get(const char* filepath);
};

#endif // HV_FILE_CACHE_H_
