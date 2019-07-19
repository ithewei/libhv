/*
** Copyright (C) 2014 Wang Yaofu
** All rights reserved.
**
**Author:Wang Yaofu voipman@qq.com
**Description: The header file of class CrcHash.
*/

#ifndef _COMMON_CRC_CRCHASH_H_
#define _COMMON_CRC_CRCHASH_H_
#include <string>
#include <stdint.h>
namespace common {
    // string => 0x0000-0xffff
    uint16_t Hash16(const std::string& key);
    uint16_t Hash16(const char* cpKey, const int iKeyLen);

    // string => 0x00000000-0xffffffff
    uint32_t Hash32(const std::string& key);
    uint32_t Hash32(const char* cpKey, const int iKeyLen);

    // string => 0x0000000000000000-0xffffffffffffffff
    uint64_t Hash64(const std::string& key);
    uint64_t Hash64(const char* cpKey, const int iKeyLen);    
} // namespace common
#endif  // _COMMON_CRC_CRCHASH_H_
