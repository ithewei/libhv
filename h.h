#ifndef H_H
#define H_H

// platform
#include "hplatform.h"
#ifdef __unix__
#include "hunix.h"
#endif

// c
#include "hversion.h"
#include "hdef.h"
#include "htime.h"
#include "hlog.h"
#include "herr.h"

// cpp
#ifdef __cplusplus
#include "hstring.h"
#include "hthread.h"
#include "hthreadpool.h"
#include "hmutex.h"
#include "hscope.h"
#include "singleton.h"

#include "hvar.h"
#include "hobj.h"

#include "hbuf.h"
#include "hbytearray.h"
#include "hgui.h"

#include "hframe.h"
#include "htable.h"
#endif

#endif // H_H