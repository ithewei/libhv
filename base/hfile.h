#ifndef HV_FILE_H_
#define HV_FILE_H_

#include <string>

#include "hdef.h"
#include "hbuf.h"
#include "herr.h"

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

    size_t readall(HBuf& buf) {
        size_t filesize = size();
        buf.resize(filesize);
        return fread(buf.base, 1, filesize, _fp);
    }

    size_t readall(std::string& str) {
        size_t filesize = size();
        str.resize(filesize);
        return fread((void*)str.data(), 1, filesize, _fp);
    }

    bool readline(std::string& str) {
        str.clear();
        char ch;
        while (fread(&ch, 1, 1, _fp)) {
            if (ch == LF) {
                // unix: LF
                return true;
            }
            if (ch == CR) {
                // dos: CRLF
                // read LF
                if (fread(&ch, 1, 1, _fp) && ch != LF) {
                    // mac: CR
                    fseek(_fp, -1, SEEK_CUR);
                }
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

#endif // HV_FILE_H_
