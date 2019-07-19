/*
** Copyright (C) 2014 Wang Yaofu
** All rights reserved.
**
**Author:Wang Yaofu voipman@qq.com
**Description: The source file of class CrcHash.
*/

#include "crchash.h"
#include "crc16.h"
#include "crc32.h"
#include "crc64.h"
namespace common {
    uint16_t Hash16(const std::string& key)
    {
        return crc16(key.c_str(), key.size());
    }
    uint16_t Hash16(const char* cpKey, const int iKeyLen)
    {
        return crc16(cpKey, iKeyLen);
    }

    uint32_t Hash32(const std::string& key)
    {
        return crc32(key.c_str(), key.size());
    }
    uint32_t Hash32(const char* cpKey, const int iKeyLen)
    {
        return crc32(cpKey, iKeyLen);
    }

    uint64_t Hash64(const std::string& key)
    {
        return crc64(key.c_str(), key.size());
    }
    uint64_t Hash64(const char* cpKey, const int iKeyLen)
    {
        return crc64(cpKey, iKeyLen);
    }
} // namespace common
