#ifndef HV_H_
#define HV_H_

/**
 * @copyright 2018 HeWei, all rights reserved.
 */

// platform
#include "hconfig.h"
#include "hexport.h"
#include "hplatform.h"

// c
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

#include "hlog.h"
#include "hbuf.h"

// cpp
#if defined (__cplusplus) && defined (WITH_EVPP)
#include "hmap.h"       // <map>
#include "hstring.h"    // <string>
#include "hfile.h"
#include "hpath.h"
#include "hdir.h"
#include "hurl.h"
#endif

#endif  // HV_H_
