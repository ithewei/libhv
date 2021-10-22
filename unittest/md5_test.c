/*
 * @build: gcc -o bin/md5 unittest/md5_test.c util/md5.c -I. -Iutil
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "md5.h"

static void test() {
    unsigned char ch = '1';
    char md5[33] = {0};
    hv_md5_hex(&ch, 1, md5, sizeof(md5));
    assert(strcmp(md5, "c4ca4238a0b923820dcc509a6f75849b") == 0);
}

int main(int argc, char* argv[]) {
    test();

    if (argc < 2) {
        printf("Usage: md5 file\n");
        printf("       md5 -s string\n");
        return -10;
    }

    char md5[33] = {0};

    if (argc == 2) {
        const char* filepath = argv[1];
        FILE* fp = fopen(filepath, "rb");
        if (fp == NULL) {
            printf("Open file '%s' failed!\n", filepath);
            return -20;
        }
        fseek(fp, 0, SEEK_END);
        long filesize = ftell(fp);
        // printf("filesize=%ld\n", filesize);
        fseek(fp, 0, SEEK_SET);
        unsigned char* filebuf = (unsigned char*)malloc(filesize);
        size_t nread = fread(filebuf, 1, filesize, fp);
        assert(nread == filesize);
        hv_md5_hex(filebuf, filesize, md5, sizeof(md5));

        free(filebuf);
        fclose(fp);
    }
    else if (argc == 3) {
        const char* flags = argv[1];
        if (flags[0] == '-' && flags[1] == 's') {
            hv_md5_hex((unsigned char*)argv[2], strlen(argv[2]), md5, sizeof(md5));
        }
        else {
            printf("Unrecognized flags '%s'\n", flags);
            return -40;
        }
    }

    puts(md5);

    return 0;
}
