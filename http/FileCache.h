#ifndef HW_FILE_CACHE_H_
#define HW_FILE_CACHE_H_

#include <map>
#include <string>

#include "hbuf.h"
#include "hfile.h"
#include "md5.h"
#include "HttpRequest.h" // for get_content_type_str_by_suffix
#include "httpd_conf.h"

#ifndef INVALID_FD
#define INVALID_FD  -1
#endif

typedef struct file_cache_s {
    //std::string filepath;
    struct stat st;
    time_t      open_time;
    time_t      stat_time;
    uint32_t    stat_cnt;
    HBuf        filebuf;
    char        last_modified[64];
    char        etag[64];
    const char* content_type;

    file_cache_s() {
        stat_cnt = 0;
        content_type = NULL;
    }
} file_cache_t;

// filepath => file_cache_t
typedef std::map<std::string, file_cache_t*> FileCacheMap;

class FileCache {
public:
    FileCacheMap cached_files;

    ~FileCache() {
        for (auto& pair : cached_files) {
            delete pair.second;
        }
        cached_files.clear();
    }

    file_cache_t* Open(const char* filepath) {
        file_cache_t* fc = Get(filepath);
        bool filechanged = false;
        if (fc) {
            time_t tt;
            time(&tt);
            if (tt - fc->stat_time > g_conf_ctx.file_stat_interval) {
                struct timespec mtime = fc->st.st_mtim;
                stat(filepath, &fc->st);
                fc->stat_time = tt;
                fc->stat_cnt++;
                if (mtime.tv_sec != fc->st.st_mtim.tv_sec ||
                    mtime.tv_nsec != fc->st.st_mtim.tv_nsec) {
                    filechanged = true;
                    fc->stat_cnt = 1;
                }
            }
        }
        if (fc == NULL || filechanged) {
            int fd = open(filepath, O_RDONLY);
            if (fd < 0) {
                return NULL;
            }
            if (fc == NULL) {
                fc = new file_cache_t;
                //fc->filepath = filepath;
                fstat(fd, &fc->st);
                time(&fc->open_time);
                fc->stat_time = fc->open_time;
                fc->stat_cnt = 1;
                cached_files[filepath] = fc;
            }
            fc->filebuf.resize(fc->st.st_size);
            read(fd, fc->filebuf.base, fc->filebuf.len);
            close(fd);
            time_t tt = fc->st.st_mtim.tv_sec;
            strftime(fc->last_modified, sizeof(fc->last_modified), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&tt));
            MD5_CTX md5_ctx;
            MD5Init(&md5_ctx);
            MD5Update(&md5_ctx, fc->filebuf.base, fc->filebuf.len);
            unsigned char digital[16];
            MD5Final(digital, &md5_ctx);
            char* md5 = fc->etag;
            for (int i = 0; i < 16; ++i) {
                sprintf(md5, "%02x", digital[i]);
                md5 += 2;
            }
            fc->etag[32] = '\0';
            const char* suffix = strrchr(filepath, '.');
            if (suffix) {
                ++suffix;
                fc->content_type = http_content_type_str_by_suffix(suffix);
            }
        }
        return fc;
    }

    int Close(const char* filepath) {
        auto iter = cached_files.find(filepath);
        if (iter != cached_files.end()) {
            delete iter->second;
            iter = cached_files.erase(iter);
            return 0;
        }
        return -1;
    }

protected:
    file_cache_t* Get(const char* filepath) {
        auto iter = cached_files.find(filepath);
        if (iter != cached_files.end()) {
            return iter->second;
        }
        return NULL;
    }
};

#endif // HW_FILE_CACHE_H_
