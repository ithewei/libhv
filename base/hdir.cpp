#include "hdir.h"

#include "hplatform.h"

#ifdef OS_WIN
//FILETIME starts from 1601-01-01 UTC, epoch from 1970-01-01 UTC
//FILETIME unit (100ns)
#define FILETIME_EPOCH_DIFF     11644473600 // s
time_t FileTime2Epoch(FILETIME filetime) {
    uint64_t ll = (((uint64_t)filetime.dwHighDateTime) << 32) | filetime.dwLowDateTime;
    ll /= 1e7; // s
    return ll - FILETIME_EPOCH_DIFF;
}
#endif

static bool less(const hdir_t& lhs, const hdir_t& rhs) {
    return stricmp(lhs.name, rhs.name) < 0;
}

int listdir(const char* dir, std::list<hdir_t>& dirs) {
    int dirlen = strlen(dir);
    if (dirlen > 256) {
        return -1;
    }
    char path[512];
    strcpy(path, dir);
    if (dir[dirlen-1] != '/') {
        strcat(path, "/");
        ++dirlen;
    }
    dirs.clear();
#ifdef OS_UNIX
    // opendir -> readdir -> closedir
    DIR* dp = opendir(dir);
    if (dp == NULL) return -1;
    struct dirent* result = NULL;
    struct stat st;
    hdir_t tmp;
    while ((result = readdir(dp))) {
        memset(&tmp, 0, sizeof(hdir_t));
        strncpy(tmp.name, result->d_name, sizeof(tmp.name));
        strncpy(path+dirlen, result->d_name, sizeof(path)-dirlen);
        if (lstat(path, &st) == 0) {
            if (S_ISREG(st.st_mode))        tmp.type = 'f';
            else if (S_ISDIR(st.st_mode))   tmp.type = 'd';
            else if (S_ISLNK(st.st_mode))   tmp.type = 'l';
            else if (S_ISBLK(st.st_mode))   tmp.type = 'b';
            else if (S_ISCHR(st.st_mode))   tmp.type = 'c';
            else if (S_ISSOCK(st.st_mode))  tmp.type = 's';
            else if (S_ISFIFO(st.st_mode))  tmp.type = 'p';
            else                            tmp.type = '-';
            tmp.mode = st.st_mode & 0777;
            tmp.size = st.st_size;
            tmp.atime = st.st_atime;
            tmp.mtime = st.st_mtime;
            tmp.ctime = st.st_ctime;
        }
        dirs.push_back(tmp);
    }
    closedir(dp);
#elif defined(OS_WIN)
    // FindFirstFile -> FindNextFile -> FindClose
    strcat(path, "*");
    WIN32_FIND_DATA data;
    HANDLE h = FindFirstFile(path, &data);
    if (h == NULL) {
        return -1;
    }
    hdir_t tmp;
    do {
        memset(&tmp, 0, sizeof(hdir_t));
        strncpy(tmp.name, data.cFileName, sizeof(tmp.name));
        tmp.type = 'f';
        if (data.dwFileAttributes & _A_SUBDIR) {
            tmp.type = 'd';
        }
        tmp.mode = 0777;
        tmp.size = (((uint64_t)data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        tmp.atime = FileTime2Epoch(data.ftLastAccessTime);
        tmp.mtime = FileTime2Epoch(data.ftLastWriteTime);
        tmp.ctime = FileTime2Epoch(data.ftCreationTime);
        dirs.push_back(tmp);
    } while (FindNextFile(h, &data));
    FindClose(h);
#endif
    dirs.sort(less);
    return dirs.size();
}
