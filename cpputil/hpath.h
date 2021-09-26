#ifndef HV_PATH_H_
#define HV_PATH_H_

#include <string> // for std::string

#include "hexport.h"
#include "hplatform.h" // for stat

class HV_EXPORT HPath {
public:
    static bool exists(const char* path);
    static bool isdir(const char* path);
    static bool isfile(const char* path);

    // filepath = /mnt/share/image/test.jpg
    // basename = test.jpg
    // dirname  = /mnt/share/image
    // filename = test
    // suffixname = jpg
    static std::string basename(const std::string& str);
    static std::string dirname(const std::string& str);
    static std::string filename(const std::string& str);
    static std::string suffixname(const std::string& str);

    static std::string join(const std::string& dir, const std::string& filename);
};

#endif // HV_PATH_H_
