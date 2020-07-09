#ifndef HV_URL_H_
#define HV_URL_H_

#include <string>

#include "hexport.h"

HV_EXPORT std::string url_escape(const char* istr);
HV_EXPORT std::string url_unescape(const char* istr);

#endif // HV_URL_H_
