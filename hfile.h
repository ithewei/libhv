#ifndef HW_FILE_H_
#define HW_FILE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "hdef.h"
#include "hbuf.h"
#include "herr.h"
#include "hstring.h"

class HFile {
 public:
    HFile() {
        _fp = NULL;
    }

    ~HFile() {
        close();
    }

    int open(const char* filepath, const char* mode) {
        close();
        strncpy(_filepath, filepath, MAX_PATH);
        _fp = fopen(filepath, mode);
        if (_fp == NULL) {
            return ERR_OPEN_FILE;
        }
        return 0;
    }

    void close() {
        if (_fp) {
            fclose(_fp);
            _fp = NULL;
        }
    }

    size_t size() {
        struct stat st;
        stat(_filepath, &st);
        return st.st_size;
    }

    size_t read(void* ptr, size_t len) {
        return fread(ptr, 1, len, _fp);
    }

    size_t readall(hbuf_t& buf) {
        size_t filesize = size();
        buf.init(filesize);
        return fread(buf.base, 1, buf.len, _fp);
    }

    size_t readall(string& str) {
        size_t filesize = size();
        str.resize(filesize);
        return fread((void*)str.data(), 1, filesize, _fp);
    }

    bool readline(string& str) {
        str.clear();
        char ch;
        while (fread(&ch, 1, 1, _fp)) {
            if (ch == '\n') {
                return true;
            }
            str += ch;
        }
        return str.length() != 0;
    }

    size_t write(const void* ptr, size_t len) {
        return fwrite(ptr, 1, len, _fp);
    }

    char  _filepath[MAX_PATH];
    FILE* _fp;
};

#endif  // HW_FILE_H_
