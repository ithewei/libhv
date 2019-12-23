#ifndef HV_NMAP_H_
#define HV_NMAP_H_

#include <map>
#include "hsocket.h"

// addr => 0:down 1:up
typedef std::map<uint32_t, int> Nmap;

// ip = segment + host
// segment16: 192.168.x.x
// segment24: 192.168.1.x

// @return up_cnt
int nmap_discovery(Nmap* nmap);
int segment_discovery(const char* segment16, Nmap* nmap);
int host_discovery(const char* segment24, Nmap* nmap);

#endif // HV_NMAP_H_
