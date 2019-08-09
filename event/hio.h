#ifndef HW_HIO_H_
#define HW_HIO_H_

#include "hloop.h"

//int hio_read (hio_t* io, void* buf, size_t len);
//@see io->readbuf
int hio_read (hio_t* io);
int hio_write(hio_t* io, const void* buf, size_t len);
int hio_close(hio_t* io);

int hio_accept (hio_t* io);
int hio_connect(hio_t* io);

#endif // HW_HIO_H_
