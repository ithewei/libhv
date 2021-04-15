/*
 * @build: gcc -o bin/sha1 unittest/sha1_test.c util/sha1.c -I. -Iutil
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "sha1.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: sha1 file\n");
        printf("       sha1 -s string\n");
        return -10;
    }

    unsigned char ch = '1';
    char sha1[41];
    hv_sha1_hex(&ch, 1, sha1, sizeof(sha1));
    assert(strcmp(sha1, "356a192b7913b04c54574d18c28d46e6395428ab") == 0);

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
        unsigned char* buf = (unsigned char*)malloc(filesize);
        size_t nread = fread(buf, 1, filesize, fp);
        assert(nread == filesize);
        hv_sha1_hex(buf, filesize, sha1, sizeof(sha1));
    }
    else if (argc == 3) {
        const char* flags = argv[1];
        if (flags[0] == '-' && flags[1] == 's') {
            hv_sha1_hex((unsigned char*)argv[2], strlen(argv[2]), sha1, sizeof(sha1));
        }
        else {
            printf("Unrecognized flags '%s'\n", flags);
            return -40;
        }
    }

    puts(sha1);

    return 0;
}
