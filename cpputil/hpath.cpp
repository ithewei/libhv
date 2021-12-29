#include "hpath.h"

#include "hplatform.h"

bool HPath::exists(const char* path) {
    return access(path, 0) == 0;
}

bool HPath::isdir(const char* path) {
    if (access(path, 0) != 0) return false;
    struct stat st;
    memset(&st, 0, sizeof(st));
    stat(path, &st);
    return S_ISDIR(st.st_mode);
}

bool HPath::isfile(const char* path) {
    if (access(path, 0) != 0) return false;
    struct stat st;
    memset(&st, 0, sizeof(st));
    stat(path, &st);
    return S_ISREG(st.st_mode);
}

bool HPath::islink(const char* path) {
#ifdef OS_WIN
    return HPath::isdir(path) && (GetFileAttributesA(path) & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    if (access(path, 0) != 0) return false;
    struct stat st;
    memset(&st, 0, sizeof(st));
    lstat(path, &st);
    return S_ISLNK(st.st_mode);
#endif
}

std::string HPath::basename(const std::string& filepath) {
    std::string::size_type pos1 = filepath.find_last_not_of("/\\");
    if (pos1 == std::string::npos) {
        return "/";
    }
    std::string::size_type pos2 = filepath.find_last_of("/\\", pos1);
    if (pos2 == std::string::npos) {
        pos2 = 0;
    } else {
        pos2++;
    }

    return filepath.substr(pos2, pos1-pos2+1);
}

std::string HPath::dirname(const std::string& filepath) {
    std::string::size_type pos1 = filepath.find_last_not_of("/\\");
    if (pos1 == std::string::npos) {
        return "/";
    }
    std::string::size_type pos2 = filepath.find_last_of("/\\", pos1);
    if (pos2 == std::string::npos) {
        return ".";
    } else if (pos2 == 0) {
        pos2 = 1;
    }

    return filepath.substr(0, pos2);
}

std::string HPath::filename(const std::string& filepath) {
    std::string::size_type pos1 = filepath.find_last_of("/\\");
    if (pos1 == std::string::npos) {
        pos1 = 0;
    } else {
        pos1++;
    }
    std::string file = filepath.substr(pos1, -1);

    std::string::size_type pos2 = file.find_last_of(".");
    if (pos2 == std::string::npos) {
        return file;
    }
    return file.substr(0, pos2);
}

std::string HPath::suffixname(const std::string& filepath) {
    std::string::size_type pos1 = filepath.find_last_of("/\\");
    if (pos1 == std::string::npos) {
        pos1 = 0;
    } else {
        pos1++;
    }
    std::string file = filepath.substr(pos1, -1);

    std::string::size_type pos2 = file.find_last_of(".");
    if (pos2 == std::string::npos) {
        return "";
    }
    return file.substr(pos2+1, -1);
}

std::string HPath::join(const std::string& dir, const std::string& filename) {
    char separator = '/';
#ifdef OS_WIN
    if (dir.find_first_of("\\") != std::string::npos) {
        separator = '\\';
    }
#endif
    std::string filepath(dir);
    if (dir[dir.length()-1] != separator) {
        filepath += separator;
    }
    filepath += filename;
    return filepath;
}
