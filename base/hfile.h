#ifndef HV_FILE_H_
#define HV_FILE_H_

#include <string> // for std::string

#include "hplatform.h" // for stat
#include "hdef.h" // for LF, CR
#include "hbuf.h" // for HBuf

class HFile {
public:
    HFile() {
        fp = NULL;
    }

    ~HFile() {
        close();
    }

    int open(const char* filepath, const char* mode) {
        close();
        strncpy(this->filepath, filepath, MAX_PATH);
        fp = fopen(filepath, mode);
        return fp ? 0 : errno;
    }

    void close() {
        if (fp) {
            fclose(fp);
            fp = NULL;
        }
    }

    size_t read(void* ptr, size_t len) {
        return fread(ptr, 1, len, fp);
    }

    size_t write(const void* ptr, size_t len) {
        return fwrite(ptr, 1, len, fp);
    }

    size_t size() {
        struct stat st;
        memset(&st, 0, sizeof(st));
        stat(filepath, &st);
        return st.st_size;
    }

    size_t readall(HBuf& buf) {
        size_t filesize = size();
        buf.resize(filesize);
        return fread(buf.base, 1, filesize, fp);
    }

    size_t readall(std::string& str) {
        size_t filesize = size();
        str.resize(filesize);
        return fread((void*)str.data(), 1, filesize, fp);
    }

    bool readline(std::string& str) {
        str.clear();
        char ch;
        while (fread(&ch, 1, 1, fp)) {
            if (ch == LF) {
                // unix: LF
                return true;
            }
            if (ch == CR) {
                // dos: CRLF
                // read LF
                if (fread(&ch, 1, 1, fp) && ch != LF) {
                    // mac: CR
                    fseek(fp, -1, SEEK_CUR);
                }
                return true;
            }
            str += ch;
        }
        return str.length() != 0;
    }

    int readrange(std::string& str, size_t from = 0, size_t to = 0) {
        size_t filesize = size();
        if (to == 0 || to >= filesize) to = filesize - 1;
        size_t readbytes = to - from + 1;
        str.resize(readbytes);
        fseek(fp, from, SEEK_SET);
        return fread((void*)str.data(), 1, readbytes, fp);
    }

public:
    char  filepath[MAX_PATH];
    FILE* fp;
};

#endif // HV_FILE_H_
