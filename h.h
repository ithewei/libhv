#ifndef HW_H_
#define HW_H_

/**
 * @copyright 2018 HeWei, all rights reserved.
 */

//-------------------base---------------------------
// platform
#include "hplatform.h"
#include "hdef.h"
#include "hversion.h"

// c
#include "hsysinfo.h"
#include "hproc.h"
#include "hmath.h"
#include "htime.h"
#include "herr.h"
#include "hlog.h"
#include "hmutex.h"
#include "hsocket.h"

// cpp
#ifdef __cplusplus
#include "hstring.h"
#include "hvar.h"
#include "hobj.h"
#include "hgui.h"
#include "hbuf.h"
#include "hfile.h"
#include "hscope.h"
#include "hthread.h"
#include "hthreadpool.h"
#endif

//--------------------utils-----------------------------
#ifdef WITH_HW_UTILS
#include "md5.h"
#include "base64.h"
#include "hbytearray.h"
#include "hframe.h"
#include "ifconfig.h"
#include "iniparser.h"
#include "json.hpp"
#include "singleton.h"
#include "htask.h"
#include "task_queue.h"
#endif

//--------------------misc------------------------------
#ifdef WITH_HW_MISC
#include "win32_getopt.h"
#endif

#endif  // HW_H_
