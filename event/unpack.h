#ifndef HV_UNPACK_H_
#define HV_UNPACK_H_

#include "hloop.h"

int hio_unpack(hio_t* io, void* buf, int readbytes);
int hio_unpack_by_fixed_length(hio_t* io, void* buf, int readbytes);
int hio_unpack_by_delimiter(hio_t* io, void* buf, int readbytes);
int hio_unpack_by_length_field(hio_t* io, void* buf, int readbytes);

#endif // HV_UNPACK_H_
