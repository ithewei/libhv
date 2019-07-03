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

#include "htime.h"
#include "herr.h"

// cpp
#ifdef __cplusplus
#include "hlog.h"
#include "hstring.h"
#include "hsocket.h"

#include "hvar.h"
#include "hobj.h"
#include "hgui.h"
#include "hbuf.h"
#include "hfile.h"
#include "hscope.h"
#include "hmutex.h"
#include "hthread.h"
#include "hthreadpool.h"
#endif

//--------------------utils-----------------------------
#ifdef WITH_HW_UTILS
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
