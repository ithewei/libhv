#ifndef HV_DIR_H_
#define HV_DIR_H_

/*
 *@code
int main(int argc, char* argv[]) {
    const char* dir = ".";
    if (argc > 1) {
        dir = argv[1];
    }
    std::list<hdir_t> dirs;
    listdir(dir, dirs);
    for (auto& item : dirs) {
        printf("%c%c%c%c%c%c%c%c%c%c\t",
            item.type,
            item.mode & 0400 ? 'r' : '-',
            item.mode & 0200 ? 'w' : '-',
            item.mode & 0100 ? 'x' : '-',
            item.mode & 0040 ? 'r' : '-',
            item.mode & 0020 ? 'w' : '-',
            item.mode & 0010 ? 'x' : '-',
            item.mode & 0004 ? 'r' : '-',
            item.mode & 0002 ? 'w' : '-',
            item.mode & 0001 ? 'x' : '-');
        float hsize;
        if (item.size < 1024) {
            printf("%lu\t", item.size);
        }
        else if ((hsize = item.size/1024.0f) < 1024.0f) {
            printf("%.1fK\t", hsize);
        }
        else if ((hsize /= 1024.0f) < 1024.0f) {
            printf("%.1fM\t", hsize);
        }
        else {
            hsize /= 1024.0f;
            printf("%.1fG\t", hsize);
        }
        struct tm* tm = localtime(&item.mtime);
        printf("%04d-%02d-%02d %02d:%02d:%02d\t",
                tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
        printf("%s%s\n", item.name, item.type == 'd' ? "/" : "");
    }
    return 0;
}
*/

#include <string.h>
#include <time.h>

#include <list>

#include "hexport.h"

typedef struct hdir_s {
    char    name[256];
    char    type; // f:file d:dir l:link b:block c:char s:socket p:pipe
    char    reserverd;
    unsigned short mode;
    size_t  size;
    time_t  atime;
    time_t  mtime;
    time_t  ctime;
} hdir_t;

// listdir: same as ls on unix, dir on win
HV_EXPORT int listdir(const char* dir, std::list<hdir_t>& dirs);

#endif // HV_DIR_H_
