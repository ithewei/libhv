#include "hpath.h"

int main(int argc, char** argv) {
    std::string filepath = HPath::join("/mnt/share/image", "test.jpg");
    std::string basename = HPath::basename(filepath);
    std::string dirname = HPath::dirname(filepath);
    std::string filename = HPath::filename(filepath);
    std::string suffixname = HPath::suffixname(filepath);
    printf("filepath = %s\n", filepath.c_str());
    printf("basename = %s\n", basename.c_str());
    printf("dirname  = %s\n", dirname.c_str());
    printf("filename = %s\n", filename.c_str());
    printf("suffixname = %s\n", suffixname.c_str());
    return 0;
}
