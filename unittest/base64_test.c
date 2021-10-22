/*
 * @build: gcc -o bin/base64 unittest/base64_test.c util/base64.c -I. -Iutil
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "base64.h"

static void test() {
    unsigned char in[] = "0123456789";
    // test encode
    int encoded_size = BASE64_ENCODE_OUT_SIZE(10);
    char* encoded = (char*)malloc(encoded_size + 1);
    encoded_size = hv_base64_encode(in, 10, encoded);
    encoded[encoded_size] = '\0';
    assert(strcmp(encoded, "MDEyMzQ1Njc4OQ==") == 0);
    // test decode
    int decoded_size = BASE64_DECODE_OUT_SIZE(encoded_size);
    unsigned char* decoded = (unsigned char*)malloc(decoded_size);
    decoded_size = hv_base64_decode(encoded, encoded_size, decoded);
    assert(decoded_size == 10 && memcmp(in, decoded, decoded_size) == 0);

    free(encoded);
    free(decoded);
}

int main(int argc, char* argv[]) {
    test();

    if (argc < 3) {
        printf("Usage: base64 infile outfile\n");
        printf("       base64 -d infile outfile\n");
        return -10;
    }
    else if (argc == 3) {
        // encode file
        const char* infile = argv[1];
        const char* outfile = argv[2];

        FILE* infp = fopen(infile, "rb");
        if (infp == NULL) {
            printf("Open file '%s' failed!\n", infile);
            return -20;
        }
        fseek(infp, 0, SEEK_END);
        long filesize = ftell(infp);
        // printf("filesize=%ld\n", filesize);
        fseek(infp, 0, SEEK_SET);
        unsigned char* filebuf = (unsigned char*)malloc(filesize);
        size_t nread = fread(filebuf, 1, filesize, infp);
        assert(nread == filesize);

        int encoded_size = BASE64_ENCODE_OUT_SIZE(filesize);
        char* encoded = (char*)malloc(encoded_size + 1);
        encoded_size = hv_base64_encode(filebuf, filesize, encoded);
        encoded[encoded_size] = '\0';

        FILE* outfp = fopen(outfile, "w");
        if (outfp == NULL) {
            printf("Save file '%s' failed!\n", infile);
            return -20;
        }
        size_t nwrite = fwrite(encoded, 1, encoded_size, outfp);
        assert(nwrite == encoded_size);

        free(filebuf);
        free(encoded);
        fclose(infp);
        fclose(outfp);
    }
    else if (argc == 4) {
        const char* flags = argv[1];
        if (flags[0] == '-' && flags[1] == 'd') {
            // decode file
            const char* infile = argv[2];
            const char* outfile = argv[3];
            FILE* infp = fopen(infile, "r");
            if (infp == NULL) {
                printf("Open file '%s' failed!\n", infile);
                return -20;
            }
            fseek(infp, 0, SEEK_END);
            long filesize = ftell(infp);
            // printf("filesize=%ld\n", filesize);
            fseek(infp, 0, SEEK_SET);
            char* filebuf = (char*)malloc(filesize);
            size_t nread = fread(filebuf, 1, filesize, infp);
            assert(nread == filesize);

            int decoded_size = BASE64_DECODE_OUT_SIZE(filesize);
            unsigned char* decoded = (unsigned char*)malloc(decoded_size);
            decoded_size = hv_base64_decode(filebuf, filesize, decoded);

            FILE* outfp = fopen(outfile, "wb");
            if (outfp == NULL) {
                printf("Save file '%s' failed!\n", infile);
                return -20;
            }
            size_t nwrite = fwrite(decoded, 1, decoded_size, outfp);
            assert(nwrite == decoded_size);

            free(filebuf);
            free(decoded);
            fclose(infp);
            fclose(outfp);
        }
        else {
            printf("Unrecognized flags '%s'\n", flags);
            return -40;
        }
    }

    return 0;
}
