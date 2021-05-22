#ifndef HV_H_
#define HV_H_

/*
 * @功能: base模块下的头文件比较零散，所以提个了一个汇总的头文件，方便大家#include
 *
 */

/**
 * @copyright 2018 HeWei, all rights reserved.
 */

// platform
// 平台相关头文件
#include "hconfig.h"
#include "hexport.h"
#include "hplatform.h"

// c
// c语言头文件
#include "hdef.h"   // <stddef.h>
#include "hatomic.h"// <stdatomic.h>
#include "herr.h"   // <errno.h>
#include "htime.h"  // <time.h>
#include "hmath.h"  // <math.h>

#include "hbase.h"
#include "hversion.h"
#include "hsysinfo.h"
#include "hproc.h"
#include "hthread.h"
#include "hmutex.h"
#include "hsocket.h"
#include "hssl.h"

#include "hlog.h"
#include "hbuf.h"

// cpp
// c++语言头文件
#ifdef __cplusplus
#include "hmap.h"       // <map>
#include "hstring.h"    // <string>
#include "hfile.h"
#include "hdir.h"
#include "hurl.h"
#include "hscope.h"
#endif

#endif  // HV_H_
